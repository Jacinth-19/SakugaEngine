#pragma once
// CPU counterpart to shaders/post_atmosphere_math.glsl. Vulkan ZO perspective, no reversed Z.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace sk {
inline bool canSampleSceneDepth(bool imageQuerySucceeded, uint32_t formatSampleCounts, uint32_t sampledDepthCounts, uint32_t samples) {
    return imageQuerySucceeded && samples != 0 && (formatSampleCounts & samples) && (sampledDepthCounts & samples);
}
inline bool usePostAtmosphere(bool requested, bool postEnabled, bool depthSupported, float fogDensity) {
    return requested && postEnabled && depthSupported && std::isfinite(fogDensity) && fogDensity > 0.f;
}
inline bool supportsAtmosphereProjection(const glm::mat4& p) {
    return std::isfinite(p[0][0]) && std::isfinite(p[1][1]) && std::isfinite(p[2][2]) && std::isfinite(p[3][2]) &&
        std::abs(p[0][0]) > 1e-7f && std::abs(p[1][1]) > 1e-7f && p[2][2] <= -1.f && p[3][2] < 0.f &&
        std::abs(p[2][3] + 1.f) < 1e-6f && std::abs(p[3][3]) < 1e-6f &&
        std::abs(p[0][1]) < 1e-6f && std::abs(p[0][2]) < 1e-6f && std::abs(p[0][3]) < 1e-6f &&
        std::abs(p[1][0]) < 1e-6f && std::abs(p[1][2]) < 1e-6f && std::abs(p[1][3]) < 1e-6f &&
        std::abs(p[2][0]) < 1e-6f && std::abs(p[2][1]) < 1e-6f &&
        std::abs(p[3][0]) < 1e-6f && std::abs(p[3][1]) < 1e-6f;
}
// p22=p[2][2], p32=p[3][2]; p00/p11 include the Vulkan Y flip. Output is radial camera distance.
inline float perspectiveDepthDistance(float depth, glm::vec2 uv, float p00, float p11, float p22, float p32) {
    float z = std::abs(p32) / std::max(std::abs(depth + p22), 1e-7f);
    glm::vec2 ndc = uv * 2.f - glm::vec2(1.f);
    glm::vec2 xy{ndc.x * z / p00, ndc.y * z / p11};
    return std::sqrt(z*z + glm::dot(xy, xy));
}
inline float atmosphereFogAmount(float distance, float density) {
    float d = std::max(0.f, density) * std::max(0.f, distance);
    return 1.f - std::exp(-d*d);
}
inline float fogForSceneDepth(float depth, glm::vec2 uv, const glm::mat4& proj, float density) {
    if (depth >= 1.f) return 0.f; // clear-depth sky
    return atmosphereFogAmount(perspectiveDepthDistance(depth, uv, proj[0][0], proj[1][1], proj[2][2], proj[3][2]), density);
}
}
