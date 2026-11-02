#pragma once
// On-screen touch controls: a floating virtual stick (move), a look-drag area and four action buttons + sprint. The output is merged into the
// DeviceState as a *virtual gamepad* (left stick, face buttons, L3) plus a pointer delta for the look drag, so the existing Input System bindings
// (Move / Look / Jump / Attack / Interact / Crouch / Sprint) work unchanged.
//
// Platform note: desktop GLFW exposes no multi-touch events. The controls therefore take touches through injectTouch(id, x, y, down) -- a mobile
// / SDL / browser front end calls that directly -- and the sandbox feeds the mouse in as touch id 0 so the layout can be tried (and tested) with a mouse.
#include "platform/Input.h"
#include <glm/glm.hpp>
#include <vector>

struct ImDrawList;

namespace sk {

class TouchControls {
public:
    bool enabled = false;
    float uiScale = 1.f;

    void setScreen(float w, float h) { w_ = w; h_ = h; }
    void injectTouch(int id, float x, float y, bool down);     // pixels, origin top-left
    void cancel();                                            // release all pointers and virtual controls
    void apply(DeviceState& d);                                // merge into the device state (call once per frame, after injecting)
    void draw(ImDrawList* dl) const;                           // overlay (ImGui foreground list)
    bool anyActive() const { return !touches_.empty(); }
    glm::vec2 stick() const { return stick_; }
    bool buttonHeld(int i) const { return i >= 0 && i < kButtons && btnHeld_[i]; }
    static constexpr int kButtons = 5;                         // jump, attack, interact, crouch, sprint

private:
    struct Touch { int id; glm::vec2 pos, start, last; enum Role { None, Stick, Look, Button } role = None; int button = -1; };
    struct Btn { glm::vec2 c; float r; const char* label; int pad; };
    void layout(Btn out[kButtons]) const;
    float w_ = 1280, h_ = 720;
    std::vector<Touch> touches_;
    glm::vec2 stick_{0}, stickBase_{0}, lookDelta_{0};
    bool btnHeld_[kButtons] = {};
};

}  // namespace sk
