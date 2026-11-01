#pragma once
// Shadow fitting math, extracted from Renderer::beginFrame so it can be tested without a GPU.
//   * directional cascades: a light-space ortho box around a bounding sphere, snapped to whole shadow texels so the
//     shadow map does not crawl when the camera or the focus moves sub-texel distances;
//   * the near cascade sphere from the camera frustum slice (the same construction as Unity's / Holden's
//     "fit a sphere to the frustum slice" trick), grown to also contain the focus;
//   * point-light cube faces.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace sk {

inline float shadowTexelSize(float radius, uint32_t resolution) { return 2.f * radius / (float)std::max(resolution, 1u); }

inline glm::vec3 shadowLightSpaceUp(const glm::vec3& lightDir) {
    return std::fabs(lightDir.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
}

// Rounds the sphere centre to the shadow texel grid in light space (keeps z). Sub-texel motion of `centre` therefore
// produces a bit-identical matrix, which is what stops the cascade shimmering.
inline glm::vec3 snapShadowCentre(const glm::vec3& centre, const glm::vec3& lightDir, float texelSize) {
    if (texelSize <= 0.f) return centre;
    glm::vec3 fwd = -glm::normalize(lightDir), up = shadowLightSpaceUp(glm::normalize(lightDir));
    glm::vec3 right = glm::normalize(glm::cross(fwd, up)), u = glm::cross(right, fwd);
    float a = std::round(glm::dot(centre, right) / texelSize) * texelSize;
    float b = std::round(glm::dot(centre, u) / texelSize) * texelSize;
    return right * a + u * b + fwd * glm::dot(centre, fwd);
}

// Ortho light matrix that covers the sphere (centre, radius). Y is flipped for Vulkan clip space.
inline glm::mat4 fitShadowMatrix(const glm::vec3& centre, float radius, const glm::vec3& lightDir, uint32_t resolution) {
    glm::vec3 ld = glm::normalize(lightDir);
    float texel = shadowTexelSize(radius, resolution);
    glm::vec3 c = snapShadowCentre(centre, ld, texel);
    glm::vec3 up = shadowLightSpaceUp(ld);
    glm::mat4 lv = glm::lookAt(c + ld * radius * 2.f, c, up);
    glm::mat4 lp = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.f);
    lp[1][1] *= -1.f;
    return lp * lv;
}

struct ShadowSphere {
    glm::vec3 centre{0.f};
    float radius = 0.f;
};

// Camera forward from a view matrix (third column of the rotation part, negated).
inline glm::vec3 cameraForward(const glm::mat4& view) {
    return -glm::normalize(glm::vec3(view[0][2], view[1][2], view[2][2]));
}

// Bounding sphere of the camera frustum slice [zNear, zFar] (straight from the projection's tan halves).
// The slice's farthest points are the four corners at zFar, so the sphere is centred on the axis at zc and has the
// radius of those corners; `k2` is tanX^2 + tanY^2.
inline ShadowSphere cameraSliceSphere(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& cameraPos,
                                      float zNear, float zFar) {
    float tanY = 1.f / std::max(std::fabs(proj[1][1]), 1e-3f);
    float tanX = 1.f / std::max(std::fabs(proj[0][0]), 1e-3f);
    float k2 = tanX * tanX + tanY * tanY;
    float zc = std::min(0.5f * (zNear + zFar) * (1.f + k2), zFar);
    ShadowSphere s;
    s.radius = std::sqrt((zFar - zc) * (zFar - zc) + k2 * zFar * zFar);
    s.centre = cameraPos + cameraForward(view) * zc;
    return s;
}

// Smallest sphere that contains `s` and `point` (with a small pad) - or `s` unchanged when the point is already inside.
inline ShadowSphere growSphereToInclude(const ShadowSphere& s, const glm::vec3& point, float pad) {
    float d = glm::length(point - s.centre);
    if (d + pad <= s.radius) return s;
    float r = 0.5f * (d + pad + s.radius);
    if (d < 1e-6f) return ShadowSphere{s.centre, r};
    ShadowSphere out;
    out.centre = s.centre + (point - s.centre) / d * (r - s.radius);
    out.radius = r;
    return out;
}

// Quantised shadow radius: half-metre steps, capped. Keeps the texel size stable while the camera moves.
inline float quantiseShadowRadius(float radius, float cap) {
    return std::min(std::ceil(radius * 2.f) / 2.f, cap);
}

// ------------------------------------------------------------------ point lights: 6 cube faces per light
constexpr int kPointShadowFaces = 6;
inline const glm::vec3* pointShadowFaceDirs() {
    static const glm::vec3 dirs[kPointShadowFaces] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return dirs;
}
inline const glm::vec3* pointShadowFaceUps() {
    static const glm::vec3 ups[kPointShadowFaces] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
    return ups;
}

// View-projection of one cube face (90 degree + a hair of overlap, Y flipped for Vulkan).
inline glm::mat4 pointShadowFaceMatrix(const glm::vec3& position, float range, int face) {
    face = std::clamp(face, 0, kPointShadowFaces - 1);
    glm::mat4 proj = glm::perspective(glm::half_pi<float>() + 0.02f, 1.f, 0.08f, std::max(range * 1.15f, 1.f));
    proj[1][1] *= -1.f;
    return proj * glm::lookAt(position, position + pointShadowFaceDirs()[face], pointShadowFaceUps()[face]);
}

// Which (up to) `maxShadows` lights get a cube atlas: the ones nearest to the focus, within `reach` metres of it.
// Returns the number of allocated slots and fills `slotOfLight` (-1 = no shadow).
inline int choosePointShadowLights(const glm::vec3 positions[4], const float intensity[4], const float range[4],
                                   const glm::vec3& focus, int maxShadows, float reach, int slotOfLight[4]) {
    float dist[4];
    int order[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; ++i) {
        slotOfLight[i] = -1;
        dist[i] = intensity[i] > 0.f ? glm::length(positions[i] - focus) - range[i] : 1e9f;
    }
    std::sort(order, order + 4, [&](int a, int b) { return dist[a] < dist[b]; });
    int n = 0;
    for (int k = 0; k < 4 && n < maxShadows; ++k) {
        int i = order[k];
        if (dist[i] > reach) break;
        slotOfLight[i] = n++;
    }
    return n;
}

}  // namespace sk
