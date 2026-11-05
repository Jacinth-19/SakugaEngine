#include "Check.h"
#include "post/TimestampMath.h"
#include "post/PostTiming.h"
#include <cstdint>
int main() {
    SK_TEST("timestamp_math_test");
    CHECK_NEAR(sk::timestampMilliseconds(100, 1100, 64, 2.0), .002, 1e-10);
    CHECK_NEAR(sk::timestampMilliseconds(250, 5, 8, 1000.0), .011, 1e-10);
    CHECK_NEAR(sk::timestampMilliseconds(UINT64_MAX - 4, 5, 64, 1000.0), .010, 1e-10);
    CHECK(sk::timestampMilliseconds(0, 1, 0, 1.0) < 0);
    CHECK(sk::timestampMilliseconds(0, 1, 65, 1.0) < 0);
    CHECK(sk::timestampMilliseconds(0, 1, 64, 0.0) < 0);
    sk::PostSettings settings; settings.bloom = false;
    auto active = sk::postTimingPlan(false, false, sk::planPost(settings, 128, 128), 6);
    CHECK(!active[static_cast<unsigned>(sk::PostTimingStage::Prefilter)]);
    CHECK(active[static_cast<unsigned>(sk::PostTimingStage::Composite)]);
    CHECK(!active[static_cast<unsigned>(sk::PostTimingStage::Finish)]);
    settings.bloom = true; settings.halation = .5f; settings.ink = .2f;
    active = sk::postTimingPlan(true, true, sk::planPost(settings, 128, 128), 6);
    for (bool enabled : active) CHECK(enabled);
    active = sk::postTimingPlan(false, false, sk::planPost(settings, 1, 1), 1);
    CHECK(!active[static_cast<unsigned>(sk::PostTimingStage::Downsample)]);
    CHECK(!active[static_cast<unsigned>(sk::PostTimingStage::Upsample)]);
    CHECK(sk::kFrameTimingQueries == 20);
    return t_.finish();
}
