#pragma once
// CPU counterpart of the depth-aware DoF gather's discontinuity rejection.
#include <algorithm>
#include <cmath>
namespace sk {
inline float dofViewDistance(float depth, float p22, float p32) {
    if (depth >= 1.f) return 100000.f;
    return std::abs(p32) / std::max(std::abs(depth + p22), 1e-7f);
}
inline float dofDepthWeight(float centreDepth, float tapDepth, float p22, float p32, float focusRange) {
    const bool centreSky = centreDepth >= 1.f, tapSky = tapDepth >= 1.f;
    if (centreSky != tapSky) return 0.f;
    if (centreSky) return 1.f;
    float a = dofViewDistance(centreDepth, p22, p32), b = dofViewDistance(tapDepth, p22, p32);
    float tolerance = std::max(.05f, std::min(a, b) * .08f + std::max(focusRange, .01f) * .05f);
    return std::exp2(-std::abs(a - b) / tolerance);
}
}
