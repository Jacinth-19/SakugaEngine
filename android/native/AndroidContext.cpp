#include "gfx/Context.h"
#include <android/native_window.h>
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace sk {
namespace {
bool deviceExtension(VkPhysicalDevice dev, const char* name) {
    uint32_t n = 0;
    if (vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> props(n);
    if (vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, props.data()) != VK_SUCCESS) return false;
    return std::any_of(props.begin(), props.end(), [name](const auto& p) { return std::strcmp(p.extensionName, name) == 0; });
}
}
bool Context::initAndroid(ANativeWindow* window, std::string& error) {
    auto fail = [&](const char* message) { error = message; shutdown(); return false; };
    if (!window) return fail("Android surface unavailable");
    uint32_t version = VK_API_VERSION_1_0;
    auto getVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (!getVersion || getVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3) return fail("Vulkan 1.3 is required");
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "Sakuga"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &app; ci.enabledExtensionCount = 2; ci.ppEnabledExtensionNames = exts;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) return fail("Could not create a Vulkan 1.3 instance");
    VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR}; sci.window = window;
    if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &androidSurface) != VK_SUCCESS) return fail("Could not create Android Vulkan surface");
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(instance, &n, nullptr) != VK_SUCCESS || !n) return fail("No Vulkan device");
    std::vector<VkPhysicalDevice> devices(n);
    if (vkEnumeratePhysicalDevices(instance, &n, devices.data()) != VK_SUCCESS) return fail("Cannot enumerate Vulkan devices");
    auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (!getFeatures2) return fail("Cannot query Vulkan 1.3 features");
    const char* reason = "No Vulkan 1.3 device with swapchain support";
    for (auto pd : devices) {
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(pd, &p);
        if (p.apiVersion < VK_API_VERSION_1_3 || !deviceExtension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f13; getFeatures2(pd, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2) { reason = "Vulkan dynamic rendering or synchronization2 unavailable"; continue; }
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, androidSurface, &caps) != VK_SUCCESS ||
            !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            !caps.supportedCompositeAlpha) { reason = "Vulkan surface image usage or composite alpha unsupported"; continue; }
        uint32_t formats = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(pd, androidSurface, &formats, nullptr) != VK_SUCCESS || !formats) { reason = "No Vulkan surface formats"; continue; }
        VkSampleCountFlags samples = p.limits.framebufferColorSampleCounts & p.limits.framebufferDepthSampleCounts;
        // The current renderer allocates a 4096x2048 shadow atlas and a 32 MiB storage buffer.
        if (p.limits.maxImageDimension2D < 4096) { reason = "Vulkan maximum image size is below the renderer's shadow atlas"; continue; }
        if (p.limits.maxStorageBufferRange < 32u * 1024u * 1024u) { reason = "Vulkan storage buffer range is below 32 MiB"; continue; }
        if (p.limits.maxPushConstantsSize < 96) { reason = "Vulkan push constant limit is below 96 bytes"; continue; }
        uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, queues.data());
        for (uint32_t i = 0; i < count; ++i) {
            VkBool32 present = VK_FALSE;
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, androidSurface, &present) == VK_SUCCESS && present) {
                phys = pd; props = p; queueFamily = i;
                msaa = (samples & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : (samples & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
                break;
            }
        }
        if (phys) break;
        reason = "No graphics queue can present to the Android surface";
    }
    if (!phys) { __android_log_print(ANDROID_LOG_WARN, "SakugaPreview", "Vulkan device rejected: %s", reason); return fail(reason); }
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering = VK_TRUE; f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures have{}; vkGetPhysicalDeviceFeatures(phys, &have);
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext = &f13;
    f2.features.samplerAnisotropy = have.samplerAnisotropy; f2.features.fillModeNonSolid = have.fillModeNonSolid;
    float priority = 1.f; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = queueFamily; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    const char* deviceExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext = &f2; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &deviceExt;
    if (vkCreateDevice(phys, &di, nullptr, &device) != VK_SUCCESS) return fail("Could not create Vulkan device");
    beginRenderingFn = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(device, "vkCmdBeginRendering"));
    endRenderingFn = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(device, "vkCmdEndRendering"));
    barrier2Fn = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2"));
    if (!beginRenderingFn || !endRenderingFn || !barrier2Fn) return fail("Vulkan 1.3 device entry points unavailable");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &pi, nullptr, &cmdPool) != VK_SUCCESS) return fail("Could not create Vulkan command pool");
    surfaceCapable = true;
    return true;
}
}
