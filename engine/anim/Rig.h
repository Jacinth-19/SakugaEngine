#pragma once
// Skeleton, pose and keyframed clip types shared by the glTF loader, the animation player and motion matching.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace sk {

enum class HumanBone : int {
    Hips, Spine, Chest, UpperChest, Neck, Head,
    LShoulder, LUpperArm, LLowerArm, LHand, RShoulder, RUpperArm, RLowerArm, RHand,
    LUpperLeg, LLowerLeg, LFoot, LToes, RUpperLeg, RLowerLeg, RFoot, RToes,
    Count
};
constexpr int kHumanBoneCount = (int)HumanBone::Count;
const char* humanBoneName(HumanBone b);                       // VRM 1.0 naming ("leftUpperArm", ...)
int humanBoneFromVrmName(const std::string& n);               // -1 if not one of the tracked bones

struct Node {
    std::string name;
    int parent = -1;
    glm::vec3 t{0}; glm::quat r{1, 0, 0, 0}; glm::vec3 s{1};
};

struct Pose {
    std::vector<glm::vec3> t, s;
    std::vector<glm::quat> r;
    void resize(size_t n) { t.resize(n); r.resize(n); s.resize(n, glm::vec3(1)); }
};

struct Skin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};

// VRMC_node_constraint (VRM 1.0): roll / aim / rotation constraints used for twist bones and clothing helpers.
struct NodeConstraint {
    enum Type { Roll, Aim, Rotation } type = Roll;
    int node = -1, source = -1;
    int axis = 0;                    // Roll: 0..2 = X,Y,Z.  Aim: 0..5 = +X,-X,+Y,-Y,+Z,-Z
    float weight = 1.f;
};

struct Rig {
    std::vector<NodeConstraint> constraints;
    std::vector<Node> nodes;
    std::vector<int> order;                  // parents before children
    std::vector<Skin> skins;
    int human[kHumanBoneCount];
    int eyeL = -1, eyeR = -1;               // optional eye bones (VRM leftEye / rightEye)
    std::vector<std::vector<int>> children;
    Rig() { for (int& h : human) h = -1; }

    void buildOrder();
    Pose restPose() const;
    void globals(const Pose& p, std::vector<glm::mat4>& out) const;
    // Recomputes `out` for `node` and all of its descendants after that node's local transform changed.
    void updateSubtree(int node, const Pose& p, std::vector<glm::mat4>& out) const;
    // Evaluates node constraints on top of an animated pose (updates the constrained nodes' local rotations).
    void applyConstraints(Pose& p) const;
    int find(const std::string& name) const;
    int bone(HumanBone b) const { return human[(int)b]; }
    bool isHumanoid() const { return human[(int)HumanBone::Hips] >= 0 && human[(int)HumanBone::LUpperLeg] >= 0; }
};

struct AnimChannel {
    int node = -1;
    enum Path { Translation, Rotation, Scale } path = Translation;
    bool step = false;
    std::vector<float> times;
    std::vector<glm::vec4> values;           // xyz(+w for rotation, xyzw order)
};

struct AnimClip {
    std::string name;
    float duration = 0;
    std::vector<AnimChannel> channels;
    void sample(float time, bool loop, Pose& pose) const;     // overwrites only the animated components
};

}  // namespace sk
