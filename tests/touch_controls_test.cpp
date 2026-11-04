#include "Check.h"
#include "platform/TouchControls.h"
#include "platform/TouchLayout.h"
#include <GLFW/glfw3.h>
int main() {
    SK_TEST("touch_controls_test");
    for (auto dims : {glm::vec2(1000,500), glm::vec2(2460,1080), glm::vec2(1280,720)}) {
        for (float scale : {0.6f, 1.f, 2.f}) {
            sk::TouchButtonGeometry b[5]; sk::touchButtonLayout(dims.x, dims.y, scale, b);
            for (int i = 0; i < 5; ++i) {
                float r = b[i].radius * 1.15f;
                CHECK(b[i].center.x - r >= 0 && b[i].center.x + r <= dims.x);
                CHECK(b[i].center.y - r >= 0 && b[i].center.y + r <= dims.y);
                for (int j = 0; j < i; ++j)
                    CHECK(glm::length(b[i].center - b[j].center) >= (b[i].radius + b[j].radius) * 1.15f);
            }
        }
    }
    sk::TouchButtonGeometry buttons[5]; sk::touchButtonLayout(1000, 500, 1, buttons);
    sk::TouchControls t; t.enabled = true; t.setScreen(1000, 500);
    sk::DeviceState d;
    t.injectTouch(7, 100, 370, true); t.apply(d);
    t.injectTouch(7, 150, 350, true);
    t.injectTouch(9, buttons[0].center.x, buttons[0].center.y, true); // JUMP, concurrent with the stick
    d = {}; t.apply(d);
    CHECK(d.padConnected); CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X] > .1f);
    CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_Y] < -.1f); CHECK(d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    t.injectTouch(11, 600, 220, true); t.apply(d);
    t.injectTouch(11, 630, 200, true); d = {}; t.apply(d);
    CHECK_NEAR(d.mouseDelta.x, 27, 1e-4); CHECK_NEAR(d.mouseDelta.y, -18, 1e-4);
    CHECK(d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    t.injectTouch(9, buttons[0].center.x, buttons[0].center.y, false); d = {}; t.apply(d); CHECK(!d.padButton[GLFW_GAMEPAD_BUTTON_A]);
    CHECK(d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X] > .1f);
    t.cancel(); d = {}; t.apply(d); CHECK(!d.padConnected); CHECK(!t.anyActive()); CHECK(!t.buttonHeld(0));
    CHECK_NEAR(t.stick().x, 0, 1e-6); CHECK_NEAR(t.stick().y, 0, 1e-6);
    t.injectTouch(3, 100, 300, true); t.enabled = false; d = {}; t.apply(d); CHECK(!t.anyActive()); CHECK(!d.padConnected);
    return t_.finish();
}
