#include "Check.h"
#include "sky/SkyProfiles.h"
int main() {
    SK_TEST("sky_profiles_test");
    sk::SkyPalette p{{.2f,.4f,.9f}, {.7f,.8f,1.f}, {.4f,.6f,.9f}, {.7f,.8f,.9f}};
    auto base = sk::applySkyStyle(sk::SkyStyle::Celestial, p, 1.f);
    CHECK_NEAR(glm::length(base.zenith - p.zenith), 0, 1e-6);
    for (auto style : {sk::SkyStyle::Cinematic, sk::SkyStyle::Atmospheric, sk::SkyStyle::Painterly}) {
        auto x = sk::applySkyStyle(style, p, 1.f);
        CHECK(glm::length(x.zenith - base.zenith) > .01f);
        CHECK(glm::length(x.horizon - base.horizon) > .01f);
        for (int k = 0; k < 3; ++k) { CHECK_FINITE(x.zenith[k]); CHECK(x.zenith[k] >= 0); CHECK_FINITE(x.fog[k]); }
    }
    return t_.finish();
}
