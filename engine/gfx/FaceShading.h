#pragma once
// ZZZ-style face shading, as pure functions. This header is the single source of truth: shaders/toon.frag mirrors these
// formulas (see the "face shading helpers" block there) and tests/face_hair_test.cpp checks the properties below.
//
// Three touches, each with a bounded, local effect:
//   1. nose/under-eye lightening: a small UV ellipse that lets part of the *cast* shadow through (per texel, inside the
//      ellipse only). The previous implementation multiplied the whole-face shadow by the ellipse and therefore erased
//      the face shadow completely wherever the ellipse overlapped it.
//   2. normal flattening: the shading normal is lerped toward the view vector (ZZZ uses ~85%), which flattens the
//      gradient across the face so the light map does the shaping. This is a real normal change, not a shadow tweak.
//   3. complementary silhouette aura: an additive rim that only exists where the (flattened) normal turns away from the
//      view (silhouette), tinted by the complement of the key light. It never brightens the interior of the face.
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace sk {
namespace face {

struct ShadingParams {
    float noseLighten = 0.18f;    // fraction of the SDF cast shadow released inside the ellipse (0..1, 0 = off)
    float normalFlatten = 0.85f;  // how far the shading normal is lerped toward the view vector (0..1)
    float auraStrength = 0.12f;   // peak additive silhouette aura (0 = off)
    float auraPower = 2.0f;       // falloff exponent; higher = thinner edge
    glm::vec2 noseCentre{0.5f, 0.45f};
    glm::vec2 noseRadius{0.12f, 0.08f};
    float noseInner = 0.60f;      // normalised ellipse radius where the mask is still 1
    float noseOuter = 1.00f;      // ... and where it reaches 0
};

inline float saturate(float v) { return std::clamp(v, 0.f, 1.f); }

inline float smoothstep01(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.f : 1.f;
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

// Normalised radius of the nose/under-eye ellipse (1 = on the ellipse outline).
inline float noseEllipseRadius(glm::vec2 uv, const ShadingParams& p = {}) {
    float rx = std::max(p.noseRadius.x, 1e-5f), ry = std::max(p.noseRadius.y, 1e-5f);
    glm::vec2 d = (uv - p.noseCentre) / glm::vec2(rx, ry);
    return glm::length(d);
}

// 1 inside the ellipse, falling to exactly 0 at noseOuter.
inline float noseMask(glm::vec2 uv, const ShadingParams& p = {}) {
    return 1.f - smoothstep01(p.noseInner, p.noseOuter, noseEllipseRadius(uv, p));
}

// Releases `amount` of the cast shadow inside the mask. Never negative, never above the input shadow, and 0 amount or 0
// mask is an exact no-op.
inline float lightenNoseShadow(float sdfShadow, float mask, float amount) {
    float s = saturate(sdfShadow);
    float keep = 1.f - saturate(amount) * saturate(mask);
    return std::max(0.f, s * keep);
}

// ZZZ face normal flattening: lerp toward the view vector, renormalised.
inline glm::vec3 flattenNormal(glm::vec3 n, glm::vec3 view, float amount) {
    float len = glm::length(n);
    if (len < 1e-8f) return n;
    n /= len;
    float vl = glm::length(view);
    if (vl < 1e-8f) return n;
    glm::vec3 v = view / vl;
    return glm::normalize(glm::mix(n, v, saturate(amount)));
}

// Silhouette-only aura: 0 when the surface faces the camera, rising toward the grazing edge.
inline float silhouetteAura(float dotNV, float strength, float power = 2.f) {
    float f = 1.f - saturate(dotNV);
    return saturate(strength) * std::pow(f, std::max(power, 0.001f));
}

// Complementary tint of the key light, mixed with the material's rim colour (keeps a hand-picked hue in control).
inline glm::vec3 complementaryTint(glm::vec3 lightColor, glm::vec3 rimColor, float mixAmount = 0.5f) {
    glm::vec3 c = glm::vec3(1.f) - glm::clamp(lightColor, glm::vec3(0.f), glm::vec3(1.f));
    return glm::mix(rimColor, c, saturate(mixAmount));
}

}  // namespace face
}  // namespace sk
