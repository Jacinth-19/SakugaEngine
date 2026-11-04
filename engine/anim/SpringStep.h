#pragma once
// Spring-bone simulation core: settings/state plus the verlet tail solve, kept free of Model/Vulkan so it is testable.
//
// Round 7 regression fixed here: the per-entity "spring tuning" sliders were applied with
//     spring.stiffnessScale *= tuning.stiffness;      (inside Entity::update, i.e. every frame, on the entity's
//                                                      persistent settings object)
// so any slider != 1.0 compounded exponentially (2.0 -> x2 per frame -> inf within seconds) and persisted across frames.
// Tuning is now *resolved* into a per-frame copy (applyTuning) and never written back. The collider radius padding is
// expressed in metres and added once, after the entity scale, instead of the previous double-scaled expression.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace sk {

struct SpringSettings {
    bool enabled = true;
    float stiffnessScale = 1.f, dragScale = 1.f, gravityScale = 1.f;
    float colliderRadiusPad = 0.f;                                  // metres, added to every collider radius (entity space)
    float windStrength = 0.f; glm::vec3 windDir{1, 0, 0.3f};        // optional ambient breeze
    float time = 0.f;
};

// Per-entity user tuning (ImGui sliders / CLI flags).
struct SpringTuning {
    float stiffness = 1.f, drag = 1.f, gravity = 1.f;
    float colliderRadiusPad = 0.f;                                  // metres
};

struct SpringState {
    struct J { glm::vec3 cur{0}, prev{0}; glm::vec3 boneAxisLocal{0, 1, 0}; float boneLength = 0.05f; glm::quat initRot{1, 0, 0, 0}; bool init = false; };
    std::vector<std::vector<J>> chains;
    glm::vec3 lastPos{0}; bool inited = false;
    void reset() { inited = false; for (auto& c : chains) for (auto& j : c) j.init = false; }
};

// Resolves the model's spring settings plus the entity's tuning into a fresh settings object. Pure: `base` is untouched,
// so calling it every frame is idempotent.
inline SpringSettings applyTuning(const SpringSettings& base, const SpringTuning& t) {
    SpringSettings s = base;
    s.stiffnessScale = base.stiffnessScale * std::max(t.stiffness, 0.f);
    s.dragScale = base.dragScale * std::clamp(t.drag, 0.f, 10.f);
    s.gravityScale = base.gravityScale * t.gravity;
    s.colliderRadiusPad = base.colliderRadiusPad + t.colliderRadiusPad;
    return s;
}

// A zero gravity or wind vector is legal in VRM; normalizing it would inject NaNs.
inline glm::vec3 springUnitDirection(const glm::vec3& v) {
    float l2 = glm::dot(v, v);
    return std::isfinite(l2) && l2 > 1e-12f ? v * (1.f / std::sqrt(l2)) : glm::vec3(0.f);
}

// One verlet step of a spring tail: drag on the implicit velocity, the rest-direction spring, external forces
// (gravity + wind), then the length constraint that keeps the bone length. `head` is the world position of the joint.
inline glm::vec3 springTailStep(const glm::vec3& cur, const glm::vec3& prev, const glm::vec3& head,
                                const glm::vec3& restDir, float drag01, float stiffness,
                                const glm::vec3& external, float boneLength, float h) {
    glm::vec3 inertia = (cur - prev) * (1.f - std::clamp(drag01, 0.f, 1.f));
    glm::vec3 stiff = restDir * (stiffness * h);
    glm::vec3 next = cur + inertia + stiff + external * h;
    glm::vec3 d = next - head;
    float len = glm::length(d);
    if (len < 1e-6f) return head + restDir * boneLength;
    return head + d * (boneLength / len);
}

// Closest point on segment ab to p.
inline glm::vec3 closestOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 ab = b - a; float l2 = glm::dot(ab, ab);
    float t = l2 > 1e-10f ? std::clamp(glm::dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    return a + ab * t;
}

// Pushes a tail out of a sphere/capsule collider and re-applies the bone length afterwards, so collision response can
// never stretch the joint away from its head. Returns the corrected tail.
inline glm::vec3 springPushOut(const glm::vec3& tail, const glm::vec3& a, const glm::vec3& b, float radius,
                               const glm::vec3& head, float boneLength) {
    glm::vec3 closest = closestOnSegment(tail, a, b);
    glm::vec3 d = tail - closest;
    float dist = glm::length(d);
    if (radius <= 0.f || dist >= radius) return tail;
    glm::vec3 n = dist > 1e-6f ? d / dist : glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 pushed = closest + n * radius;
    glm::vec3 fromHead = pushed - head;
    float len = glm::length(fromHead);
    if (len < 1e-6f) return pushed;
    return head + fromHead * (boneLength / len);
}

}  // namespace sk
