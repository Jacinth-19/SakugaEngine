#pragma once
#include <glm/glm.hpp>
namespace sk {
// Shared dimensions for the visual stair boxes and collision geometry in MotionLab.
struct StairBox { glm::vec3 center, half; };
inline StairBox motionLabStair(int index) {
    float h = (index + 1) * .16f;
    float z = 12.f - (7.5f - index) * .3f;
    return {{0.f, h * .5f, z}, {1.5f, h * .5f, .15f}};
}
}
