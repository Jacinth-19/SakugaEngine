#include "Check.h"
#include "sky/SkyProfileStore.h"
int main() {
    SK_TEST("sky_profile_store_test");
    sk::EditableSkyProfiles original{};
    original[0].atmosphere = 1.2345f; original[0].zenithTint = {0.2f, 1.1f, 1.8f};
    original[1].cloudCover = .18f; original[2].wind = 4.25f;
    original[3].model.planetRadiusKm = 2500.f; original[3].model.ozone = 2.25f; original[3].model.groundAlbedo = .6f;
    original[3].volumetricClouds = true; original[3].integratedScattering = false;
    sk::EditableSkyProfiles restored{};
    CHECK(sk::skyProfilesFromJson(sk::skyProfilesToJson(original), &restored));
    CHECK_NEAR(restored[0].atmosphere, original[0].atmosphere, 1e-6);
    CHECK_NEAR(restored[0].zenithTint.z, original[0].zenithTint.z, 1e-6);
    CHECK_NEAR(restored[1].cloudCover, .18, 1e-6); CHECK_NEAR(restored[2].wind, 4.25, 1e-6);
    CHECK_NEAR(restored[3].model.planetRadiusKm, 2500, 1e-6); CHECK_NEAR(restored[3].model.ozone, 2.25, 1e-6); CHECK_NEAR(restored[3].model.groundAlbedo, .6, 1e-6);
    CHECK(restored[3].volumetricClouds); CHECK(!restored[3].integratedScattering);
    CHECK(!sk::skyProfilesFromJson("{\"version\":1,\"profiles\":[]}", &restored));
    CHECK_NEAR(restored[0].atmosphere, original[0].atmosphere, 1e-6);
    CHECK(!sk::skyProfilesFromJson("{broken", &restored));
    CHECK(!sk::skyProfilesFromJson(sk::skyProfilesToJson(original), nullptr));
    return t_.finish();
}
