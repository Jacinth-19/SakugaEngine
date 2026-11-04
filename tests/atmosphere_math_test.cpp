#include "Check.h"
#include "post/AtmosphereMath.h"
#include "post/PostSettings.h"
#include <glm/gtc/matrix_transform.hpp>
int main() {
    SK_TEST("atmosphere_math_test");
    CHECK(sk::canSampleSceneDepth(true, 7, 7, 4));
    CHECK(!sk::canSampleSceneDepth(false, 7, 7, 4));
    CHECK(!sk::canSampleSceneDepth(true, 3, 7, 4));
    CHECK(!sk::canSampleSceneDepth(true, 7, 3, 4));
    CHECK(!sk::canSampleSceneDepth(true, 7, 7, 0));
    CHECK(sk::usePostAtmosphere(true, true, true, .01f));
    CHECK(!sk::usePostAtmosphere(false, true, true, .01f));
    CHECK(!sk::usePostAtmosphere(true, false, true, .01f));
    CHECK(!sk::usePostAtmosphere(true, true, false, .01f));
    CHECK(!sk::usePostAtmosphere(true, true, true, 0.f));
    CHECK(sk::useDepthOfField(true, 2.f, true, true));
    CHECK(!sk::useDepthOfField(false, 2.f, true, true));
    CHECK(!sk::useDepthOfField(true, 0.f, true, true));
    CHECK(!sk::useDepthOfField(true, 2.f, false, true));
    CHECK(!sk::useDepthOfField(true, 2.f, true, false));
    auto p = glm::perspective(glm::radians(55.f), 16.f/9.f, .1f, 120.f); p[1][1] *= -1.f;
    CHECK(sk::supportsAtmosphereProjection(p));
    CHECK(!sk::supportsAtmosphereProjection(glm::mat4(1.f)));
    auto invalid = p; invalid[0][0] = 0.f; CHECK(!sk::supportsAtmosphereProjection(invalid));
    invalid = p; invalid[2][0] = .15f; CHECK(!sk::supportsAtmosphereProjection(invalid)); // off-centre
    invalid = p; invalid[1][0] = .15f; CHECK(!sk::supportsAtmosphereProjection(invalid)); // skew
    for (float z : {.1f, .5f, 2.f, 20.f, 100.f}) for (auto xy : {glm::vec2(0.f), glm::vec2(.5f,-.25f), glm::vec2(-.7f,.8f)}) {
        glm::vec3 view{xy.x*z, xy.y*z, -z};
        glm::vec4 clip = p * glm::vec4(view, 1.f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        auto uv = glm::vec2(ndc) * .5f + .5f;
        float reconstructed = sk::perspectiveDepthDistance(ndc.z, uv, p[0][0], p[1][1], p[2][2], p[3][2]);
        CHECK_NEAR(reconstructed, glm::length(view), .02f);
        CHECK_FINITE(sk::fogForSceneDepth(ndc.z, uv, p, .02f));
    }
    CHECK_NEAR(sk::fogForSceneDepth(1.f, {.5f,.5f}, p, .02f), 0.f, 1e-6);
    CHECK_NEAR(sk::atmosphereFogAmount(10.f, 0.f), 0.f, 1e-6);
    CHECK(sk::atmosphereFogAmount(100.f, .02f) > sk::atmosphereFogAmount(10.f, .02f));
    sk::PostSettings settings;
    CHECK(!settings.atmosphere);
    CHECK(sk::applyPostProfile(sk::PostProfile::Atmospheric, settings).atmosphere);
    CHECK(!sk::applyPostProfile(sk::PostProfile::Painterly, sk::applyPostProfile(sk::PostProfile::Atmospheric, settings)).atmosphere);
    return t_.finish();
}
