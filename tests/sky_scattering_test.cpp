#include "Check.h"
#include "sky/SkyScatteringMath.h"
int main() {
    SK_TEST("sky_scattering_test");
    const glm::dvec3 zenith(0,1,0), horizon(1,0,0);
    const auto noon = sk::integrateSkySingleScattering(0, zenith, zenith);
    const auto night = sk::integrateSkySingleScattering(0, zenith, -zenith);
    const auto thin = sk::integrateSkySingleScattering(90, zenith, zenith);
    const auto zero = sk::integrateSkySingleScattering(0, zenith, zenith, 0, 0);
    CHECK(noon.r > night.r); CHECK(noon.b > noon.r); CHECK(glm::length(thin) < glm::length(noon));
    CHECK_NEAR(glm::length(zero), 0, 1e-12);
    sk::AtmosphereParameters model{}; model.groundAlbedo = 0;
    const auto noBounce = sk::integrateSkySingleScattering(0, zenith, zenith, 1, 1, 8, 24, model);
    model.groundAlbedo = 1;
    const auto bounce = sk::integrateSkySingleScattering(0, zenith, zenith, 1, 1, 8, 24, model);
    CHECK(bounce.r > noBounce.r); CHECK(bounce.b > noBounce.b);
    CHECK_NEAR(glm::length(sk::integrateSkySingleScattering(0, -zenith, zenith)), 0, 1e-12);
    CHECK_NEAR(glm::length(sk::integrateSkySingleScattering(0, glm::dvec3(0), zenith)), 0, 1e-12);
    for (double altitude : {0., 1., 50., 100.}) for (auto view : {zenith, horizon})
        for (auto sun : {zenith, horizon, -zenith}) {
            auto v = sk::integrateSkySingleScattering(altitude, view, sun);
            for (int i = 0; i < 3; ++i) { CHECK_FINITE(v[i]); CHECK(v[i] >= 0); }
        }
    CHECK_NEAR(sk::atmosphereHorizonMu(0), 0, 1e-12);
    CHECK(sk::atmosphereHorizonMu(50) < 0);
    return t_.finish();
}
