#include "Check.h"
#include "sky/AtmosphereLut.h"
#include <cmath>
int main() {
    SK_TEST("sky_atmosphere_test");
    const auto up = sk::atmosphereTransmittance(0, 1);
    const auto horizon = sk::atmosphereTransmittance(0, 0);
    const auto down = sk::atmosphereTransmittance(0, -1);
    const auto space = sk::atmosphereTransmittance(sk::kAtmosphereHeightKm, 1);
    for (int i = 0; i < 4; ++i) {
        CHECK(up[i] > horizon[i]); CHECK_NEAR(down[i], 0, 1e-12); CHECK_NEAR(space[i], 1, 1e-12);
    }
    CHECK(up[0] > up[1]); CHECK(up[1] > up[2]);
    CHECK(horizon[0] > horizon[1]); CHECK(horizon[1] > horizon[2]);
    CHECK(up[3] > up[0]);
    // Above the surface, downward rays can miss the ground and escape to space.
    CHECK(sk::atmosphereTransmittance(50, -1)[0] == 0);
    CHECK(sk::atmosphereTransmittance(50, -0.01)[0] > 0);
    for (double h : {0., 1., 25., 100.}) for (double mu : {-1., -0.1, 0., 0.1, 1.}) {
        for (double v : sk::atmosphereTransmittance(h, mu)) { CHECK_FINITE(v); CHECK(v >= 0 && v <= 1); }
    }
    auto lut = sk::buildAtmosphereLut();
    CHECK(lut.size() == sk::kAtmosphereLutWidth * sk::kAtmosphereLutHeight * 4u);
    CHECK(lut[0] > 0); CHECK(lut[0] < lut[(sk::kAtmosphereLutWidth - 1) * 4]); CHECK(lut[(sk::kAtmosphereLutWidth - 1) * 4] > 200);
    CHECK(lut.back() == 255);
    return t_.finish();
}
