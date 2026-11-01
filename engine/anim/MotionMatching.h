#pragma once
// Motion matching: feature database built from BVH clips (retargeted to the character's humanoid rig),
// a brute-force nearest-neighbour search, and Bollo-style inertialization for transitions.
// Design follows Daniel Holden's "Motion Matching and The Road to Next-Gen Animation" /
// "Learned Motion Matching" reference code, and the feature set of the original Unity project
// (feet positions/velocities, hip velocity, predicted root motion & facing direction).
#include "anim/Bvh.h"
#include "anim/Rig.h"
#include <array>
#include <string>
#include <vector>

namespace sk {

constexpr int kTrajSamples = 3;
constexpr int kFeatureDim = 6 + 6 + 3 + kTrajSamples * 2 + kTrajSamples * 2;   // 27
enum FeatureGroup { FG_FootPos, FG_FootVel, FG_HipVel, FG_TrajPos, FG_TrajDir, FG_Count };

constexpr int kMotionCategoryCount = 9;
extern const char* kMotionCategoryNames[kMotionCategoryCount];
int motionCategoryOf(const std::string& clipStem);     // LAFAN1 file-name prefix -> category

struct MotionDatabase {
    float fps = 30.f;
    int frameCount = 0;
    int trajOffsets[kTrajSamples] = {10, 20, 30};               // frames ahead
    std::vector<std::string> clipNames;
    std::vector<int> clipStart, clipEnd;                        // [start, end) global frame ranges
    std::vector<int> frameClip;                                 // global frame -> clip
    std::vector<int> clipCategory;                              // per clip (kMotionCategoryNames)
    std::vector<uint8_t> frameCat;                              // per frame category (search mask lookup)
    // per-frame retargeted pose (character space, root-relative)
    std::vector<glm::quat> localRot;                            // frameCount * kHumanBoneCount (local to parent node)
    std::vector<glm::vec3> hipsLocalPos;                        // hips node local translation (rig space)
    std::vector<glm::vec3> rootPos;                             // ground-projected root, metres (scaled to character)
    std::vector<float> rootYaw;                                 // radians, 0 = +Z
    std::vector<float> features;                                // frameCount * kFeatureDim (normalised)
    std::vector<char> valid;
    std::array<bool, kHumanBoneCount> mapped{};
    float featureMean[kFeatureDim] = {};
    float featureScale[FG_Count] = {};
    float retargetScale = 1.f;

    // Loads every BVH in `files` (streamed, multi-threaded), retargets it to `rig` and builds the feature database.
    // The result is cached in `cacheDir` (empty = no cache) so the next start is instant.
    bool buildFromFiles(const Rig& rig, std::vector<std::string> files, const std::string& cacheDir, int maxClips = 0, std::string* error = nullptr);
    bool saveCache(const std::string& path, uint64_t key) const;
    bool loadCache(const std::string& path, uint64_t key);
    void buildFrameCategories();
    int clipsInCategory(int cat) const { int n = 0; for (int c : clipCategory) n += c == cat; return n; }
    static int groupOfDim(int d) { return d < 6 ? FG_FootPos : d < 12 ? FG_FootVel : d < 15 ? FG_HipVel : d < 15 + 2 * kTrajSamples ? FG_TrajPos : FG_TrajDir; }
    const float* feat(int f) const { return &features[(size_t)f * kFeatureDim]; }
};

struct MMSettings {
    float searchInterval = 0.10f;          // seconds between searches
    float switchMargin = 0.05f;            // transition only if best + margin < cost(current)
    float inertHalflife = 0.10f;           // seconds
    float velHalflife = 0.25f;             // trajectory prediction: velocity spring
    float yawHalflife = 0.20f;
    float weights[FG_Count] = {0.8f, 1.0f, 1.0f, 1.3f, 1.4f};
    float walkSpeed = 1.6f, runSpeed = 3.8f, slowSpeed = 0.9f;
    float turnHalflife = 0.15f;            // facing smoothing
    uint32_t categoryMask = (1u << 0) | (1u << 1);   // LAFAN1 families the search may always pick (walk, run). Jumps are added only while a jump is requested.
    bool contextMask = true;               // below runGateSpeed only Walk clips are searched (no jog / hop clips when strolling), Jumps only after requestJump()
    float runGateSpeed = 2.4f;
    float idlePoseWeight = 0.45f;          // when standing still: cost per radian away from the typical upright standing pose (no hands-on-knees / bowing idles)
};

struct MMDebug {
    int frame = -1, clip = -1; float cost = 0, currentCost = 0; int transitions = 0;
    glm::vec3 predicted[kTrajSamples]{}, predictedDir[kTrajSamples]{};
    glm::vec3 desiredVelocity{0}, velocity{0};
    // un-normalised feature vectors of the last search (character-local, metres / m/s): the query and the best database match
    float queryRaw[kFeatureDim] = {}, matchRaw[kFeatureDim] = {}, currentRaw[kFeatureDim] = {};
    glm::vec3 matchPos{0}; float matchYaw = 0.f; bool hasSearch = false;
};

class MotionMatcher {
public:
    MMSettings settings;
    MMDebug debug;

    void init(const Rig* rig, const MotionDatabase* db);
    // Advances the animation; moves (pos,yaw) by root motion and writes the pose for the humanoid bones.
    void update(float dt, const glm::vec3& desiredVelWorld, glm::vec3& pos, float& yaw, Pose& pose);
    void reset(const glm::vec3& pos, float yaw);
    void requestJump(float seconds = 1.2f) { jumpRequest_ = seconds; }
    const MotionDatabase* database() const { return db_; }
    // Mocap browser: plays one clip straight through (root motion still moves the entity). Any movement input returns to matching.
    void playClip(int clip, const glm::vec3& pos, float yaw);
    bool browsing() const { return browse_; }
    float currentFrame() const { return frameF_; }
    // Brute-force nearest neighbour over the current database, using the exact cost function of search() (minus the
    // idle bias and the context mask) and `categoryMask`. Returns -1 when the mask admits no frame. Used by the
    // motion-matching test and by tooling; search() itself calls the same helper.
    int nearestFrame(const float* queryFeatures, uint32_t categoryMask) const;

private:
    float jumpRequest_ = 0.f;
    std::vector<float> idleScore_;           // per frame: how far the upper-body pose is from the typical standing pose (lazy, see buildIdleScores)
    void buildIdleScores();
    struct Sample { std::array<glm::quat, kHumanBoneCount> r; glm::vec3 hips; };
    void samplePose(float frameF, Sample& s) const;
    void sampleVel(int frame, std::array<glm::vec3, kHumanBoneCount>& w, glm::vec3& hipsV) const;
    void rootAt(float frameF, glm::vec3& p, float& yaw) const;
    void search(const glm::vec3& pos, float yaw, const glm::vec3& desiredVel, bool force);
    void transitionTo(int newFrame);

    const Rig* rig_ = nullptr;
    const MotionDatabase* db_ = nullptr;
    float frameF_ = 0;                // global frame (fractional)
    float searchTimer_ = 0;
    glm::vec3 prevRoot_{0}; float prevRootYaw_ = 0;
    glm::vec3 vel_{0};                // smoothed world velocity (from root motion)
    float desiredYaw_ = 0; bool haveYaw_ = false;
    // inertialization state
    std::array<glm::quat, kHumanBoneCount> offR_;
    std::array<glm::vec3, kHumanBoneCount> offRv_;
    glm::vec3 offP_{0}, offPv_{0};
    bool started_ = false;
    bool browse_ = false;
};

}  // namespace sk
