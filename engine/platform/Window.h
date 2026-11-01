#pragma once
#include "platform/Input.h"
#include <string>
#include <vector>
struct GLFWwindow;
namespace sk {
class Window {
public:
    bool create(int w, int h, const std::string& title);
    void destroy();
    bool shouldClose() const;
    void poll();                            // pumps events and refreshes device()
    void setCursorCaptured(bool on);
    bool cursorCaptured() const { return captured_; }
    GLFWwindow* handle() const { return win_; }
    void framebufferSize(int& w, int& h) const;
    const DeviceState& device() const { return dev_; }
    bool focused() const;
    // Gamepads: all joysticks 0..15 are polled and merged (buttons OR-ed, axes by largest magnitude), hot-plug is handled via the GLFW
    // joystick callback. Extra SDL_GameControllerDB-style mappings (for pads GLFW does not know) are read from a text file.
    int loadGamepadMappings(const std::string& path);
    const std::vector<std::string>& padNames() const { return padNames_; }
private:
    GLFWwindow* win_ = nullptr;
    DeviceState dev_;
    std::vector<std::string> padNames_;
    bool captured_ = false, havePrev_ = false;
    glm::vec2 prevPos_{0};
    friend void scrollCb(GLFWwindow*, double, double);
};
}
