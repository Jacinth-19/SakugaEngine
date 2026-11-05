#pragma once
// CPU mirror of the depth-based silhouette/normal finishing pass.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
namespace sk {
inline float outlineDepthDifference(float a, float b, float p22, float p32) {
    if ((a >= 1.f) != (b >= 1.f)) return 1.f;
    if (a >= 1.f) return 0.f;
    float za = std::abs(p32) / std::max(std::abs(a + p22), 1e-7f);
    float zb = std::abs(p32) / std::max(std::abs(b + p22), 1e-7f);
    float t = std::clamp((std::abs(za-zb) / std::max(std::min(za,zb), .01f) - .01f) / .09f, 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}
inline glm::vec3 outlineReconstructPosition(glm::vec2 uv, float depth, float p00, float p11, float p22, float p32) {
    float z=std::abs(p32)/std::max(std::abs(depth+p22),1e-7f);
    return {(uv.x*2.f-1.f)*z/p00, (uv.y*2.f-1.f)*z/p11, -z};
}
inline glm::vec3 outlineNormal(glm::vec3 dx, glm::vec3 dy) {
    auto n=glm::cross(dx,dy); float n2=glm::dot(n,n);
    return n2 > 1e-16f ? n / std::sqrt(n2) : glm::vec3(0.f);
}
}
