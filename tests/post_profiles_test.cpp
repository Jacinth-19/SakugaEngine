#include "Check.h"
#include "post/PostSettings.h"
#include "post/BloomSelection.h"
int main() {
    SK_TEST("post_profiles_test");
    CHECK_NEAR(sk::bloomSelection({4,2,1}, 1), 1, 1e-6);
    CHECK_NEAR(sk::bloomSelection({1,2,4}, 1), 0, 1e-6);
    CHECK_NEAR(sk::bloomSelection({1,2,4}, 2), 1, 1e-6);
    CHECK_NEAR(sk::bloomSelection({2,2,2}, 3), 0, 1e-6);
    CHECK_NEAR(sk::bloomSelection({4,2,1}, 3), 1, 1e-6);
    CHECK_NEAR(sk::bloomSelection({0,0,0}, 0), 1, 1e-6);
    CHECK(sk::canUseMaterialBloomMask(true, true, 2, 7, 4, true));
    CHECK(!sk::canUseMaterialBloomMask(false, true, 2, 7, 4, true));
    CHECK(!sk::canUseMaterialBloomMask(true, false, 2, 7, 4, true));
    CHECK(!sk::canUseMaterialBloomMask(true, true, 1, 7, 4, true));
    CHECK(!sk::canUseMaterialBloomMask(true, true, 2, 3, 4, true));
    CHECK(!sk::canUseMaterialBloomMask(true, true, 2, 7, 4, false));
    const glm::vec3 brightSurface(8.f, 8.f, 8.f), redEmission(3.f, 0.f, 0.f);
    auto full = sk::materialBloomRadiance(brightSurface, redEmission, 1, 0);
    auto emissive = sk::materialBloomRadiance(brightSurface, redEmission, 1, 1);
    CHECK_NEAR(full.g, 8, 1e-6); CHECK_NEAR(emissive.r, 3, 1e-6); CHECK_NEAR(emissive.g, 0, 1e-6);
    CHECK_NEAR(glm::length(sk::materialBloomRadiance(brightSurface, {0,0,0}, 1, 1)), 0, 1e-6);
    auto transparent = sk::blendBloomRadiance(glm::vec3(2), emissive, .25f);
    CHECK_NEAR(transparent.r, 2.25f, 1e-6); CHECK_NEAR(transparent.g, 1.5f, 1e-6);
    auto weighted = sk::materialBloomRadiance(brightSurface, redEmission, .5f, 1);
    CHECK_NEAR(weighted.r, 1.5f, 1e-6);
    auto excludedBright = sk::bloomPrefilterTap({20,20,20}, 1.f, .5f, 0, 0.f);
    auto selectedDark = sk::bloomPrefilterTap({.1f,.1f,.1f}, 1.f, .5f, 0, 1.f);
    CHECK_NEAR(glm::length(excludedBright + selectedDark), 0, 1e-6);
    CHECK(glm::length(sk::bloomPrefilterTap({4,2,1}, 1.f, .5f, 1, 1.f)) > 1.f);
    CHECK_NEAR(glm::length(sk::bloomPrefilterTap({1,2,4}, 1.f, .5f, 1, 1.f)), 0, 1e-6);
    sk::PostSettings base;
    base.exposure = 1.42f; base.lutStrength = .35f; base.enabled = false;
    for (auto style : {sk::PostProfile::Celestial, sk::PostProfile::Cinematic,
                       sk::PostProfile::Atmospheric, sk::PostProfile::Painterly}) {
        auto p = sk::applyPostProfile(style, base);
        CHECK_NEAR(p.exposure, 1.42, 1e-5); CHECK_NEAR(p.lutStrength, .35, 1e-5); CHECK(!p.enabled);
        CHECK(p.bloom); CHECK(p.tonemap == 1); CHECK(p.bloomIntensity > 0); CHECK(p.bloomThreshold > 0);
        CHECK(!sk::planPost(p, 1920, 1080).bloom);
        p.enabled = true;
        auto plan = sk::planPost(p, 1921, 1081);
        CHECK(plan.bloom); CHECK(plan.halation == (style == sk::PostProfile::Cinematic));
        CHECK(plan.halationWidth == 480); CHECK(plan.halationHeight == 270);
        CHECK_FINITE(p.saturation); CHECK_FINITE(p.contrast); CHECK_FINITE(p.halation);
    }
    sk::PostSettings finish;
    CHECK(!sk::planPost(finish, 16, 16).finish);
    finish.ink = .2f; CHECK(sk::planPost(finish, 16, 16).finish);
    finish.enabled = false; CHECK(!sk::planPost(finish, 16, 16).finish);
    finish = {}; finish.depthInk = .5f;
    CHECK(sk::planPost(finish, 16, 16).finish);
    CHECK(sk::useGeometryInk(finish, true, true));
    CHECK(!sk::useGeometryInk(finish, false, true)); CHECK(!sk::useGeometryInk(finish, true, false));
    finish.enabled = false; CHECK(!sk::useGeometryInk(finish, true, true));
    finish = {}; finish.normalInk = .5f; CHECK(sk::useGeometryInk(finish, true, true));
    finish = {}; finish.colourLevels = 1; CHECK(!sk::planPost(finish, 16, 16).finish);
    finish.colourLevels = 2; CHECK(sk::planPost(finish, 16, 16).finish);
    finish = {}; finish.lensFlare = .2f; CHECK(!sk::planPost(finish, 16, 16).finish); // lens is HDR composite
    auto cinematic = sk::applyPostProfile(sk::PostProfile::Cinematic, sk::PostSettings{});
    cinematic.bloom = false; CHECK(!sk::planPost(cinematic, 8, 8).halation);
    cinematic.bloom = true; cinematic.bloomIntensity = 0; CHECK(!sk::planPost(cinematic, 8, 8).halation);
    cinematic.bloomIntensity = 1; auto tiny = sk::planPost(cinematic, 1, 3);
    CHECK(tiny.bloom); CHECK(tiny.halation); CHECK(tiny.halationWidth == 1); CHECK(tiny.halationHeight == 1);
    auto clean = sk::applyPostProfile(sk::PostProfile::Painterly, cinematic);
    CHECK_NEAR(clean.halation, 0, 1e-6); CHECK(!sk::planPost(clean, 10, 10).halation);
    return t_.finish();
}
