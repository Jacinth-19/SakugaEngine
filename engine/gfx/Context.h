#pragma once
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <string>
struct GLFWwindow;
#ifdef __ANDROID__
struct ANativeWindow;
#endif

namespace sk {

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0, mips = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

// Owns instance / device / queue and exposes small resource helpers.
class Context {
public:
    bool init(bool wantSurface, bool validation);
#ifdef __ANDROID__
    // The Android surface belongs to this context and remains valid until shutdown().
    bool initAndroid(ANativeWindow* window, std::string& error);
    VkSurfaceKHR androidSurface = VK_NULL_HANDLE;
#endif
    void shutdown();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkSampleCountFlagBits msaa = VK_SAMPLE_COUNT_4_BIT;
    bool surfaceCapable = false;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags flags) const;
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& b);
    Image createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, uint32_t mips = 1,
                      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
    void destroyImage(Image& i);
    void immediate(const std::function<void(VkCommandBuffer)>& fn);
    VkShaderModule loadShader(const std::string& spvPath);

    static void barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                        uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS);
};

#define VK_OK(expr) do { VkResult _r = (expr); if (_r != VK_SUCCESS) { std::fprintf(stderr, "[sakuga:error] Vulkan %s failed: %d (%s:%d)\n", #expr, (int)_r, __FILE__, __LINE__); std::abort(); } } while (0)

}  // namespace sk
