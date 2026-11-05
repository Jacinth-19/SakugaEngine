#pragma once
#include "gfx/Context.h"
#include "gfx/Material.h"
#include "post/PostSettings.h"
#include "post/PostTiming.h"
namespace sk {
// HDR fullscreen graphics passes. Caller owns scene/final images and LUT; this class owns its transient images,
// descriptors, sampler and pipelines. Rebuild targets after waiting for in-flight work on resize/LUT replacement.
struct AerialInputs {
    glm::vec4 viewRotation{0, 0, 0, 1}; // inverse-view quaternion, xyzw
    glm::vec4 sunDirection{0, 1, 0, 0};
    glm::vec4 model{6360, 100, 8, 1.2f};
    glm::vec4 optical{1, 1, 1, 0}; // ozone, Rayleigh/Mie artistic weights
    glm::vec4 sunColorStrength{1, 1, 1, 0};
    float altitudeKm = 0;
};
class PostProcessing {
public:
    static constexpr int kBloomLevels = 6;
    void init(Context& ctx);
    void createPipelines(VkShaderModule vs, VkShaderModule prefilter, VkShaderModule down, VkShaderModule up,
                         VkShaderModule halation, VkShaderModule composite, VkShaderModule atmosphere, VkShaderModule dof, VkShaderModule finish, VkFormat hdrFormat, VkFormat finalFormat);
    void createTargets(uint32_t width, uint32_t height, VkFormat hdrFormat, VkFormat finalFormat, const Image& scene, const Image& depth, bool depthSupported, const Image& mask, bool maskSupported, const Texture& lut);
    void rebuildSets(const Image& scene, const Image& depth, bool depthSupported, const Image& mask, bool maskSupported, const Texture& lut);
    // Update after targets or the transmittance LUT are recreated, with all frames idle.
    void setSkyTransmittance(const Texture& texture);
    void destroyTargets();
    void shutdown();
    // scene must be COLOR_ATTACHMENT_OPTIMAL; final is overwritten and left COLOR_ATTACHMENT_OPTIMAL.
    PostTimingActive record(VkCommandBuffer cmd, const Image& scene, const Image& depth, const Image& mask, bool maskSupported, Image& final, const PostSettings& settings,
                bool atmosphereOn, bool dofOn, const glm::mat4& projection, const glm::vec3& fogColor, float fogDensity, float time, int lutSize, const AerialInputs& aerial, VkQueryPool detailPool, uint32_t detailQueryBase);
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
