#include "platform/TouchControls.h"
#include "platform/TouchLayout.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace sk {

void TouchControls::layout(Btn b[kButtons]) const {
    TouchButtonGeometry g[kButtons]; touchButtonLayout(w_, h_, uiScale, g);
    const char* labels[kButtons] = {"JUMP", "ATK", "USE", "DUCK", "RUN"};
    const int pads[kButtons] = {GLFW_GAMEPAD_BUTTON_A, GLFW_GAMEPAD_BUTTON_X, GLFW_GAMEPAD_BUTTON_Y,
                                GLFW_GAMEPAD_BUTTON_B, GLFW_GAMEPAD_BUTTON_LEFT_THUMB};
    for (int i = 0; i < kButtons; ++i) b[i] = {g[i].center, g[i].radius, labels[i], pads[i]};
}

void TouchControls::injectTouch(int id, float x, float y, bool down) {
    auto it = std::find_if(touches_.begin(), touches_.end(), [&](const Touch& t) { return t.id == id; });
    if (!down) { if (it != touches_.end()) touches_.erase(it); return; }
    if (it != touches_.end()) { it->pos = {x, y}; return; }
    Touch t; t.id = id; t.pos = t.start = t.last = {x, y};
    Btn b[kButtons]; layout(b);
    for (int i = 0; i < kButtons; ++i) if (glm::length(t.pos - b[i].c) < b[i].r * 1.15f) { t.role = Touch::Button; t.button = i; break; }
    if (t.role == Touch::None) {
        bool haveStick = false; for (auto& o : touches_) haveStick |= o.role == Touch::Stick;
        if (t.pos.x < w_ * 0.42f && !haveStick) { t.role = Touch::Stick; stickBase_ = t.pos; }
        else if (t.pos.x >= w_ * 0.42f) t.role = Touch::Look;
    }
    touches_.push_back(t);
}

void TouchControls::cancel() {
    touches_.clear(); stick_ = lookDelta_ = glm::vec2(0);
    for (bool& held : btnHeld_) held = false;
}

void TouchControls::apply(DeviceState& d) {
    stick_ = glm::vec2(0); lookDelta_ = glm::vec2(0);
    for (bool& h : btnHeld_) h = false;
    if (!enabled) { cancel(); return; }
    float radius = 90.f * std::min(w_, h_) * 0.0014f * uiScale;
    for (auto& t : touches_) {
        if (t.role == Touch::Stick) { glm::vec2 v = (t.pos - stickBase_) / radius; float l = glm::length(v); if (l > 1.f) { stickBase_ += (v / l) * (l - 1.f) * radius; v /= l; } stick_ = glm::vec2(v.x, v.y); }
        else if (t.role == Touch::Look) { lookDelta_ += t.pos - t.last; t.last = t.pos; }
        else if (t.role == Touch::Button && t.button >= 0) btnHeld_[t.button] = true;
    }
    if (touches_.empty()) return;
    d.padConnected = true; d.padCount = std::max(d.padCount, 1);
    if (glm::length(stick_) > 0.01f) {                       // Y of the pad axes points down, like on a real stick
        d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X] = stick_.x * 1.0f; d.padAxis[GLFW_GAMEPAD_AXIS_LEFT_Y] = stick_.y;
    }
    Btn b[kButtons]; layout(b);
    for (int i = 0; i < kButtons; ++i) if (btnHeld_[i]) d.padButton[b[i].pad] = true;
    d.mouseDelta += lookDelta_ * 0.9f;
}

void TouchControls::draw(ImDrawList* dl) const {
    if (!enabled) return;
    Btn b[kButtons]; layout(b);
    float radius = 90.f * std::min(w_, h_) * 0.0014f * uiScale;
    bool stickOn = false; for (auto& t : touches_) stickOn |= t.role == Touch::Stick;
    glm::vec2 base = stickOn ? stickBase_ : glm::vec2(radius * 1.8f, h_ - radius * 1.8f);
    dl->AddCircleFilled(ImVec2(base.x, base.y), radius, IM_COL32(255, 255, 255, 36), 40); dl->AddCircle(ImVec2(base.x, base.y), radius, IM_COL32(255, 255, 255, 110), 40, 2.f);
    glm::vec2 k = base + stick_ * radius;
    dl->AddCircleFilled(ImVec2(k.x, k.y), radius * 0.42f, IM_COL32(255, 255, 255, stickOn ? 150 : 70), 32);
    for (int i = 0; i < kButtons; ++i) {
        bool on = btnHeld_[i];
        dl->AddCircleFilled(ImVec2(b[i].c.x, b[i].c.y), b[i].r, on ? IM_COL32(61, 187, 216, 210) : IM_COL32(12, 23, 38, 160), 32);
        dl->AddCircle(ImVec2(b[i].c.x, b[i].c.y), b[i].r, on ? IM_COL32(214, 251, 255, 240) : IM_COL32(142, 224, 244, 180), 32, 2.f);
        ImVec2 ts = ImGui::CalcTextSize(b[i].label);
        dl->AddText(ImVec2(b[i].c.x - ts.x * 0.5f, b[i].c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 200), b[i].label);
    }
}

}  // namespace sk
