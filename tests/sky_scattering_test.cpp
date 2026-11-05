#include "Check.h"
#include "sky/SkyScatteringMath.h"
#include "sky/SkyViewLut.h"
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
    CHECK_NEAR(bounce.r, noBounce.r, 1e-12); CHECK_NEAR(bounce.b, noBounce.b, 1e-12); // ground is handled by the multiple-scattering LUT
    CHECK_NEAR(glm::length(sk::integrateSkySingleScattering(0, -zenith, zenith)), 0, 1e-12);
    CHECK_NEAR(glm::length(sk::integrateSkySingleScattering(0, glm::dvec3(0), zenith)), 0, 1e-12);
    for (double altitude : {0., 1., 50., 100.}) for (auto view : {zenith, horizon})
        for (auto sun : {zenith, horizon, -zenith}) {
            auto v = sk::integrateSkySingleScattering(altitude, view, sun);
            for (int i = 0; i < 3; ++i) { CHECK_FINITE(v[i]); CHECK(v[i] >= 0); }
        }
    CHECK_NEAR(sk::atmosphereHorizonMu(0), 0, 1e-12);
    CHECK(sk::atmosphereHorizonMu(50) < 0);
    // Sky-view LUT shares the same encoded/filtering convention with the GPU sky and IBL.
    const auto lut = sk::buildSkyViewLut(0, .5, 1, 1);
    CHECK(lut.size() == sk::kSkyViewWidth * sk::kSkyViewHeight * 4u);
    for (int y = 0; y < sk::kSkyViewHeight; ++y) for (int c = 0; c < 3; ++c)
        CHECK(lut[(y * sk::kSkyViewWidth) * 4 + c] == lut[(y * sk::kSkyViewWidth + sk::kSkyViewWidth-1) * 4 + c]);
    glm::vec3 sun(std::sqrt(.75f), .5f, 0.f);
    for (auto view : {glm::vec3(0,1,0), glm::vec3(1,0,0), glm::normalize(glm::vec3(.2f,.6f,.7f))}) {
        auto sampled = sk::sampleSkyViewLut(lut, view, sun);
        auto reference = sk::integrateSkySingleScattering(0, glm::dvec3(view), glm::dvec3(sun));
        for (int c = 0; c < 3; ++c) { CHECK_FINITE(sampled[c]); CHECK_NEAR(sampled[c], reference[c], .08); }
    }
    for (double mu : {-1., -.2, 0., .2, 1.}) CHECK_NEAR(sk::multipleMu(sk::multipleU(mu)), mu, 1e-12);
    // A geometric sum should agree with a finite series away from the stability cap.
    glm::dvec3 first(.2,.5,1.), feedback(.1,.4,.7), finite(0), term=first;
    for(int i=0;i<100;++i) { finite+=term; term*=feedback; }
    auto closed=sk::closeMultipleScatteringSeries(first,feedback);
    for(int c=0;c<3;++c) CHECK_NEAR(closed[c],finite[c],1e-12);
    auto capped=sk::closeMultipleScatteringSeries(glm::dvec3(1),glm::dvec3(1));
    for(int c=0;c<3;++c) { CHECK_FINITE(capped[c]); CHECK_NEAR(capped[c],20.0,1e-10); }
    sk::AtmosphereParameters blackGround{}; blackGround.groundAlbedo=0.f;
    sk::AtmosphereParameters brightGround=blackGround; brightGround.groundAlbedo=1.f;
    auto downBlack=sk::multipleFirstOrderRay(0, {0,-1,0}, {0,1,0}, blackGround);
    auto downBright=sk::multipleFirstOrderRay(0, {0,-1,0}, {0,1,0}, brightGround);
    CHECK_NEAR(glm::length(downBlack.radiance),0,1e-12);
    CHECK(glm::length(downBright.radiance)>1.0); CHECK(downBright.feedback.r>.9);
    auto downNight=sk::multipleFirstOrderRay(0, {0,-1,0}, {0,-1,0}, brightGround);
    CHECK_NEAR(glm::length(downNight.radiance),0,1e-12);
    const auto darkLut=sk::buildMultipleScatteringLut(blackGround);
    const auto brightLut=sk::buildMultipleScatteringLut(brightGround);
    auto darkMs=sk::sampleMultipleScatteringLut(darkLut,0,1,blackGround);
    auto brightMs=sk::sampleMultipleScatteringLut(brightLut,0,1,brightGround);
    for(int c=0;c<3;++c) CHECK(brightMs[c]>darkMs[c]);
    sk::AtmosphereParameters noOzone{}; noOzone.ozone=0.f;
    sk::AtmosphereParameters highOzone=noOzone; highOzone.ozone=4.f;
    auto noOzoneLut=sk::buildMultipleScatteringLut(noOzone);
    auto highOzoneLut=sk::buildMultipleScatteringLut(highOzone);
    auto noOzoneHorizon=sk::sampleMultipleScatteringLut(noOzoneLut,0,0,noOzone);
    auto highOzoneHorizon=sk::sampleMultipleScatteringLut(highOzoneLut,0,0,highOzone);
    CHECK(highOzoneHorizon.g < noOzoneHorizon.g);
    for (double altitude : {0., 10., 50., 100.}) for (double sunMu : {-1., -.1, 0., .5, 1.}) {
        auto value=sk::sampleMultipleScatteringLut(brightLut,altitude,sunMu,brightGround);
        for (int c=0;c<3;++c) { CHECK_FINITE(value[c]); CHECK(value[c]>=0.0); CHECK(value[c]<=sk::kMultipleRange); }
    }
    const auto msLut = sk::buildMultipleScatteringLut();
    CHECK(msLut.size() == sk::kMultipleWidth * sk::kMultipleHeight * 4u);
    auto msDay = sk::sampleMultipleScatteringLut(msLut, 0, 1);
    auto msNight = sk::sampleMultipleScatteringLut(msLut, 0, -1);
    for (int c=0;c<3;++c) { CHECK_FINITE(msDay[c]); CHECK(msDay[c] > msNight[c]); CHECK(msNight[c] >= 0); }
    const auto withMs = sk::buildSkyViewLut(0, .5, 1, 1, {}, &msLut);
    auto msSky = sk::sampleSkyViewLut(withMs, glm::vec3(0,1,0), sun);
    auto singleSky = sk::sampleSkyViewLut(lut, glm::vec3(0,1,0), sun);
    for (int c=0;c<3;++c) CHECK(msSky[c] >= singleSky[c]);
    CHECK(glm::length(msSky) > glm::length(singleSky));
    auto rotated = sk::sampleSkyViewLut(lut, glm::vec3(0,0,1), glm::vec3(0,.5f,std::sqrt(.75f)));
    auto original = sk::sampleSkyViewLut(lut, glm::vec3(1,0,0), sun);
    CHECK(glm::length(rotated-original) < 1e-5f);
    CHECK(glm::length(sk::sampleSkyViewLut(lut, glm::vec3(0,-1,0), sun)) == 0);
    return t_.finish();
}
