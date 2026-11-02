// Android's Activity owns the surface and input. These stubs satisfy desktop-only Window references in sandbox UI code;
// the Android frame loop never calls the GLFW window loop.
#include "platform/Window.h"
namespace sk {
bool Window::create(int, int, const std::string&) { return false; }
void Window::destroy() {}
bool Window::shouldClose() const { return false; }
void Window::poll() {}
void Window::setCursorCaptured(bool on) { captured_ = on; }
void Window::framebufferSize(int& w, int& h) const { w = h = 0; }
bool Window::focused() const { return true; }
int Window::loadGamepadMappings(const std::string&) { return 0; }
}
