#include "Check.h"
#include "platform/TouchControls.h"
#include <GLFW/glfw3.h>
int main() {
    SK_TEST("touch_controls_test");
    sk::TouchControls t; t.enabled = true; t.setScreen(1000, 500);
    sk::DeviceState d;
    t.injectTouch(7, 100, 300, true); t.apply(d);
    t.injectTouch(7, 150, 280, true);
    t.injectTouch(9, 928, 470, true); // JUMP, concurrent with the stick
    d = {}; t.apply(d);
    CHECK(d.padConnected); CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X] > .1f);
    CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_Y] < -.1f); CHECK(d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    t.injectTouch(11, 600, 220, true); t.apply(d);
    t.injectTouch(11, 630, 200, true); d = {}; t.apply(d);
    CHECK_NEAR(d.mouseDelta.x, 27, 1e-4); CHECK_NEAR(d.mouseDelta.y, -18, 1e-4);
    CHECK(d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    t.injectTouch(9, 928, 470, false); d = {}; t.apply(d); CHECK(!d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X] > .1f);
    t.cancel(); d = {}; t.apply(d); CHECK(!d.padConnected); CHECK(!t.anyActive()); CHECK(!t.buttonHeld(0));
    CHECK_NEAR(t.stick().x, 0, 1e-6); CHECK_NEAR(t.stick().y, 0, 1e-6);
    t.injectTouch(3, 100, 300, true); t.enabled = false; d = {}; t.apply(d); CHECK(!t.anyActive()); CHECK(!d.padConnected);
    return t_.finish();
}
