#pragma once
// Shared fixtures for the GPU-free tests: a synthetic humanoid rig and a synthetic motion database.
// They let the motion-matching code paths (feature metric, nearest neighbour, masks, root motion, inertialization) run
// without a Vulkan device and without the 333 MB LAFAN1 download; the real BVH files that *are* vendored are parsed by
// mm_search_test through the production loader.
#include "anim/MotionMatching.h"
#include "anim/Rig.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace sktest {

// 22-bone humanoid chain: enough for Rig::isHumanoid(), the human[] map and a pose round trip.
inline sk::Rig makeSyntheticRig() {
    using sk::HumanBone;
    using sk::Node;
    sk::Rig rig;
    struct Spec { HumanBone bone; const char* name; int parent; glm::vec3 t; };
    const Spec spec[] = {
        {HumanBone::Hips, "hips", -1, {0, 1.0f, 0}},
        {HumanBone::Spine, "spine", 0, {0, 0.10f, 0}},
        {HumanBone::Chest, "chest", 1, {0, 0.12f, 0}},
        {HumanBone::UpperChest, "upperChest", 2, {0, 0.12f, 0}},
        {HumanBone::Neck, "neck", 3, {0, 0.10f, 0}},
        {HumanBone::Head, "head", 4, {0, 0.08f, 0}},
        {HumanBone::LShoulder, "leftShoulder", 3, {0.05f, 0.08f, 0}},
        {HumanBone::LUpperArm, "leftUpperArm", 6, {0.08f, 0, 0}},
        {HumanBone::LLowerArm, "leftLowerArm", 7, {0.26f, 0, 0}},
        {HumanBone::LHand, "leftHand", 8, {0.24f, 0, 0}},
        {HumanBone::RShoulder, "rightShoulder", 3, {-0.05f, 0.08f, 0}},
        {HumanBone::RUpperArm, "rightUpperArm", 10, {-0.08f, 0, 0}},
        {HumanBone::RLowerArm, "rightLowerArm", 11, {-0.26f, 0, 0}},
        {HumanBone::RHand, "rightHand", 12, {-0.24f, 0, 0}},
        {HumanBone::LUpperLeg, "leftUpperLeg", 0, {0.09f, -0.05f, 0}},
        {HumanBone::LLowerLeg, "leftLowerLeg", 14, {0, -0.42f, 0}},
        {HumanBone::LFoot, "leftFoot", 15, {0, -0.40f, 0}},
        {HumanBone::LToes, "leftToes", 16, {0, -0.05f, 0.09f}},
        {HumanBone::RUpperLeg, "rightUpperLeg", 0, {-0.09f, -0.05f, 0}},
        {HumanBone::RLowerLeg, "rightLowerLeg", 18, {0, -0.42f, 0}},
        {HumanBone::RFoot, "rightFoot", 19, {0, -0.40f, 0}},
        {HumanBone::RToes, "rightToes", 20, {0, -0.05f, 0.09f}},
    };
    for (const Spec& s : spec) {
        sk::Node n;
        n.name = s.name; n.parent = s.parent; n.t = s.t;
        rig.nodes.push_back(n);
        rig.human[(int)s.bone] = (int)rig.nodes.size() - 1;
    }
    rig.buildOrder();
    return rig;
}

struct SyntheticClip {
    const char* name;
    int category;      // index into kMotionCategoryNames
    int frames;
    float speed;       // metres/second of root motion along +Z
};

// Builds a motion database with three clips (stand / walk / run) whose features encode their speed, so a nearest-neighbour
// search with a given desired velocity has an unambiguous expected answer.
inline sk::MotionDatabase makeSyntheticDb(float fps = 30.f) {
    sk::MotionDatabase db;
    db.fps = fps;
    const SyntheticClip clips[] = {{"stand", 0, 60, 0.f}, {"walk", 0, 120, 1.6f}, {"run", 1, 120, 3.8f}};
    int frame = 0;
    for (int c = 0; c < 3; ++c) {
        db.clipNames.push_back(clips[c].name);
        db.clipStart.push_back(frame);
        db.clipEnd.push_back(frame + clips[c].frames);
        db.clipCategory.push_back(clips[c].category);
        frame += clips[c].frames;
    }
    db.frameCount = frame;
    db.frameClip.assign((size_t)db.frameCount, 0);
    db.frameCat.assign((size_t)db.frameCount, 0);
    for (int c = 0; c < 3; ++c)
        for (int f = db.clipStart[c]; f < db.clipEnd[c]; ++f) { db.frameClip[f] = c; db.frameCat[f] = (uint8_t)db.clipCategory[c]; }
    db.valid.assign((size_t)db.frameCount, 1);
    db.valid[0] = 0;                                        // first frame of every DB is the T-pose lead-in
    db.clipStart[0] = 1;

    // poses: identity, with a small leg swing per clip (also proves the pose path is actually driven)
    db.localRot.assign((size_t)db.frameCount * sk::kHumanBoneCount, glm::quat(1, 0, 0, 0));
    db.hipsLocalPos.assign((size_t)db.frameCount, glm::vec3(0, 1.0f, 0));
    db.rootPos.assign((size_t)db.frameCount, glm::vec3(0));
    db.rootYaw.assign((size_t)db.frameCount, 0.f);
    for (int c = 0; c < 3; ++c) {
        float phase = 0.f;
        for (int f = db.clipStart[c]; f < db.clipEnd[c]; ++f) {
            float t = (float)(f - db.clipStart[c]) / fps;
            phase = t * (clips[c].speed > 0.f ? clips[c].speed * 3.f : 0.4f);
            float swing = std::sin(phase) * 0.25f;
            db.localRot[(size_t)f * sk::kHumanBoneCount + (int)sk::HumanBone::LUpperLeg] = glm::angleAxis(swing, glm::vec3(1, 0, 0));
            db.localRot[(size_t)f * sk::kHumanBoneCount + (int)sk::HumanBone::RUpperLeg] = glm::angleAxis(-swing, glm::vec3(1, 0, 0));
            db.hipsLocalPos[f] = glm::vec3(0, 1.0f + 0.02f * std::sin(phase * 2.f), 0);
            db.rootPos[f] = glm::vec3(0, 0, clips[c].speed * t);
            db.rootYaw[f] = 0.f;
        }
    }

    // features: 0..11 foot pos/vel, 12..14 hip vel, 15..20 trajectory positions, 21..26 trajectory directions
    db.features.assign((size_t)db.frameCount * sk::kFeatureDim, 0.f);
    db.featureMean[sk::kFeatureDim] = 0.f;
    for (int f = 0; f < db.frameCount; ++f) {
        int c = db.frameClip[f];
        float speed = clips[c].speed;
        float* x = &db.features[(size_t)f * sk::kFeatureDim];
        x[12] = speed;                                       // hip velocity along +Z (character space)
        for (int t = 0; t < sk::kTrajSamples; ++t) {
            float tt = db.trajOffsets[t] / db.fps;
            x[15 + t * 2 + 1] = speed * tt;                  // predicted position z
            x[21 + t * 2 + 1] = 1.f;                         // cos(0) heading
        }
        // unique fingerprint per frame so NN results are unambiguous
        x[26] = 1e-4f * (float)f;
    }
    for (int d = 0; d < sk::kFeatureDim; ++d) db.featureMean[d] = 0.f;
    for (int g = 0; g < sk::FG_Count; ++g) db.featureScale[g] = 1.f;
    for (int b = 0; b < sk::kHumanBoneCount; ++b) db.mapped[b] = true;
    db.buildFrameCategories();
    return db;
}

}  // namespace sktest
