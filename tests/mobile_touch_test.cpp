#include "Check.h"
#include "platform/MobileTouch.h"
#include <limits>
int main() {
    SK_TEST("mobile_touch_test");
    sk::MobileTouch t;
    using A = sk::MobileTouch::Action;
    auto event = [&](A a, int id, float x, float y) { return t.event(a,id,x,y,1000,500,1); };
    // r=65, stick centre (83,417), button rectangle (852,391)-(982,482)
    auto s = event(A::Down, 7, 83, 417); CHECK_NEAR(s.stickX, 0, 1e-6); CHECK_NEAR(s.stickY, 0, 1e-6);
    s = event(A::Move, 7, 148, 417); CHECK_NEAR(s.stickX, 1, 1e-6); CHECK_NEAR(s.stickY, 0, 1e-6);
    s = event(A::Down, 11, 900, 430); CHECK(s.nextClip); CHECK_NEAR(s.stickX, 1, 1e-6);
    s = event(A::Down, 11, 900, 430); CHECK(!s.nextClip);
    s = event(A::Move, 11, 50, 100); CHECK_NEAR(s.stickX, 1, 1e-6);
    s = event(A::Up, 11, 0, 0); CHECK_NEAR(s.stickX, 1, 1e-6);
    s = event(A::Down, 12, 900, 430); CHECK(s.nextClip);
    s = event(A::Up, 7, 0, 0); CHECK_NEAR(s.stickX, 0, 1e-6); CHECK_NEAR(s.stickY, 0, 1e-6);
    s = event(A::Down, 14, 83, 417); s = event(A::Move, 14, 1000, 1000);
    CHECK_NEAR(std::hypot(s.stickX, s.stickY), 1, 1e-6);
    s = event(A::Down, 15, 100, 417); CHECK(!s.nextClip);
    s = event(A::Up, 15, 0, 0); CHECK_NEAR(std::hypot(s.stickX, s.stickY), 1, 1e-6);
    s = event(A::Cancel, -1, 0, 0); CHECK_NEAR(s.stickX, 0, 1e-6); CHECK_NEAR(s.stickY, 0, 1e-6);
    s = event(A::Down, 12, 900, 430); CHECK(s.nextClip); // cancellation released button ownership too
    event(A::Cancel, -1, 0, 0);
    s = event(A::Down, 20, 750, 430); CHECK(!s.nextClip); // outside visible button
    s = event(A::Down, -1, 83, 417); CHECK_NEAR(s.stickX, 0, 1e-6);
    s = event(A::Down, 3, std::numeric_limits<float>::quiet_NaN(), 417); CHECK_NEAR(s.stickX, 0, 1e-6);
    s = t.event(A::Down, 3, 83, 417, 0, 500, 1); CHECK_NEAR(s.stickX, 0, 1e-6);
    event(A::Down, 3, 83, 417); s = event(A::Move, 3, 83, 352); CHECK_NEAR(s.stickY, -1, 1e-6);
    s = event(A::Move, 3, std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    CHECK_FINITE(s.stickX); CHECK_FINITE(s.stickY); CHECK_NEAR(std::hypot(s.stickX, s.stickY), 1, 1e-6);
    t.reset(); CHECK_NEAR(t.result().stickY, 0, 1e-6);
    return t_.finish();
}
