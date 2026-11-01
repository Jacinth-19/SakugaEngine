#pragma once
// Wall-touch hand IK: while running or walking along a wall the nearest hand reaches out, lands on the surface (palm flat against the
// wall, fingers along it) and is released smoothly again when the wall falls away or the character sprints.
#include "anim/Rig.h"
#include <glm/glm.hpp>

namespace sk {
class World;

struct HandIKSettings {
    bool enabled = true;
    float weight = 1.f;
    float reachScale = 1.0f;          // fraction of the arm length used as reach radius
    float palmOffset = 0.065f;        // wrist distance from the surface (m)
    float sprintFade = 0.85f;         // how strongly sprinting suppresses the touch (0..1)
    float maxWallSlope = 0.5f;        // |normal.y| above this is a floor / ceiling, not a wall
};

struct HandIKState {
    struct Hand { float w = 0.f; glm::vec3 target{0}; glm::vec3 normal{0, 0, 1}; bool had = false; bool touching = false; } hand[2];
};

bool solveHandIK(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 float dt, float speedNorm /*0 idle .. 1 run*/, bool sprinting, const HandIKSettings& s, HandIKState& st, const World* world);

}  // namespace sk
