#include "Check.h"
#include "platform/FrameBenchmark.h"
#include <limits>
int main() {
    SK_TEST("frame_benchmark_test");
    sk::FrameBenchmark b;
    b.start(.5, 1.0); CHECK(b.running());
    b.record(.25); CHECK(b.state() == sk::FrameBenchmark::State::Warmup);
    b.record(.25); CHECK(b.state() == sk::FrameBenchmark::State::Measuring);
    b.record(std::numeric_limits<double>::quiet_NaN()); b.record(0); CHECK_NEAR(b.elapsed(), 0, 1e-9);
    for (int i = 0; i < 50; ++i) b.record(.02);
    CHECK(b.state() == sk::FrameBenchmark::State::Complete);
    auto r = b.result(); CHECK(r.frames == 50); CHECK_NEAR(r.averageFps, 50, 1e-6);
    CHECK_NEAR(r.p95Ms, 20, 1e-6); CHECK_NEAR(r.onePercentLowFps, 50, 1e-6);
    CHECK_NEAR(r.overBudgetPercent, 100, 1e-6);
    b.start(0, 1.19); for (int i=0;i<99;++i) b.record(.01); b.record(.2);
    CHECK(b.state() == sk::FrameBenchmark::State::Complete);
    CHECK_NEAR(b.result().onePercentLowFps, 5, 1e-6);
    CHECK_NEAR(b.result().p95Ms, 10, 1e-6);
    b.start(); b.cancel(); CHECK(b.state() == sk::FrameBenchmark::State::Cancelled);
    b.record(100); CHECK(b.state() == sk::FrameBenchmark::State::Cancelled);
    b.start(0, .1); b.record(.1); CHECK(b.result().frames == 1);
    return t_.finish();
}
