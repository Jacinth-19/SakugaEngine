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
                         VkShaderModule halation, VkShaderModule composite, VkShaderModule atmosphere, VkShaderModule dof, VkShaderModule finish, VkFormat hdrFormat, VkFormat finalFormat);
    void createTargets(uint32_t width, uint32_t height, VkFormat hdrFormat, VkFormat finalFormat, const Image& scene, const Image& depth, bool depthSupported, const Texture& lut);
    void rebuildSets(const Image& scene, const Image& depth, bool depthSupported, const Texture& lut);
    void destroyTargets();
    void shutdown();
    // scene must be COLOR_ATTACHMENT_OPTIMAL; final is overwritten and left COLOR_ATTACHMENT_OPTIMAL.
    void record(VkCommandBuffer cmd, const Image& scene, const Image& depth, Image& final, const PostSettings& settings,
                bool atmosphereOn, bool dofOn, const glm::mat4& projection, const glm::vec3& fogColor, float fogDensity, float time, int lutSize);
private:
    VkPipeline makePipeline(VkShaderModule fs, VkShaderModule vs, VkFormat format, bool additive);
    Context* ctx_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    Image bloom_[kBloomLevels]{}, halation_{}, atmosphere_{}, dof_{}, display_{}; int bloomLevels_ = 0;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE; VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE; VkSampler sampler_ = VK_NULL_HANDLE, depthSampler_ = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet_ = VK_NULL_HANDLE, bloomSet_[kBloomLevels]{}, compSet_ = VK_NULL_HANDLE, compNoHalationSet_ = VK_NULL_HANDLE;
    VkDescriptorSet atmosphereSet_ = VK_NULL_HANDLE, foggedSceneSet_ = VK_NULL_HANDLE, foggedCompSet_ = VK_NULL_HANDLE, foggedCompNoHalationSet_ = VK_NULL_HANDLE;
    VkDescriptorSet dofSet_ = VK_NULL_HANDLE, fogDofSet_ = VK_NULL_HANDLE, dofSceneSet_ = VK_NULL_HANDLE;
    VkDescriptorSet dofCompSet_ = VK_NULL_HANDLE, dofCompNoHalationSet_ = VK_NULL_HANDLE, finishSet_ = VK_NULL_HANDLE;
    VkPipeline atmospherePipe_ = VK_NULL_HANDLE, dofPipe_ = VK_NULL_HANDLE, finishPipe_ = VK_NULL_HANDLE;
    VkPipeline prefilter_ = VK_NULL_HANDLE, down_ = VK_NULL_HANDLE, up_ = VK_NULL_HANDLE, halationPipe_ = VK_NULL_HANDLE, composite_ = VK_NULL_HANDLE;
};
}
