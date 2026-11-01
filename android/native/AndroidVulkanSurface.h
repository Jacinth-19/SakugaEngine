#pragma once
#include <string>
struct ANativeWindow;
namespace sk {
// Probes a live Android surface and releases all Vulkan resources before returning. Does not render or create a device.
// A positive result only checks the documented baseline, not every renderer format/limit.
std::string probeAndroidVulkanSurface(ANativeWindow* window);
}
