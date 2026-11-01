#pragma once
// Posture correction layer (Unity Animation Rigging analogue): post-animation, pre-IK pitch offsets on the spine chain
// plus a distributed "hunch" counter-rotation. Clamping and distribution live here so they are testable without a rig;
// Entity::update() only turns the result into quaternions.
#include <algorithm>
#include <cmath>

namespace sk {

struct PostureControl {
    float spine = 0.f, chest = 0.f, neck = 0.f, head = 0.f;   // degrees, +X = lean back
    float hunchCorrection = 0.f;                              // 0..1, straightens a hunched pose
    float weight = 1.f;                                       // overall blend weight
};

struct PostureLimits {
    float boneLimitDeg = 45.f;     // per-bone pitch clamp
    float hunchLimitDeg = 20.f;    // total counter-rotation budget for the hunch correction
    float spineShare = 0.40f, chestShare = 0.30f, neckShare = 0.30f;   // chain distribution (sums to 1)
};

namespace posture_detail {
inline float finiteOrZero(float v) { return std::isfinite(v) ? v : 0.f; }
}

// Clamps every field into its legal range (NaN -> 0). Everything downstream can assume a valid control.
inline PostureControl clampPosture(PostureControl p, const PostureLimits& l = {}) {
    auto clampDeg = [&](float v) { return std::clamp(posture_detail::finiteOrZero(v), -l.boneLimitDeg, l.boneLimitDeg); };
    p.spine = clampDeg(p.spine);
    p.chest = clampDeg(p.chest);
    p.neck = clampDeg(p.neck);
    p.head = clampDeg(p.head);
    p.hunchCorrection = std::clamp(posture_detail::finiteOrZero(p.hunchCorrection), 0.f, 1.f);
    p.weight = std::clamp(posture_detail::finiteOrZero(p.weight), 0.f, 1.f);
    return p;
}

// True when the layer would actually change the pose (lets Entity skip the work entirely).
inline bool postureActive(const PostureControl& p) {
    return p.weight > 0.f && (p.spine != 0.f || p.chest != 0.f || p.neck != 0.f || p.head != 0.f || p.hunchCorrection > 0.f);
}

// Total counter-rotation of the hunch correction, in radians (negative = straighten forward lean).
inline float hunchTotalRadians(const PostureControl& p, const PostureLimits& l = {}) {
    float deg = -l.hunchLimitDeg * std::clamp(p.hunchCorrection, 0.f, 1.f) * std::clamp(p.weight, 0.f, 1.f);
    return deg * 0.01745329252f;
}

// Per-bone radian offset of the hunch correction (spine, chest, neck). Shares are normalised so they always sum to 1.
inline void hunchRadiansPerBone(const PostureControl& p, float out[3], const PostureLimits& l = {}) {
    float shares[3] = {std::max(l.spineShare, 0.f), std::max(l.chestShare, 0.f), std::max(l.neckShare, 0.f)};
    float s = shares[0] + shares[1] + shares[2];
    if (s <= 0.f) { shares[0] = shares[1] = shares[2] = 1.f / 3.f; s = 1.f; }   // no preference: spread evenly
    float total = hunchTotalRadians(p, l);
    out[0] = total * (shares[0] / s);
    out[1] = total * (shares[1] / s);
    out[2] = total * (shares[2] / s);
}

}  // namespace sk
