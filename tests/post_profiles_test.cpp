#include "Check.h"
#include "post/PostSettings.h"
int main() {
    SK_TEST("post_profiles_test");
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
    auto cinematic = sk::applyPostProfile(sk::PostProfile::Cinematic, sk::PostSettings{});
    cinematic.bloom = false; CHECK(!sk::planPost(cinematic, 8, 8).halation);
    cinematic.bloom = true; cinematic.bloomIntensity = 0; CHECK(!sk::planPost(cinematic, 8, 8).halation);
    cinematic.bloomIntensity = 1; auto tiny = sk::planPost(cinematic, 1, 3);
    CHECK(tiny.bloom); CHECK(tiny.halation); CHECK(tiny.halationWidth == 1); CHECK(tiny.halationHeight == 1);
    auto clean = sk::applyPostProfile(sk::PostProfile::Painterly, cinematic);
    CHECK_NEAR(clean.halation, 0, 1e-6); CHECK(!sk::planPost(clean, 10, 10).halation);
    return t_.finish();
}
