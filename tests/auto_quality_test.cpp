#include "platform/AutoQuality.h"
#include "platform/AdaptiveResolution.h"
#include "Check.h"
#include <limits>
int main() {
    SK_TEST("auto_quality_test");
    using namespace sk;
    CHECK(autoQuality(DeviceKind::Integrated, false) == QualityTier::Performance);
    CHECK(autoQuality(DeviceKind::Discrete, false) == QualityTier::High);
    CHECK(autoQuality(DeviceKind::Unknown, false) == QualityTier::Balanced);
    CHECK(autoQuality(DeviceKind::Discrete, true) == QualityTier::Performance);
    auto p=qualityDefaults(QualityTier::Performance);
    CHECK_NEAR(p.renderScale,.75f,1e-6); CHECK(p.maxSamples==1); CHECK(!p.bloom && !p.pointShadows && !p.integratedSky);
    auto b=qualityDefaults(QualityTier::Balanced); CHECK(b.maxSamples==2);
    auto h=qualityDefaults(QualityTier::High); CHECK(h.maxSamples==4);
    auto e=scaledExtent(1600,900,.75f); CHECK(e.width==1200 && e.height==675);
    e=scaledExtent(1080,2460,.75f); CHECK(e.width==810 && e.height==1845);
    e=scaledExtent(1,1,.5f); CHECK(e.width==1 && e.height==1);
    e=scaledExtent(0,100,.75f); CHECK(e.width==0 && e.height==0);
    CHECK_NEAR(safeRenderScale(-1),.5f,1e-6); CHECK_NEAR(safeRenderScale(5),1.f,1e-6);
    CHECK_NEAR(safeRenderScale(std::numeric_limits<float>::quiet_NaN()),1.f,1e-6);
    CHECK(cappedSampleCount(7,4)==4); CHECK(cappedSampleCount(7,2)==2);
    CHECK(cappedSampleCount(5,2)==1); CHECK(cappedSampleCount(7,1)==1);
    CHECK(cappedSampleCount(4,2)==0);
    AdaptiveResolution controller;
    float scale=.75f;
    for(int i=0;i<100;++i) scale=controller.update(.1f,scale,.75f,true);
    CHECK_NEAR(scale,.75f,1e-6); // fewer than three slow windows
    for(int i=0;i<55;++i) scale=controller.update(.1f,scale,.75f,true);
    CHECK_NEAR(scale,.70f,1e-5);
    for(int i=0;i<1100;++i) scale=controller.update(.02f,scale,.75f,true);
    CHECK_NEAR(scale,.75f,1e-5);
    for(int i=0;i<500;++i) scale=controller.update(.1f,scale,.75f,false);
    CHECK_NEAR(scale,.75f,1e-5);
    for(int i=0;i<100;++i) scale=controller.update(.1f,scale,.75f,true);
    controller.update(3.f,scale,.75f,true); // long pauses discard samples
    for(int i=0;i<100;++i) scale=controller.update(.1f,scale,.75f,true);
    CHECK_NEAR(scale,.75f,1e-5);
    controller.reset(); scale=.75f;
    // Sustained 1 FPS is poor performance, not a pause: it must be able to downscale.
    for(int i=0;i<15;++i) scale=controller.update(1.f,scale,.75f,true);
    CHECK_NEAR(scale,.70f,1e-5);
    controller.reset(); scale=.5f;
    for(int i=0;i<200;++i) scale=controller.update(.1f,scale,.75f,true);
    CHECK_NEAR(scale,.5f,1e-5);
    controller.reset(); scale=.75f;
    for(int i=0;i<1200;++i) scale=controller.update(.02f,scale,.75f,true);
    CHECK_NEAR(scale,.75f,1e-5); // never exceeds the configured ceiling
    return t_.finish();
}
