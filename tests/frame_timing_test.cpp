#include "Check.h"
#include "platform/FrameTiming.h"
#include <limits>
int main() {
    SK_TEST("frame_timing_test");
    sk::FrameTiming t;
    CHECK_NEAR(t.update(1.f / 60.f), 60.f, .001f);
    // A one-second frame is not reported as 20 FPS just because the simulation clamps dt to 0.05s.
    sk::FrameTiming slow;
    CHECK_NEAR(slow.update(1.f), 1.f, 1e-6);
    CHECK_NEAR(slow.update(0.f), 1.f, 1e-6);
    CHECK_NEAR(slow.update(std::numeric_limits<float>::quiet_NaN()), 1.f, 1e-6);
    CHECK(slow.update(.5f) > 1.f);
    return t_.finish();
}
