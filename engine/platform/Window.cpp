#include "platform/Window.h"
#include "core/Log.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sk {
static glm::vec2 g_scroll{0};
void scrollCb(GLFWwindow*, double x, double y) { g_scroll += glm::vec2((float)x, (float)y); }

bool Window::create(int w, int h, const std::string& title) {
    if (!glfwInit()) { SK_ERR("glfwInit failed"); return false; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    win_ = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
    if (!win_) { SK_ERR("glfwCreateWindow failed"); return false; }
    glfwSetScrollCallback(win_, scrollCb);
    glfwSetJoystickCallback([](int jid, int ev) {
        if (ev == GLFW_CONNECTED) {
            if (glfwJoystickIsGamepad(jid)) SK_LOG("gamepad %d connected: %s", jid, glfwGetGamepadName(jid));
            else SK_WARN("joystick %d (%s) connected but has no gamepad mapping; add its SDL GUID line to config/gamecontrollerdb.txt", jid, glfwGetJoystickName(jid));
        } else if (ev == GLFW_DISCONNECTED) SK_LOG("joystick %d disconnected", jid);
    });
    for (int j = 0; j <= GLFW_JOYSTICK_LAST; ++j) if (glfwJoystickPresent(j) && !glfwJoystickIsGamepad(j)) SK_WARN("joystick %d (%s) has no gamepad mapping; add it to config/gamecontrollerdb.txt", j, glfwGetJoystickName(j));
    return true;
}
int Window::loadGamepadMappings(const std::string& path) {
    std::ifstream f(path); if (!f) return 0;
    std::stringstream ss; ss << f.rdbuf();
    if (!glfwUpdateGamepadMappings(ss.str().c_str())) { SK_WARN("some lines of %s were rejected by GLFW", path.c_str()); }
    int n = 0; std::string line; std::stringstream is(ss.str()); while (std::getline(is, line)) if (!line.empty() && line[0] != '#') ++n;
    SK_LOG("gamepad mappings: %d entries from %s", n, path.c_str());
    return n;
}
void Window::destroy() { if (win_) glfwDestroyWindow(win_); win_ = nullptr; glfwTerminate(); }
bool Window::shouldClose() const { return win_ && glfwWindowShouldClose(win_); }
bool Window::focused() const { return win_ && glfwGetWindowAttrib(win_, GLFW_FOCUSED); }
void Window::framebufferSize(int& w, int& h) const { glfwGetFramebufferSize(win_, &w, &h); }

void Window::setCursorCaptured(bool on) {
    captured_ = on;
    glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    havePrev_ = false;
}

void Window::poll() {
    g_scroll = {0, 0};
    glfwPollEvents();
    DeviceState d;
    for (int k = GLFW_KEY_SPACE; k < DeviceState::kKeys && k <= GLFW_KEY_LAST; ++k) d.key[k] = glfwGetKey(win_, k) == GLFW_PRESS;
    for (int b = 0; b < 8; ++b) d.mouseButton[b] = glfwGetMouseButton(win_, b) == GLFW_PRESS;
    double mx, my; glfwGetCursorPos(win_, &mx, &my);
    d.mousePos = {(float)mx, (float)my};
    d.mouseDelta = havePrev_ ? d.mousePos - prevPos_ : glm::vec2(0);
    prevPos_ = d.mousePos; havePrev_ = true;
    d.scroll = g_scroll;
    padNames_.clear();
    for (int jid = 0; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        GLFWgamepadstate gp;
        if (!glfwJoystickPresent(jid) || !glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &gp)) continue;
        d.padConnected = true; ++d.padCount; padNames_.push_back(glfwGetGamepadName(jid));
        for (int i = 0; i < 15; ++i) d.padButton[i] = d.padButton[i] || gp.buttons[i];
        for (int i = 0; i < 6; ++i) {
            bool trigger = i == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER || i == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
            float cur = d.padCount == 1 ? (trigger ? -1.f : 0.f) : d.padAxis[i];
            if (d.padCount == 1) cur = trigger ? -1.f : 0.f;
            if (trigger ? gp.axes[i] > cur : std::fabs(gp.axes[i]) > std::fabs(cur)) d.padAxis[i] = gp.axes[i]; else d.padAxis[i] = cur;
        }
    }
    if (d.padConnected && d.padAxis[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.4f) d.padButton[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = true;   // RT also sprints
    dev_ = d;
}
}
