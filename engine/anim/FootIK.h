#pragma once
#include "anim/Rig.h"
#include <glm/glm.hpp>

namespace sk {
class World;

struct FootIKSettings {
    bool enabled = true;
    float weight = 1.f;
    float contactHeight = 0.07f;     // a sole closer than this to the ground may become planted
    float contactSpeed = 0.35f;      // m/s: slower than this counts as stationary
    float releaseSpeed = 0.65f;      // planted foot is released when the animated foot moves faster than this ...
    float releaseDistance = 0.10f;   // ... or drifts further than this from the plant point
    float blendIn = 18.f, blendOut = 26.f;   // 1/s
    bool pelvis = true; float pelvisHalflife = 0.07f, maxPelvisDrop = 0.40f;   // pelvis follows the lower foot on slopes / stairs and eases hip-to-foot overextension
    bool alignFoot = true;           // tilt planted feet to the ground normal
    float groundY = 0.f;             // used when no World is supplied
};

struct FootIKState {
    struct Foot { bool locked = false, hasPrev = false; float w = 0.f; glm::vec3 lockPos{0}, prev{0}; float lockAnkleAboveSole = 0.07f; } foot[2];
    glm::vec3 heelLocal[2], toeLocal[2];     // sole sample points in the foot / toe bone frames (derived from the rest pose, true sole offset)
    float ankleRest[2] = {0.f, 0.f};
    float pelvisOffset = 0.f, pelvisTarget = 0.f;
    bool inited = false;
    // debug / UI
    float debugLift[2] = {0.f, 0.f}, soleHeight[2] = {0.f, 0.f}, groundAtFoot[2] = {0.f, 0.f};
};

// Foot placement: per-foot ground ray casts, true sole geometry (heel + toe points derived from the rest pose), pelvis adjustment,
// two-bone leg IK with world-space foot planting and ground-normal alignment.
// `G` holds model-space node matrices for `pose` and is kept up to date; `T` is the entity transform, `rootGroundY` the ground height under the root.
bool solveFootIK(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 float dt, const FootIKSettings& s, FootIKState& st, const World* world, float rootGroundY);

}  // namespace sk
