#include "AndroidVulkanSurface.h"
#include "gfx/PresentationRequirements.h"
#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace sk {
namespace {
struct ProbeResources {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    ~ProbeResources() {
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};
bool instanceExtension(const std::vector<VkExtensionProperties>& ext, const char* name) {
    return std::any_of(ext.begin(), ext.end(), [name](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
}
bool deviceHasSwapchain(VkPhysicalDevice device) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
    return instanceExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}
}
std::string probeAndroidVulkanSurface(ANativeWindow* window) {
    if (!window) return "Android surface unavailable";
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return "Vulkan instance extension query failed";
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS ||
        !instanceExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !instanceExtension(extensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) return "Android Vulkan surface extension unavailable";
    uint32_t loader = VK_API_VERSION_1_0;
    auto getVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (getVersion && getVersion(&loader) != VK_SUCCESS) loader = VK_API_VERSION_1_0;
    const uint32_t requested = loader >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_0;
    const char* names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "Sakuga Android surface probe"; app.apiVersion = requested;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app; ci.enabledExtensionCount = 2; ci.ppEnabledExtensionNames = names;
    ProbeResources resources;
    if (vkCreateInstance(&ci, nullptr, &resources.instance) != VK_SUCCESS) return "Vulkan instance creation failed";
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR}; surfaceInfo.window = window;
    if (vkCreateAndroidSurfaceKHR(resources.instance, &surfaceInfo, nullptr, &resources.surface) != VK_SUCCESS) return "Android Vulkan surface creation failed";
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(resources.instance, &n, nullptr) != VK_SUCCESS || n == 0) return "No Vulkan physical devices";
    std::vector<VkPhysicalDevice> devices(n);
    if (vkEnumeratePhysicalDevices(resources.instance, &n, devices.data()) != VK_SUCCESS) return "Vulkan device enumeration failed";
    std::vector<PresentationCandidate> candidates;
    const auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(resources.instance, "vkGetPhysicalDeviceFeatures2"));
    for (VkPhysicalDevice device : devices) {
        PresentationCandidate candidate;
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(device, &props);
        candidate.name = props.deviceName;
        // Both the loader/instance and the device must support the core Vulkan version we request.
        uint32_t effective = std::min(loader, props.apiVersion);
        candidate.apiMajor = VK_VERSION_MAJOR(effective); candidate.apiMinor = VK_VERSION_MINOR(effective);
        candidate.swapchain = deviceHasSwapchain(device);
        uint32_t queueCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount); vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
        for (uint32_t i = 0; i < queueCount; ++i) {
            VkBool32 present = VK_FALSE;
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && queues[i].queueCount &&
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, resources.surface, &present) == VK_SUCCESS && present) {
                candidate.graphicsPresentQueue = static_cast<int>(i); break;
            }
        }
        uint32_t formats = 0;
        candidate.surfaceFormats = vkGetPhysicalDeviceSurfaceFormatsKHR(device, resources.surface, &formats, nullptr) == VK_SUCCESS && formats > 0;
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, resources.surface, &caps) == VK_SUCCESS) {
            candidate.transferDestination = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
            candidate.colorAttachment = (caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
        }
        if (requested >= VK_API_VERSION_1_3 && props.apiVersion >= VK_API_VERSION_1_3 && getFeatures2) {
            VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &features13;
            getFeatures2(device, &features);
            candidate.dynamicRendering = features13.dynamicRendering == VK_TRUE;
            candidate.synchronization2 = features13.synchronization2 == VK_TRUE;
        }
        candidates.push_back(candidate);
    }
    const int chosen = firstPresentationCandidate(candidates);
    const auto& c = candidates[chosen >= 0 ? static_cast<size_t>(chosen) : 0];
    std::ostringstream result;
    if (chosen >= 0) result << "Surface baseline met; renderer not tested";
    else result << presentationRequirementFailure(c);
    result << " | " << c.name << " | Vulkan " << c.apiMajor << '.' << c.apiMinor;
    return result.str();
}
}
