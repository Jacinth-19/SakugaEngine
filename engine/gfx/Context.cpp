#include "gfx/Context.h"
#include "core/Log.h"
#include "core/FileUtil.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <algorithm>

namespace sk {
static VKAPI_ATTR VkBool32 VKAPI_CALL dbgCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) SK_WARN("vk: %s", d->pMessage);
    return VK_FALSE;
}

bool Context::init(bool wantSurface, bool validation) {
    std::vector<const char*> exts, layers;
    if (wantSurface) {
        uint32_t n = 0; const char** g = glfwGetRequiredInstanceExtensions(&n);
        if (!g) { SK_ERR("GLFW reports no Vulkan surface support"); return false; }
        for (uint32_t i = 0; i < n; ++i) exts.push_back(g[i]);
    }
    if (validation) {
        uint32_t lc = 0; vkEnumerateInstanceLayerProperties(&lc, nullptr);
        std::vector<VkLayerProperties> lp(lc); vkEnumerateInstanceLayerProperties(&lc, lp.data());
        for (auto& l : lp) if (!std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty()) { SK_WARN("validation requested but VK_LAYER_KHRONOS_validation not installed"); validation = false; }
        else exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "Sakuga"; ai.pEngineName = "Sakuga Engine"; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = (uint32_t)exts.size(); ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (uint32_t)layers.size(); ci.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) { SK_ERR("vkCreateInstance failed (Vulkan 1.3 loader/driver needed)"); return false; }
    if (validation) {
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = dbgCb;
        if (fn) fn(instance, &mi, nullptr, &messenger);
    }

    uint32_t pc = 0; vkEnumeratePhysicalDevices(instance, &pc, nullptr);
    if (!pc) { SK_ERR("no Vulkan devices"); return false; }
    std::vector<VkPhysicalDevice> pds(pc); vkEnumeratePhysicalDevices(instance, &pc, pds.data());
    int bestScore = -1;
    for (auto pd : pds) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > bestScore) { bestScore = score; phys = pd; props = p; }
    }
    if (!phys) { SK_ERR("no Vulkan 1.3 capable device"); return false; }

    uint32_t qc = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qc); vkGetPhysicalDeviceQueueFamilyProperties(phys, &qc, qp.data());
    bool found = false;
    for (uint32_t i = 0; i < qc; ++i) {
        bool gfx = qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
        bool present = !wantSurface || glfwGetPhysicalDevicePresentationSupport(instance, phys, i);
        if (gfx && present) { queueFamily = i; found = true; break; }
    }
    if (!found) { SK_ERR("no suitable queue family"); return false; }

    VkSampleCountFlags sc = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    msaa = (sc & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : (sc & VK_SAMPLE_COUNT_2_BIT) ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE; f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    f2.features.samplerAnisotropy = VK_TRUE;
    f2.features.fillModeNonSolid = VK_TRUE;
    VkPhysicalDeviceFeatures have; vkGetPhysicalDeviceFeatures(phys, &have);
    f2.features.samplerAnisotropy = have.samplerAnisotropy; f2.features.fillModeNonSolid = have.fillModeNonSolid;
    std::vector<const char*> dexts;
    if (wantSurface) dexts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)dexts.size(); dci.ppEnabledExtensionNames = dexts.data();
    if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS) { SK_ERR("vkCreateDevice failed"); return false; }
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queueFamily;
    VK_OK(vkCreateCommandPool(device, &pci, nullptr, &cmdPool));
    surfaceCapable = wantSurface;
    SK_LOG("GPU: %s | Vulkan %u.%u.%u | MSAA x%d", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
           VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion), (int)msaa);
    return true;
}

void Context::shutdown() {
    if (device) vkDeviceWaitIdle(device);
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (messenger) { auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"); if (fn) fn(instance, messenger, nullptr); }
    if (instance) vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE;
}

uint32_t Context::findMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    SK_FATAL("no suitable memory type");
    return 0;
}

Buffer Context::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer b; b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_OK(vkCreateBuffer(device, &ci, nullptr, &b.buffer));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(device, b.buffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_OK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
    VK_OK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    if (hostVisible) VK_OK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    return b;
}
void Context::destroyBuffer(Buffer& b) {
    if (b.mapped) vkUnmapMemory(device, b.memory);
    if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device, b.memory, nullptr);
    b = {};
}

Image Context::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, uint32_t mips, VkSampleCountFlagBits samples, VkImageAspectFlags aspect) {
    Image im; im.width = w; im.height = h; im.format = fmt; im.mips = mips; im.samples = samples; im.aspect = aspect;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w, h, 1}; ci.mipLevels = mips; ci.arrayLayers = 1;
    ci.samples = samples; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_OK(vkCreateImage(device, &ci, nullptr, &im.image));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, im.image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_OK(vkAllocateMemory(device, &ai, nullptr, &im.memory));
    VK_OK(vkBindImageMemory(device, im.image, im.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange = {aspect, 0, mips, 0, 1};
    VK_OK(vkCreateImageView(device, &vi, nullptr, &im.view));
    return im;
}
void Context::destroyImage(Image& i) {
    if (i.view) vkDestroyImageView(device, i.view, nullptr);
    if (i.image) vkDestroyImage(device, i.image, nullptr);
    if (i.memory) vkFreeMemory(device, i.memory, nullptr);
    i = {};
}

void Context::immediate(const std::function<void(VkCommandBuffer)>& fn) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cmd; VK_OK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_OK(vkBeginCommandBuffer(cmd, &bi));
    fn(cmd);
    VK_OK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    VK_OK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_OK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

VkShaderModule Context::loadShader(const std::string& path) {
    std::vector<uint8_t> code;
    if (!readFile(path, code)) SK_FATAL("cannot read shader %s (was the project built?)", path.c_str());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size(); ci.pCode = (const uint32_t*)code.data();
    VkShaderModule m; VK_OK(vkCreateShaderModule(device, &ci, nullptr, &m));
    return m;
}

void Context::barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, uint32_t baseMip, uint32_t mipCount) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = from; b.newLayout = to; b.image = img;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {aspect, baseMip, mipCount, 0, 1};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1; di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}
}  // namespace sk
