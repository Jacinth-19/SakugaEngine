// Motion matching search: feature metric, nearest-neighbour results, category masking, and the real BVH loader.
// The metric and the brute-force loop are the ones used at runtime (anim/MotionFeatures.h + MotionMatcher::nearestFrame);
// the database is synthetic here so the expected answers are known exactly, while the BVH files exercised at the end are
// the ones vendored in assets/motion.
#include "Check.h"
#include "SyntheticMotion.h"
#include "anim/Bvh.h"
#include "anim/MotionFeatures.h"
#include "anim/MotionMatching.h"

#include <cmath>
#include <string>
#include <vector>

using namespace sk;

int main() {
    SK_TEST("mm_search_test");

    // ------------------------------------------------------------------ feature layout
    {
        int counts[FG_Count] = {0, 0, 0, 0, 0};
        for (int d = 0; d < kFeatureDim; ++d) {
            int g = MotionDatabase::groupOfDim(d);
            CHECK(g >= 0 && g < FG_Count);
            ++counts[g];
        }
        CHECK_NEAR(counts[FG_FootPos], 6.f, 0);
        CHECK_NEAR(counts[FG_FootVel], 6.f, 0);
        CHECK_NEAR(counts[FG_HipVel], 3.f, 0);
        CHECK_NEAR(counts[FG_TrajPos], 2.f * kTrajSamples, 0);
        CHECK_NEAR(counts[FG_TrajDir], 2.f * kTrajSamples, 0);
        CHECK_NEAR(kFeatureDim, 27.f, 0);
        CHECK_NEAR(motionCategoryOf("walk1_subject1"), 0.f, 0);
        CHECK_NEAR(motionCategoryOf("run1_subject2"), 1.f, 0);
        CHECK_NEAR(motionCategoryOf("sprint1_subject4"), 1.f, 0);
        CHECK_NEAR(motionCategoryOf("jumps1_subject1"), 3.f, 0);
        CHECK_NEAR(motionCategoryOf("dance1_subject1"), 5.f, 0);
        CHECK_NEAR(motionCategoryOf("ground1_subject1"), 8.f, 0);
    }

    // ------------------------------------------------------------------ weighted cost
    {
        float q[kFeatureDim] = {0}, x[kFeatureDim] = {0}, w[kFeatureDim];
        for (int d = 0; d < kFeatureDim; ++d) w[d] = 1.f;
        // w[12] is FG_HipVel -> weight 1 by this array, so check exactness explicitly
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q, x, w, 1e30f), 0.f, 1e-9f);

        x[0] = 2.f; x[5] = -1.f;
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q, x, w, 1e30f), 5.f, 1e-6f);
        float q2[kFeatureDim] = {0}; q2[0] = 2.f; q2[5] = -1.f;
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q2, x, w, 1e30f), 0.f, 1e-9f);       // symmetry
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(x, q2, w, 1e30f), 0.f, 1e-9f);

        // per-group weights scale the groups independently
        for (int d = 0; d < kFeatureDim; ++d) w[d] = (MotionDatabase::groupOfDim(d) == FG_FootPos) ? 3.f : 0.f;
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q, x, w, 1e30f), 3.f * (4.f + 1.f), 1e-5f);
        for (int d = 0; d < kFeatureDim; ++d) w[d] = (MotionDatabase::groupOfDim(d) == FG_HipVel) ? 2.f : 0.f;
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q, x, w, 1e30f), 0.f, 1e-9f);         // hip velocity differs only

        // the early-out fires as soon as the partial sum exceeds the limit
        for (int d = 0; d < kFeatureDim; ++d) w[d] = 1.f;
        float early = weightedFeatureCost<kFeatureDim>(q, x, w, 1.f);
        CHECK(early > 1.f);
        // base (the idle bias) is included and alone can exceed the limit
        CHECK_NEAR(weightedFeatureCost<kFeatureDim>(q, q, w, 1e30f, 0.75f), 0.75f, 1e-6f);
        CHECK(weightedFeatureCost<kFeatureDim>(q, q, w, 0.5f, 0.75f) > 0.5f);
    }

    // ------------------------------------------------------------------ nearest neighbour on a known database
    MotionDatabase db = sktest::makeSyntheticDb();
    Rig rig = sktest::makeSyntheticRig();
    {
        MotionMatcher mm; mm.init(&rig, &db);
        const uint32_t all = ~0u;
        for (int f : {1, 5, 30, 59, 60, 61, 100, 150, 179, 180, 240, 299}) {
            int best = mm.nearestFrame(db.feat(f), all);
            CHECK_MSG(best == f, "an exact feature match finds its own frame");
        }
        // a frame in the run clip is picked when the query is its own features, even though walk frames are numerous
        CHECK(mm.nearestFrame(db.feat(200), all) == 200);

        // category masks restrict the search: only Stand (0) frames are allowed
        const uint32_t standOnly = 1u << 0;
        int bestStand = mm.nearestFrame(db.feat(200), standOnly);   // query from the run clip
        CHECK(bestStand >= 0);
        CHECK(db.frameCat[bestStand] == 0);
        CHECK(db.frameClip[bestStand] != 2);

        // run-only mask never returns a walk frame
        const uint32_t runOnly = 1u << 1;
        int bestRun = mm.nearestFrame(db.feat(70), runOnly);
        CHECK(bestRun >= 0);
        CHECK_NEAR(db.frameCat[bestRun], 1.f, 0);
        CHECK_NEAR(db.frameClip[bestRun], 2.f, 0);        // clip 2 == "run"

        // an empty mask has no answer
        CHECK_NEAR(mm.nearestFrame(db.feat(100), 1u << 20), -1.f, 0);
        // a null query is rejected instead of dereferenced
        CHECK_NEAR(mm.nearestFrame(nullptr, all), -1.f, 0);

        // the "invalid" first frame is never returned
        for (int f = 1; f < db.frameCount; ++f) {
            if (mm.nearestFrame(db.feat(f), all) == 0) { CHECK_MSG(false, "frame 0 is marked invalid"); break; }
        }

        // a query at the trajectory of walk speed matches the walk clip, not the run clip
        float walkQuery[kFeatureDim] = {0};
        walkQuery[12] = 1.6f;
        for (int t = 0; t < kTrajSamples; ++t) {
            walkQuery[15 + t * 2 + 1] = 1.6f * (float)db.trajOffsets[t] / db.fps;
            walkQuery[21 + t * 2 + 1] = 1.f;
        }
        int walkBest = mm.nearestFrame(walkQuery, all);
        CHECK(walkBest >= db.clipStart[1] && walkBest < db.clipEnd[1]);
    }

    // ------------------------------------------------------------------ database shape invariants
    {
        CHECK(db.frameCount > 0);
        CHECK_NEAR(db.valid[0], 0.f, 0);
        CHECK_NEAR((double)db.features.size(), (double)db.frameCount * kFeatureDim, 0);
        CHECK_NEAR((double)db.localRot.size(), (double)db.frameCount * kHumanBoneCount, 0);
        CHECK_NEAR((double)db.frameClip.size(), (double)db.frameCount, 0);
        for (int c = 0; c < (int)db.clipStart.size(); ++c) {
            CHECK(db.clipStart[c] >= 0);
            CHECK(db.clipEnd[c] > db.clipStart[c]);
            CHECK(db.clipEnd[c] <= db.frameCount);
            CHECK(db.clipCategory[c] >= 0 && db.clipCategory[c] < kMotionCategoryCount);
        }
        for (int f = 0; f < db.frameCount; ++f) {
            CHECK_FINITE(db.feat(f)[12]);
            CHECK(db.frameClip[f] >= 0 && db.frameClip[f] < (int)db.clipStart.size());
        }
    }

    // ------------------------------------------------------------------ movement-context gating
    // Regression: characters behaved as if only the two vendored clips existed, because MMSettings::categoryMask was
    // hard-wired to (walk | run) and nothing could reach the other clip families.
    {
        MMSettings st;
        CHECK_MSG(st.categoryMask == (1u << kMotionCategoryCount) - 1, "every clip family is eligible by default");
        const float walk = 1.4f, run = 4.0f;
        const uint32_t walkOnly = 1u << 0, runOnly = 1u << 1, obstacles = 1u << 2, jumps = 1u << 3, dance = 1u << 5, ground = 1u << 8;

        // moving: locomotion families only, run gated by speed
        uint32_t movingSlow = resolveCategoryMask(st, true, walk, false);
        CHECK((movingSlow & walkOnly) != 0);
        CHECK((movingSlow & obstacles) != 0);
        CHECK_MSG((movingSlow & runOnly) == 0, "no jogging when strolling");
        CHECK((movingSlow & dance) == 0);
        CHECK((movingSlow & jumps) == 0);
        uint32_t movingFast = resolveCategoryMask(st, true, run, false);
        CHECK((movingFast & runOnly) != 0);

        // standing: everything except the triggered families
        uint32_t idle = resolveCategoryMask(st, false, 0.f, false);
        CHECK_MSG((idle & dance) != 0, "idle characters can use dance / fight / aiming / interaction");
        CHECK_MSG((idle & jumps) == 0, "no jumping while standing still");
        CHECK_MSG((idle & ground) == 0, "no lying down on their own");
        CHECK((idle & walkOnly) != 0);

        // a jump request unlocks Jumps even while standing
        uint32_t jumping = resolveCategoryMask(st, false, 0.f, true);
        CHECK_MSG((jumping & jumps) != 0, "requested jumps are reachable");
        CHECK((resolveCategoryMask(st, true, walk, true) & jumps) != 0);

        // the base mask is a hard limit: a walk-only configuration never yields dance
        MMSettings walkCfg = st; walkCfg.categoryMask = walkOnly | runOnly;
        CHECK((resolveCategoryMask(walkCfg, false, 0.f, false) & dance) == 0);
        CHECK((resolveCategoryMask(walkCfg, false, 0.f, true) & jumps) == 0);   // jumps are not in the base mask

        // context gating can be switched off entirely
        MMSettings noCtx = st; noCtx.contextMask = false;
        CHECK_NEAR((double)resolveCategoryMask(noCtx, true, walk, false), (double)noCtx.categoryMask, 0);
        CHECK_NEAR((double)resolveCategoryMask(noCtx, false, 0.f, false), (double)noCtx.categoryMask, 0);

        // a database without walk clips (strolling) yields an empty mask; search() falls back to the base mask
        MMSettings runOnlyCfg = st; runOnlyCfg.categoryMask = runOnly;
        CHECK_NEAR((double)resolveCategoryMask(runOnlyCfg, true, walk, false), 0.0, 0);
    }

    // ------------------------------------------------------------------ the real BVH files
    {
        const std::string dir = SAKUGA_ASSET_DIR "/motion/";
        for (const char* name : {"walk1_subject1.bvh", "run1_subject2.bvh"}) {
            BvhClip clip; std::string err;
            bool ok = loadBvh(dir + name, clip, &err);
            CHECK_MSG(ok, std::string("loaded ") + name + (ok ? "" : ": " + err));
            if (!ok) continue;
            CHECK_MSG(clip.joints.size() >= 20, "LAFAN1 skeleton has >= 20 joints (End Sites dropped)");
            CHECK_MSG(clip.frameCount > 100, "clip has frames");
            CHECK_MSG(clip.channelCount > 0, "clip has channels");
            CHECK_NEAR(clip.frameTime, 1.f / 30.f, 0.005f);
            CHECK_NEAR((double)clip.data.size(), (double)clip.frameCount * clip.channelCount, 0);
            CHECK_FINITE(clip.data.empty() ? 0.f : clip.data[0]);

            // hierarchy sanity: parents precede children, one root
            int roots = 0;
            for (size_t j = 0; j < clip.joints.size(); ++j) {
                int p = clip.joints[j].parent;
                if (p < 0) ++roots; else CHECK((size_t)p < j);
            }
            CHECK_NEAR(roots, 1.f, 0);

            // forward kinematics produces finite, non-degenerate world positions
            std::vector<glm::vec3> pos; std::vector<glm::quat> rot;
            clip.fk(0, 0.01f, pos, rot);
            CHECK_NEAR((double)pos.size(), (double)clip.joints.size(), 0);
            for (size_t j = 0; j < pos.size(); ++j) {
                CHECK_FINITE(pos[j].x); CHECK_FINITE(pos[j].y); CHECK_FINITE(pos[j].z);
                CHECK_FINITE(rot[j].w);
                CHECK_NEAR(glm::length(rot[j]), 1.f, 1e-3f);
            }
            CHECK_MSG(clip.findJoint(clip.joints[0].name) == 0, "root joint is addressable by name");

            // the hips actually move over the clip (i.e. this is motion, not a rest pose)
            std::vector<glm::vec3> posEnd;
            clip.fk(clip.frameCount - 1, 0.01f, posEnd, rot);
            float hipTravel = 0.f;
            for (size_t j = 0; j < pos.size() && j < posEnd.size(); ++j) hipTravel = std::max(hipTravel, glm::length(posEnd[j] - pos[j]));
            CHECK_MSG(hipTravel > 0.05f, "the clip is animated");
        }

        // a garbage file is rejected with an error instead of crashing
        BvhClip bad; std::string err;
        CHECK(!loadBvh(dir + "does_not_exist.bvh", bad, &err));
        CHECK(!err.empty());
    }

    return t_.finish();
}
