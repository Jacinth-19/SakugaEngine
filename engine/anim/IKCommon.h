#pragma once
#include "anim/Rig.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace sk {

glm::quat rotOf(const glm::mat4& m);                         // orthonormalised rotation part
glm::quat fromTo(glm::vec3 a, glm::vec3 b);                  // shortest rotation a -> b
glm::quat yawQ(float a);
float wrapPi(float a);
inline float expBlend(float dt, float halflife) { return 1.f - std::exp(-0.693147f * dt / std::max(halflife, 1e-4f)); }

// Analytic two-bone IK in model space. `G` are model-space globals; (nu, nl, nf) = upper, lower, end joint nodes.
// Rotates the upper and lower bones so the end joint reaches `target` (clamped to the reachable range), bending toward `pole`
// (a direction in model space; pass zero to keep the animated bend direction). Writes pose.r[nu], pose.r[nl] and returns true.
bool twoBoneIK(const Rig& rig, Pose& pose, const std::vector<glm::mat4>& G, int nu, int nl, int nf, glm::vec3 target, glm::vec3 pole, float* deficit = nullptr);

}  // namespace sk
