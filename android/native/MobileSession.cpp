#include "MobileSession.h"
#include <algorithm>
#include <cmath>

namespace sk {
namespace {
// This is a simple humanoid debug rig, not a VRM model or the Vulkan renderer.
Rig makeRig() {
    Rig rig;
    struct Spec { HumanBone bone; const char* name; int parent; glm::vec3 t; };
    const Spec spec[] = {
        {HumanBone::Hips, "hips", -1, {0,1.f,0}}, {HumanBone::Spine, "spine", 0,{0,.10f,0}},
        {HumanBone::Chest, "chest", 1,{0,.12f,0}}, {HumanBone::UpperChest, "upperChest", 2,{0,.12f,0}},
        {HumanBone::Neck, "neck", 3,{0,.10f,0}}, {HumanBone::Head, "head", 4,{0,.08f,0}},
        {HumanBone::LShoulder, "leftShoulder", 3,{.05f,.08f,0}}, {HumanBone::LUpperArm, "leftUpperArm", 6,{.08f,0,0}},
        {HumanBone::LLowerArm, "leftLowerArm", 7,{.26f,0,0}}, {HumanBone::LHand, "leftHand", 8,{.24f,0,0}},
        {HumanBone::RShoulder, "rightShoulder", 3,{-.05f,.08f,0}}, {HumanBone::RUpperArm, "rightUpperArm", 10,{-.08f,0,0}},
        {HumanBone::RLowerArm, "rightLowerArm", 11,{-.26f,0,0}}, {HumanBone::RHand, "rightHand", 12,{-.24f,0,0}},
        {HumanBone::LUpperLeg, "leftUpperLeg", 0,{.09f,-.05f,0}}, {HumanBone::LLowerLeg, "leftLowerLeg", 14,{0,-.42f,0}},
        {HumanBone::LFoot, "leftFoot", 15,{0,-.40f,0}}, {HumanBone::LToes, "leftToes", 16,{0,-.05f,.09f}},
        {HumanBone::RUpperLeg, "rightUpperLeg", 0,{-.09f,-.05f,0}}, {HumanBone::RLowerLeg, "rightLowerLeg", 18,{0,-.42f,0}},
        {HumanBone::RFoot, "rightFoot", 19,{0,-.40f,0}}, {HumanBone::RToes, "rightToes", 20,{0,-.05f,.09f}}
    };
    for (const auto& s : spec) {
        Node n; n.name = s.name; n.parent = s.parent; n.t = s.t;
        rig.nodes.push_back(n); rig.human[static_cast<int>(s.bone)] = static_cast<int>(rig.nodes.size() - 1);
    }
    rig.buildOrder(); return rig;
}
}
bool MobileSession::load(const std::vector<std::string>& paths, std::string& error) {
    rig_ = makeRig();
    if (!db_.buildFromFiles(rig_, paths, "", 0, &error) || db_.clipNames.empty()) return false;
    pose_ = rig_.restPose(); matcher_.init(&rig_, &db_); matcher_.reset(position_, yaw_);
    matcher_.playClip(0, position_, yaw_); return true;
}
void MobileSession::update(float dt, float moveX, float moveY) {
    if (db_.clipNames.empty() || !std::isfinite(dt) || !std::isfinite(moveX) || !std::isfinite(moveY)) return;
    dt = std::clamp(dt, 0.f, .05f);
    glm::vec2 move(moveX, moveY); float length = glm::length(move);
    if (length > 1.f) move /= length;
    matcher_.update(dt, glm::vec3(move.x, 0.f, move.y) * 1.6f, position_, yaw_, pose_);
}
void MobileSession::nextClip() {
    if (db_.clipNames.empty()) return;
    const int clip = (std::max(-1, matcher_.debug.clip) + 1) % clipCount();
    matcher_.playClip(clip, position_, yaw_);
}
std::array<float, kHumanBoneCount * 3> MobileSession::joints() const {
    std::array<float, kHumanBoneCount * 3> result{};
    if (pose_.t.empty()) return result;
    std::vector<glm::mat4> globals; rig_.globals(pose_, globals);
    for (int i = 0; i < kHumanBoneCount; ++i) {
        glm::vec3 p(globals[rig_.human[i]][3]);
        result[i*3] = p.x; result[i*3+1] = p.y; result[i*3+2] = p.z;
    }
    return result;
}
std::string MobileSession::clipName() const {
    const int i = clipIndex(); return i >= 0 && i < clipCount() ? db_.clipNames[i] : "";
}
}
