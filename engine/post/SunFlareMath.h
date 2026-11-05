#pragma once
// GPU-free projection for the optional depth-masked procedural-sun flare.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
namespace sk {
struct SunFlareProjection { glm::vec2 uv{0.f}; float radius = 0.f; bool visible = false; };
inline SunFlareProjection projectSunFlare(const glm::mat4& view, const glm::mat4& proj, glm::vec3 direction, float size) {
    SunFlareProjection out;
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) || !std::isfinite(size) || glm::dot(direction,direction) < 1e-12f || size <= 0.f) return out;
    glm::vec4 clip=proj*view*glm::vec4(glm::normalize(direction),0.f);
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || clip.w <= 1e-6f || !std::isfinite(proj[1][1])) return out;
    out.uv=glm::vec2(clip)/clip.w*.5f+.5f;
    if (out.uv.x < 0.f || out.uv.x > 1.f || out.uv.y < 0.f || out.uv.y > 1.f) return out;
    float angle=std::clamp(.052f*size, .001f, .3f);
    out.radius=std::clamp(std::abs(proj[1][1])*.5f*std::tan(angle), .001f, .2f);
    out.visible=true;
    return out;
}
inline float clearDepthCoverage(const float* depths, int count) {
    if (!depths || count <= 0) return 0.f;
    int clear=0; for(int i=0;i<count;++i) if(depths[i] >= 1.f) ++clear;
    return float(clear)/float(count);
}
}
