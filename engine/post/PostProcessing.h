#pragma once
#include "gfx/Context.h"
#include "gfx/Material.h"
#include "post/PostSettings.h"
namespace sk {
// HDR fullscreen graphics passes. Caller owns scene/final images and LUT; this class owns its transient images,
// descriptors, sampler and pipelines. Rebuild targets after waiting for in-flight work on resize/LUT replacement.
class PostProcessing {
public:
    static constexpr int kBloomLevels = 6;
    void init(Context& ctx);
    void createPipelines(VkShaderModule vs, VkShaderModule prefilter, VkShaderModule down, VkShaderModule up,
                         VkShaderModule halation, VkShaderModule composite, VkFormat hdrFormat, VkFormat finalFormat);
    void createTargets(uint32_t width, uint32_t height, VkFormat hdrFormat, const Image& scene, const Texture& lut);
    void rebuildSets(const Image& scene, const Texture& lut);
    void destroyTargets();
    void shutdown();
    // scene must be COLOR_ATTACHMENT_OPTIMAL; final is overwritten and left COLOR_ATTACHMENT_OPTIMAL.
    void record(VkCommandBuffer cmd, const Image& scene, Image& final, const PostSettings& settings, float time, int lutSize);
private:
    VkPipeline makePipeline(VkShaderModule fs, VkShaderModule vs, VkFormat format, bool additive);
    Context* ctx_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    Image bloom_[kBloomLevels]{}, halation_{}; int bloomLevels_ = 0;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE; VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE; VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet_ = VK_NULL_HANDLE, bloomSet_[kBloomLevels]{}, compSet_ = VK_NULL_HANDLE, compNoHalationSet_ = VK_NULL_HANDLE;
    VkPipeline prefilter_ = VK_NULL_HANDLE, down_ = VK_NULL_HANDLE, up_ = VK_NULL_HANDLE, halationPipe_ = VK_NULL_HANDLE, composite_ = VK_NULL_HANDLE;
};
}
