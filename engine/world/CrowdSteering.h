#pragma once
// CPU-only helpers for animation-driven crowd control.
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
namespace sk {
// Re-requesting the same Detour target every frame restarts pathfinding and can starve the path queue.
struct CrowdTargetGate {
    glm::vec3 last{0};
    float elapsed = 0.f;
    bool active = false;
    void tick(float dt) { if (std::isfinite(dt) && dt > 0) elapsed += dt; }
    bool shouldRequest(glm::vec3 goal) const {
        glm::vec3 delta = goal - last;
        return !active || (elapsed >= .35f && glm::dot(delta, delta) > .25f * .25f) || elapsed >= 3.f;
    }
    void requested(glm::vec3 goal) { active = true; last = goal; elapsed = 0; }
    void clear() { active = false; elapsed = 0; }
};
inline glm::vec3 crowdRequestedVelocity(glm::vec3 v, float maxSpeed) {
    v.y = 0;
    if (!std::isfinite(v.x) || !std::isfinite(v.z) || !std::isfinite(maxSpeed) || maxSpeed <= 0) return glm::vec3(0);
    float speed = glm::length(v);
    if (speed <= .05f) return glm::vec3(0);
    return v * (std::min(speed, maxSpeed) / speed);
}
inline float locomotionInputScale(float magnitude, float deadzone = .08f) {
    if (!std::isfinite(magnitude)) return 0.f;
    deadzone = std::clamp(deadzone, 0.f, .99f);
    return std::clamp((magnitude - deadzone) / (1.f - deadzone), 0.f, 1.f);
}
}
