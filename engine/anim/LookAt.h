#pragma once
// Head / neck / eye look-at: the gaze target is clamped to anatomical limits, smoothed with critically damped springs, and the
// rotation is distributed over chest -> neck -> head (residual based), with eyes leading the head, micro-saccades and
// expression fallbacks (VRM lookLeft/Right/Up/Down) for rigs without eye bones.
#include "anim/Rig.h"
#include <glm/glm.hpp>

namespace sk {

struct LookAtSettings {
    bool enabled = true;
    float weight = 1.f;                         // 0 = ignore the target
    float yawLimit = 1.25f, pitchUp = 0.55f, pitchDown = 0.65f;     // radians (head + neck + chest)
    float chestShare = 0.12f, neckShare = 0.38f, headShare = 0.50f;
    float headHalflife = 0.13f, eyeHalflife = 0.045f;
    float eyeYawLimit = 0.50f, eyePitchLimit = 0.32f;
    bool saccades = true;
};

struct LookAtState {
    float yaw = 0.f, pitch = 0.f;               // smoothed head aim relative to the body (radians)
    float eyeYaw = 0.f, eyePitch = 0.f;
    float saccadeYaw = 0.f, saccadePitch = 0.f, nextSaccade = 0.5f, clock = 0.f;
    float activeWeight = 0.f;                   // smoothed enable weight
    glm::vec2 exprLook{0.f};                    // eye yaw/pitch normalised to [-1, 1] (drives lookLeft/Right/Up/Down expressions)
};

// target: world-space point to look at. Returns true when the pose was modified.
bool solveLookAt(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 glm::vec3 targetWorld, bool hasTarget, float dt, const LookAtSettings& s, LookAtState& st);

}  // namespace sk
