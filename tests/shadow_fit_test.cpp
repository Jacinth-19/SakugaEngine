// Shadow fit math (directional cascades + point-light cube faces), extracted from Renderer::beginFrame into
// gfx/ShadowFit.h. What is checked:
//   * the light-space texel snap really is stable under sub-texel motion (that is what stops cascade shimmering);
//   * the ortho frustum covers the sphere it was fitted to;
//   * the near-cascade sphere derived from the camera frustum slice contains that slice, and grows to contain the focus;
//   * point lights: 6 finite faces and all valid lights get slots, with a deterministic reduced-budget priority.
#include "Check.h"
#include "gfx/ShadowAtlas.h"
#include "gfx/ShadowFit.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

using namespace sk;

static const uint32_t kRes = 2048;

static bool insideNdc(glm::vec3 p) { return std::fabs(p.x) <= 1.001f && std::fabs(p.y) <= 1.001f && std::fabs(p.z) <= 1.001f; }

int main() {
    SK_TEST("shadow_fit_test");
    // 3x3 directional PCF taps at either atlas edge never reach the adjacent tile.
    for (int cascade = 0; cascade < 2; ++cascade) for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) {
        glm::vec2 tap = sk::clampCascadeTap(glm::vec2(.5f * (cascade + 1), 1.f) + glm::vec2(x / 2048.f, y / 1024.f), cascade, {2048, 1024});
        CHECK(tap.x >= .5f * cascade && tap.x < .5f * (cascade + 1));
        CHECK(tap.y >= 0.f && tap.y < 1.f);
    }


    const glm::vec3 light = glm::normalize(glm::vec3(0.45f, 0.62f, 0.64f));
    const glm::vec3 lowSun = glm::normalize(glm::vec3(0.9f, 0.05f, 0.2f));   // near-horizontal: exercises the up-vector fallback

    // --- texel size
    CHECK_NEAR(shadowTexelSize(5.f, 2048), 10.f / 2048.f, 1e-6f);
    CHECK_NEAR(shadowTexelSize(5.f, 0), 10.f, 1e-6f);              // no division by zero
    CHECK_NEAR(shadowTexelSize(0.f, 2048), 0.f, 1e-9f);

    // --- light-space up vector never degenerates
    CHECK(glm::length(shadowLightSpaceUp(glm::vec3(0, 1, 0))) > 0.f);
    CHECK(glm::length(shadowLightSpaceUp(glm::vec3(0, -1, 0))) > 0.f);
    CHECK_MSG(std::fabs(glm::dot(shadowLightSpaceUp(light), glm::normalize(light))) < 0.95f, "up hint is never parallel to the light");

    // --- snapping: sub-texel motion of the centre leaves the centre (and the matrix) untouched
    {
        float radius = 6.f, texel = shadowTexelSize(radius, kRes);
        glm::vec3 centre(1.3f, 0.f, -2.7f);
        glm::vec3 fwd = -light, up = shadowLightSpaceUp(light);
        glm::vec3 right = glm::normalize(glm::cross(fwd, up)), u = glm::cross(right, fwd);
        // start from a snapped point (exactly on the grid), then wobble by well under half a texel: no change
        glm::vec3 snapped = snapShadowCentre(centre, light, texel);
        for (float ox : {-0.2f, 0.1f, 0.3f}) for (float oy : {-0.3f, 0.25f}) {
            glm::vec3 moved = snapShadowCentre(snapped + right * (ox * texel) + u * (oy * texel), light, texel);
            CHECK_NEAR(glm::length(moved - snapped), 0.f, 1e-5f);
        }

        glm::mat4 m0 = fitShadowMatrix(snapped, radius, light, kRes);
        glm::mat4 m1 = fitShadowMatrix(snapped + right * (0.3f * texel), radius, light, kRes);
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) CHECK_NEAR(m1[c][r], m0[c][r], 1e-6f);

        // a real move (several texels) does change the matrix
        glm::mat4 m2 = fitShadowMatrix(snapped + right * (4.f * texel), radius, light, kRes);
        float diff = 0.f;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) diff += std::fabs(m2[c][r] - m0[c][r]);
        CHECK_MSG(diff > 1e-6f, "moving by several texels does move the cascade");
    }

    // --- the fitted ortho frustum covers the sphere
    for (glm::vec3 ld : {light, lowSun}) {
        glm::mat4 m = fitShadowMatrix(glm::vec3(0.4f, -0.2f, 1.7f), 5.f, ld, kRes);
        int inside = 0, total = 0;
        for (int i = 0; i < 500; ++i) {
            // deterministic pseudo-random points on the sphere
            float a = 2.399963f * (float)i, z = 1.f - 2.f * ((float)(i % 50) + 0.5f) / 50.f, r = std::sqrt(std::max(0.f, 1.f - z * z));
            glm::vec3 dir(std::cos(a) * r, std::sin(a) * r, z);
            glm::vec3 p = glm::vec3(0.4f, -0.2f, 1.7f) + dir * (5.f * 0.99f);
            glm::vec4 c = m * glm::vec4(p, 1.f);
            glm::vec3 ndc = glm::vec3(c) / c.w;
            ++total;
            inside += insideNdc(ndc) ? 1 : 0;
        }
        CHECK_MSG(inside == total, "every point of the shadow sphere lands inside the light frustum");
        CHECK(total > 400);
    }

    // --- camera slice sphere contains the whole frustum slice
    {
        const float zn = 0.3f, zf = 11.f, fov = 0.9f, aspect = 16.f / 9.f;
        glm::mat4 proj = glm::perspective(fov, aspect, zn, zf);
        glm::vec3 camPos(2.f, 3.f, -4.f), target(0.f, 1.f, 0.f);
        glm::mat4 view = glm::lookAt(camPos, target, glm::vec3(0, 1, 0));
        ShadowSphere s = cameraSliceSphere(proj, view, camPos, zn, zf);
        CHECK(s.radius > 0.f);
        CHECK_FINITE(s.centre.x); CHECK_FINITE(s.radius);

        // eight corners of the slice (inverse projection)
        glm::mat4 invVP = glm::inverse(proj * view);
        int inside = 0;
        for (int i = 0; i < 8; ++i) {
            float ndcZ = (i & 4) ? 1.f : -1.f;      // glm perspective: near = -1, far = +1
            glm::vec4 p = invVP * glm::vec4(((i & 1) ? 1.f : -1.f), ((i & 2) ? 1.f : -1.f), ndcZ, 1.f);
            glm::vec3 w = glm::vec3(p) / p.w;
            float d = glm::length(w - s.centre);
            ++inside;
            CHECK_MSG(d <= s.radius * (1.f + 1e-3f), "slice corner is covered by the sphere");
            CHECK_MSG(glm::dot(w - camPos, cameraForward(view)) > 0.f, "corner is in front of the camera");
        }
        CHECK(inside == 8);

        // grow to include the focus
        glm::vec3 focus = s.centre + glm::vec3(0.f, 0.f, s.radius * 2.5f);
        ShadowSphere g = growSphereToInclude(s, focus, 1.2f);
        CHECK(glm::length(focus - g.centre) + 1.2f <= g.radius + 1e-4f);
        CHECK(glm::length(s.centre - g.centre) + s.radius <= g.radius + 1e-4f);
        // a point already inside does not change the sphere
        ShadowSphere same = growSphereToInclude(s, s.centre, 0.f);
        CHECK_NEAR(same.radius, s.radius, 1e-6f);
        CHECK_NEAR(glm::length(same.centre - s.centre), 0.f, 1e-6f);
    }

    // --- radius quantisation: half-metre steps, never shrinking, capped
    {
        CHECK_NEAR(quantiseShadowRadius(3.11f, 100.f), 3.5f, 1e-6f);
        CHECK_NEAR(quantiseShadowRadius(3.0f, 100.f), 3.0f, 1e-6f);
        CHECK_NEAR(quantiseShadowRadius(99.f, 22.4f), 22.4f, 1e-6f);
        for (float r = 0.1f; r < 40.f; r += 0.37f) {
            float q = quantiseShadowRadius(r, 1e4f);
            CHECK(q >= r - 1e-6f);
            CHECK(q - r < 0.5f + 1e-6f);
        }
    }

    // --- point-light cube faces
    {
        glm::vec3 pos(1.f, 2.f, 3.f);
        float range = 6.f;
        std::vector<glm::vec3> axes;
        for (int f = 0; f < kPointShadowFaces; ++f) {
            glm::mat4 m = pointShadowFaceMatrix(pos, range, f);
            for (int i = 0; i < 16; ++i) CHECK_FINITE(((const float*)&m)[i]);
            // a point 1 m along the face axis is in front of that face and inside its frustum
            glm::vec3 p = pos + pointShadowFaceDirs()[f] * 1.f;
            glm::vec4 c = m * glm::vec4(p, 1.f);
            CHECK(c.w > 0.f);
            glm::vec3 ndc = glm::vec3(c) / c.w;
            CHECK_MSG(insideNdc(ndc), "the face axis maps to the middle of its cube face");
            CHECK_NEAR(ndc.x, 0.f, 1e-4f);
            CHECK_NEAR(ndc.y, 0.f, 1e-4f);
            axes.push_back(pointShadowFaceDirs()[f]);
        }
        // the six directions are distinct, axis-aligned and opposite in pairs
        for (int f = 0; f < kPointShadowFaces; ++f) {
            CHECK_NEAR(glm::length(axes[f]), 1.f, 1e-6f);
            CHECK_NEAR(glm::dot(axes[f], pointShadowFaceUps()[f]), 0.f, 1e-6f);
            CHECK_NEAR(glm::dot(axes[f], axes[(f / 2) * 2 + (1 - f % 2)]), -1.f, 1e-6f);   // (-x,+x),(-y,+y),(-z,+z)
        }
        // out-of-range face index is clamped, not UB
        glm::mat4 bad = pointShadowFaceMatrix(pos, range, 99);
        CHECK_FINITE(((const float*)&bad)[0]);
    }

    // --- which point lights get a shadow atlas
    {
        glm::vec3 pos[4] = {{0, 0, 100}, {1, 0, 0}, {0, 0, 10}, {0.5f, 0, 0.5f}};
        float intensity[4] = {1.f, 0.f, 1.f, 1.f};        // light 1 is switched off
        float range[4] = {4.f, 4.f, 4.f, 4.f};
        int slot[4];
        int n = choosePointShadowLights(pos, intensity, range, glm::vec3(0.f), 2, slot);
        // distances to the focus: light 1 = off, light 3 ~= 0.7 m, light 2 = 10 m (reach 6), light 0 = 100 m (reach 96)
        CHECK_NEAR(n, 2.f, 0);
        CHECK_NEAR(slot[3], 0.f, 0);                      // nearest gets slot 0
        CHECK_NEAR(slot[2], 1.f, 0);                      // second nearest
        CHECK_NEAR(slot[0], -1.f, 0);                     // farther than the two allocated lights
        CHECK_NEAR(slot[1], -1.f, 0);                     // switched off: never gets a slot

        glm::vec3 near4[4] = {{0.5f, 0, 0}, {1.f, 0, 0}, {2.f, 0, 0}, {3.f, 0, 0}};
        float on4[4] = {1.f, 1.f, 1.f, 1.f};
        n = choosePointShadowLights(near4, on4, range, glm::vec3(0.f), 2, slot);
        CHECK_NEAR(n, 2.f, 0);
        CHECK(slot[0] >= 0 && slot[1] >= 0);              // the two closest
        CHECK_NEAR(slot[2], -1.f, 0);
        CHECK_NEAR(slot[3], -1.f, 0);
        CHECK(slot[0] != slot[1]);
    }

    // Every tile occupies a distinct viewport. Simulate the shader's PCF clamping at tile edges
    // for both actual atlas resolutions; taps cannot bleed into the next face or light.
    for (uint32_t res : {256u, 512u}) for (int row = 0; row < 4; ++row) for (int face = 0; face < 6; ++face) {
        PointShadowTile t = pointShadowTile(face, row, res);
        CHECK(t.x == face * int(res) && t.y == row * int(res) && t.size == res);
        for (int edge : {0, 1}) for (int tap = -1; tap <= 1; ++tap) {
            const float local = edge ? float(res) : 0.f;
            const float x = std::clamp(float(t.x) + local + float(tap), float(t.x) + .5f, float(t.x + int(res)) - .5f);
            const float y = std::clamp(float(t.y) + local + float(tap), float(t.y) + .5f, float(t.y + int(res)) - .5f);
            CHECK(x >= t.x + .5f && x <= t.x + res - .5f);
            CHECK(y >= t.y + .5f && y <= t.y + res - .5f);
        }
    }

    // All four lamps cast, even when the camera/hero is far from their influence volumes.
    {
        glm::vec3 pos[4] = {{0, 2, 0}, {10, 2, 0}, {20, 2, 0}, {30, 2, 0}};
        float intensity[4] = {1, 2, 3, 4}, range[4] = {4, 4, 4, 4};
        int slot[4];
        CHECK(choosePointShadowLights(pos, intensity, range, glm::vec3(500.f), 4, slot) == 4);
        for (int i = 0; i < 4; ++i) {
            CHECK(slot[i] >= 0 && slot[i] < 4);
            for (int j = 0; j < i; ++j) CHECK(slot[i] != slot[j]);
        }
        intensity[0] = 0.f; range[1] = 0.f; intensity[2] = INFINITY;
        CHECK(choosePointShadowLights(pos, intensity, range, glm::vec3(0), 20, slot) == 1);
        CHECK(slot[0] == -1 && slot[1] == -1 && slot[2] == -1 && slot[3] == 0);
        CHECK(choosePointShadowLights(pos, intensity, range, glm::vec3(0), 0, slot) == 0);
    }

    return t_.finish();
}
