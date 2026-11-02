#include "gfx/Renderer.h"
#include "gfx/ShadowFit.h"
#include "gfx/SurfaceChoices.h"
#include "core/Log.h"
#ifndef __ANDROID__
#include <GLFW/glfw3.h>
#else
#include <android/native_window.h>
#include <android/log.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#ifndef __ANDROID__
#include <backends/imgui_impl_glfw.h>
#endif
#include <backends/imgui_impl_vulkan.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>

namespace sk {

namespace {
uint16_t floatToHalf(float f) {
    f = std::clamp(f, 0.f, 65000.f);
    uint32_t x; std::memcpy(&x, &f, 4);
    uint32_t e = (x >> 23) & 0xFF, m = x & 0x7FFFFF;
    if (e < 113) return 0;
    return (uint16_t)(((e - 112) << 10) | (m >> 13));
}
glm::vec3 equirectDir(float u, float v) {          // must match envDir/equirectUV in common.glsl
    float th = v * glm::pi<float>(), ph = (u - 0.5f) * 2.f * glm::pi<float>();
    return {std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
}
}

namespace {
struct GlobalUBO {
    glm::mat4 viewProj, view, proj, lightViewProj;
    glm::vec4 cameraPos, screenParams, lightDir, lightColor, skyColor, groundColor, fogColor, misc, split;
    glm::vec4 sh[9];            // diffuse SH (cosine-convolved, divided by pi): rgb
    glm::vec4 envParams;        // x intensity, y max LOD, z hdr sky (0/1), w rotation
    glm::vec4 pointPos[kMaxPointLights];   // xyz, w = range
    glm::vec4 pointCol[kMaxPointLights];   // rgb * intensity
    glm::vec4 faceForward, faceRight;
    glm::vec4 skyZenith, skyHorizon, skyParams, style;   // procedural sky (cloud cover, time, stars, sun disc) / style (x = pure NPR amount)
    glm::mat4 lightViewProj1;   // far shadow cascade (atlas x range 0.5..1)
    glm::vec4 shadowCascade;    // x near radius, y far radius, z unused, w cascade count
    glm::vec4 contactPos[kMaxContacts];   // xyz = ground contact point of a character, w = radius (0 = off)
    glm::mat4 pointViewProj[12];          // 2 shadow-casting point lights x 6 cube faces
    glm::vec4 pointShadow[4];             // per point light: x = shadow slot (-1 = none), y = normal offset, z = depth bias
};
struct PushConstants { glm::mat4 model; uint32_t boneOffset, skinned, pad0, pad1; uint32_t morphWeightOffset, morphCount, morphBase, vertexCount; };
static_assert(sizeof(PushConstants) == 96);
struct PostPC { glm::vec4 a, b, c, d, e; };
const VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
}

glm::mat4 Renderer::perspective(float fovY, float aspect, float zn, float zf) {
    glm::mat4 p = glm::perspective(fovY, aspect, zn, zf);
    p[1][1] *= -1.f;   // Vulkan clip space has +Y down
    return p;
}

bool Renderer::savePNG(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
    return stbi_write_png(path.c_str(), (int)w, (int)h, 4, rgba.data(), (int)w * 4) != 0;
}

bool Renderer::presenting() const {
#ifdef __ANDROID__
    return androidWindow_ != nullptr;
#else
    return window_ != nullptr;
#endif
}
void Renderer::framebufferSize(int& w, int& h) const {
#ifdef __ANDROID__
    w = ANativeWindow_getWidth(androidWindow_); h = ANativeWindow_getHeight(androidWindow_);
#else
    glfwGetFramebufferSize(window_, &w, &h);
#endif
}
#ifdef __ANDROID__
bool Renderer::initAndroid(Context& ctx, ANativeWindow* window, uint32_t w, uint32_t h, const std::string& shaderDir) {
    if (!window || !ctx.androidSurface) return false;
    androidWindow_ = window; surface_ = ctx.androidSurface;
    return init(ctx, nullptr, w, h, shaderDir);
}
#endif

// ===================================================================================== init
bool Renderer::init(Context& ctx, GLFWwindow* window, uint32_t w, uint32_t h, const std::string& shaderDir) {
    ctx_ = &ctx; window_ = window; width_ = w; height_ = h;
    VkDevice dev = ctx.device;

    if (presenting()) createSwapchain();

    // --- descriptor layouts
    {
        VkDescriptorSetLayoutBinding g[7] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 7; ci.pBindings = g;
        VK_OK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &globalLayout_));
        VkDescriptorSetLayoutBinding m[1 + TEX_COUNT];
        m[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for (int i = 0; i < TEX_COUNT; ++i) m[1 + i] = {(uint32_t)(1 + i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        ci.bindingCount = 1 + TEX_COUNT; ci.pBindings = m;
        VK_OK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &materialLayout_));
        VkDescriptorSetLayout layouts[2] = {globalLayout_, materialLayout_};
        VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 2; pl.pSetLayouts = layouts; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pcr;
        VK_OK(vkCreatePipelineLayout(dev, &pl, nullptr, &pipeLayout_));
        VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 + kFramesInFlight},
                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 * TEX_COUNT + 8},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * kFramesInFlight}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1024 + kFramesInFlight; pi.poolSizeCount = 3; pi.pPoolSizes = ps;
        VK_OK(vkCreateDescriptorPool(dev, &pi, nullptr, &pool_));
    }

    // --- post-processing resources
    {
        VkDescriptorSetLayoutBinding pb[3] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; ci.bindingCount = 3; ci.pBindings = pb;
        VK_OK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &postSetLayout_));
        VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPC)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1; pl.pSetLayouts = &postSetLayout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pcr;
        VK_OK(vkCreatePipelineLayout(dev, &pl, nullptr, &postLayout_));
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * (kBloomLevels + 3)};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = kBloomLevels + 3; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VK_OK(vkCreateDescriptorPool(dev, &pi, nullptr, &postPool_));
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_LINEAR; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_OK(vkCreateSampler(dev, &sc, nullptr, &postSampler_));
    }

    // --- samplers / targets
    {
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_LINEAR; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; sc.compareEnable = VK_TRUE; sc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VK_OK(vkCreateSampler(dev, &sc, nullptr, &shadowSampler_));
        VkSamplerCreateInfo ec{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        ec.magFilter = ec.minFilter = VK_FILTER_LINEAR; ec.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; ec.maxLod = 16.f;
        ec.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; ec.addressModeV = ec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_OK(vkCreateSampler(dev, &ec, nullptr, &envSampler_));
        shadow_ = ctx.createImage(kShadowRes * 2, kShadowRes, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1,
                                  VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        pointShadow_ = ctx.createImage(kPointShadowRes * 6, kPointShadowRes * kMaxPointShadows, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1,
                                       VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    createTargets();

    morphDeltas_ = ctx.createBuffer(sizeof(glm::vec4) * kMaxMorphVec4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    std::memset(morphDeltas_.mapped, 0, 64);

    // --- frames
    for (auto& f : frames_) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = ctx.cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VK_OK(vkAllocateCommandBuffers(dev, &ai, &f.cmd));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_OK(vkCreateFence(dev, &fi, nullptr, &f.fence));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_OK(vkCreateSemaphore(dev, &si, nullptr, &f.imageAvailable));
        f.globalUBO = ctx.createBuffer(sizeof(GlobalUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        f.bones = ctx.createBuffer(sizeof(glm::mat4) * kMaxBones, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        f.lines = ctx.createBuffer(sizeof(LineVertex) * kMaxLineVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        f.morphWeights = ctx.createBuffer(sizeof(float) * kMaxMorphWeights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = pool_; da.descriptorSetCount = 1; da.pSetLayouts = &globalLayout_;
        VK_OK(vkAllocateDescriptorSets(dev, &da, &f.globalSet));
        VkDescriptorBufferInfo b0{f.globalUBO.buffer, 0, sizeof(GlobalUBO)}, b1{f.bones.buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo ii{shadowSampler_, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo b4{morphDeltas_.buffer, 0, VK_WHOLE_SIZE}, b5{f.morphWeights.buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo ps{shadowSampler_, pointShadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr[6]{};
        wr[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 6, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ps, nullptr, nullptr};
        wr[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &b0, nullptr};
        wr[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &b1, nullptr};
        wr[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr};
        wr[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &b4, nullptr};
        wr[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &b5, nullptr};
        vkUpdateDescriptorSets(dev, 6, wr, 0, nullptr);
    }

    createPipelines(shaderDir);

    // --- default textures
    const uint8_t w4[4] = {255, 255, 255, 255}, b4[4] = {0, 0, 0, 255}, n4[4] = {128, 128, 255, 255};
    white_ = createTexture("white", w4, 1, 1, false, false);
    black_ = createTexture("black", b4, 1, 1, false, false);
    bump_ = createTexture("bump", n4, 1, 1, false, false);
    updateProceduralEnvironment(LightingSettings{});
    return true;
}

// ===================================================================================== swapchain / targets
void Renderer::createSwapchain() {
    VkDevice dev = ctx_->device;
#ifndef __ANDROID__
    if (!surface_) VK_OK(glfwCreateWindowSurface(ctx_->instance, window_, nullptr, &surface_));
#endif
    VkSurfaceCapabilitiesKHR caps; VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->phys, surface_, &caps));
    uint32_t fc = 0; VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->phys, surface_, &fc, nullptr));
    if (!fc) SK_FATAL("no Vulkan surface formats");
    std::vector<VkSurfaceFormatKHR> fmts(fc); VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->phys, surface_, &fc, fmts.data()));
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = f; break; }
    swapFormat_ = chosen.format;
    int fw, fh; framebufferSize(fw, fh);
    surfaceWidth_ = fw; surfaceHeight_ = fh;
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) ext = {(uint32_t)fw, (uint32_t)fh};
    ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    width_ = ext.width; height_ = ext.height;
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_; ci.minImageCount = count; ci.imageFormat = chosen.format; ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = ext; ci.imageArrayLayers = 1; ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = static_cast<VkSurfaceTransformFlagBitsKHR>(preferredSurfaceTransform(caps.supportedTransforms, caps.currentTransform));
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "surface dimensions window=%dx%d swap=%ux%u transform=%u supported=%u selected=%u", fw, fh, ext.width, ext.height, caps.currentTransform, caps.supportedTransforms, ci.preTransform);
#endif
    const uint32_t alpha = preferredCompositeAlpha(caps.supportedCompositeAlpha);
    if (!alpha) SK_FATAL("no supported Vulkan composite alpha mode");
    ci.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(alpha);
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; ci.clipped = VK_TRUE;
    VK_OK(vkCreateSwapchainKHR(dev, &ci, nullptr, &swapchain_));
    uint32_t n = 0; vkGetSwapchainImagesKHR(dev, swapchain_, &n, nullptr);
    swapImages_.resize(n); vkGetSwapchainImagesKHR(dev, swapchain_, &n, swapImages_.data());
    renderDone_.resize(n);
    for (auto& s : renderDone_) { VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VK_OK(vkCreateSemaphore(dev, &si, nullptr, &s)); }
}
void Renderer::destroySwapchain() {
    VkDevice dev = ctx_->device;
    for (auto s : renderDone_) vkDestroySemaphore(dev, s, nullptr);
    renderDone_.clear();
    if (swapchain_) vkDestroySwapchainKHR(dev, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}
void Renderer::createTargets() {
    VkSampleCountFlagBits ms = ctx_->msaa;
    msaaColor_ = ctx_->createImage(width_, height_, colorFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 1, ms);
    msaaDepth_ = ctx_->createImage(width_, height_, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 1, ms, VK_IMAGE_ASPECT_DEPTH_BIT);
    sceneColor_ = ctx_->createImage(width_, height_, colorFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    finalColor_ = ctx_->createImage(width_, height_, finalFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    readback_ = ctx_->createBuffer((VkDeviceSize)width_ * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    bloomLevels_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) {
        uint32_t w = std::max(1u, width_ >> (i + 1)), h = std::max(1u, height_ >> (i + 1));
        if (i > 0 && (w < 4 || h < 4)) break;
        bloom_[i] = ctx_->createImage(w, h, colorFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        ++bloomLevels_;
    }
    createPostSets();
}
void Renderer::destroyTargets() {
    ctx_->destroyImage(msaaColor_); ctx_->destroyImage(msaaDepth_); ctx_->destroyImage(sceneColor_); ctx_->destroyImage(finalColor_); ctx_->destroyBuffer(readback_);
    for (int i = 0; i < kBloomLevels; ++i) ctx_->destroyImage(bloom_[i]);
    bloomLevels_ = 0;
    if (postPool_) vkResetDescriptorPool(ctx_->device, postPool_, 0);
}
void Renderer::createPostSets() {
    VkDevice dev = ctx_->device;
    if (!lutTex_) { LutStrip id = identityLut(16); lutSize_ = id.size; lutTex_ = createTexture("lut_identity", id.rgba.data(), id.size * id.size, id.size, false, false, false); }
    auto make = [&](VkImageView a, VkImageView b) {
        VkDescriptorSet s; VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = postPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &postSetLayout_;
        VK_OK(vkAllocateDescriptorSets(dev, &ai, &s));
        VkDescriptorImageInfo ia{postSampler_, a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, ib{postSampler_, b, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo il{lutTex_->sampler, lutTex_->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[3]{};
        w[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &il, nullptr, nullptr};
        w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ia, nullptr, nullptr};
        w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ib, nullptr, nullptr};
        vkUpdateDescriptorSets(dev, 3, w, 0, nullptr);
        return s;
    };
    postSceneSet_ = make(sceneColor_.view, sceneColor_.view);
    for (int i = 0; i < bloomLevels_; ++i) postBloomSet_[i] = make(bloom_[i].view, bloom_[i].view);
    postCompSet_ = make(sceneColor_.view, bloom_[0].view);
}
void Renderer::resize(uint32_t w, uint32_t h) {
    if (!presenting()) {                     // headless: just rebuild targets
        vkDeviceWaitIdle(ctx_->device); destroyTargets(); width_ = w; height_ = h; createTargets(); return;
    }
    needResize_ = true;
}

// ===================================================================================== pipelines
VkPipeline Renderer::makePipeline(VkShaderModule vs, VkShaderModule fs, int vertexKind, VkCullModeFlags cull, bool depthTest, bool depthWrite,
                                  VkSampleCountFlagBits samples, bool hasColor, VkFormat colorFmt, bool depthBias, VkPrimitiveTopology topo, bool blend) {
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription bind{0, vertexKind == 1 ? (uint32_t)sizeof(Vertex) : (uint32_t)sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription mesh[6] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)}, {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vertex, joints)},    {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)}};
    VkVertexInputAttributeDescription line[2] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (vertexKind) { vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
                      vi.vertexAttributeDescriptionCount = vertexKind == 1 ? 6 : 2; vi.pVertexAttributeDescriptions = vertexKind == 1 ? mesh : line; }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = topo;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = cull; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;   // empirically verified: front faces are CCW with our Y-flipped projection
    rs.lineWidth = 1.f; rs.depthBiasEnable = depthBias; rs.depthBiasConstantFactor = 1.5f; rs.depthBiasSlopeFactor = 2.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depthTest; ds.depthWriteEnable = depthWrite; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF; cba.blendEnable = blend;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = hasColor ? 1 : 0; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = hasColor ? 1 : 0; ri.pColorAttachmentFormats = &colorFmt; ri.depthAttachmentFormat = kDepthFormat;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &ri; gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
    gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dy;
    gp.layout = pipeLayout_;
    VkPipeline p; VK_OK(vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1, &gp, nullptr, &p));
    return p;
}

VkPipeline Renderer::makePostPipeline(VkShaderModule fs, VkShaderModule vs, VkFormat fmt, bool additive) {
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF; cba.blendEnable = additive;
    cba.srcColorBlendFactor = cba.dstColorBlendFactor = cba.srcAlphaBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &fmt;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &ri; gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
    gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dy; gp.layout = postLayout_;
    VkPipeline p; VK_OK(vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1, &gp, nullptr, &p));
    return p;
}

void Renderer::createPipelines(const std::string& dir) {
    auto load = [&](const char* n) { auto m = ctx_->loadShader(dir + "/" + n + ".spv"); modules_.push_back(m); return m; };
    auto toonV = load("toon.vert"), toonF = load("toon.frag"), outV = load("outline.vert"), outF = load("outline.frag"),
         shV = load("shadow.vert"), shF = load("shadow.frag"), skyV = load("sky.vert"), skyF = load("sky.frag"),
         lnV = load("line.vert"), lnF = load("line.frag");
    VkSampleCountFlagBits ms = ctx_->msaa;
    auto tri = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeToon_    = makePipeline(toonV, toonF, 1, VK_CULL_MODE_NONE,  true, true, ms, true, colorFormat_, false, tri, false);
    pipeToonBlend_ = makePipeline(toonV, toonF, 1, VK_CULL_MODE_NONE, true, false, ms, true, colorFormat_, false, tri, true);
    pipeOutline_ = makePipeline(outV, outF, 1, VK_CULL_MODE_FRONT_BIT, true, true, ms, true, colorFormat_, false, tri, false);
    pipeShadow_  = makePipeline(shV, shF, 1, VK_CULL_MODE_NONE,  true, true, VK_SAMPLE_COUNT_1_BIT, false, VK_FORMAT_UNDEFINED, true, tri, false);
    pipeSky_     = makePipeline(skyV, skyF, 0, VK_CULL_MODE_NONE, false, false, ms, true, colorFormat_, false, tri, false);
    {
        auto pv = load("post.vert"), pf = load("post_prefilter.frag"), pd = load("post_down.frag"), pu = load("post_up.frag"), pc = load("post_composite.frag");
        pipePrefilter_ = makePostPipeline(pf, pv, colorFormat_, false); pipeDown_ = makePostPipeline(pd, pv, colorFormat_, false);
        pipeUp_ = makePostPipeline(pu, pv, colorFormat_, true); pipeComposite_ = makePostPipeline(pc, pv, finalFormat_, false);
    }
    pipeLine_    = makePipeline(lnV, lnF, 2, VK_CULL_MODE_NONE,  true, false, ms, true, colorFormat_, false, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true);
}

// ===================================================================================== resources
GpuMesh* Renderer::createMesh(const MeshData& d) {
    auto m = std::make_unique<GpuMesh>();
    m->indexCount = (uint32_t)d.indices.size();
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (auto& v : d.vertices) { mn = glm::min(mn, v.pos); mx = glm::max(mx, v.pos); }
    m->boundsMin = mn; m->boundsMax = mx; m->vertexCount = (uint32_t)d.vertices.size();
    auto upload = [&](const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        Buffer staging = ctx_->createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        std::memcpy(staging.mapped, data, size);
        Buffer dst = ctx_->createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
        ctx_->immediate([&](VkCommandBuffer c) { VkBufferCopy r{0, 0, size}; vkCmdCopyBuffer(c, staging.buffer, dst.buffer, 1, &r); });
        ctx_->destroyBuffer(staging);
        return dst;
    };
    m->vertex = upload(d.vertices.data(), d.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m->index = upload(d.indices.data(), d.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    meshes_.push_back(std::move(m));
    return meshes_.back().get();
}

Texture* Renderer::createTexture(const std::string& name, const uint8_t* rgba, uint32_t w, uint32_t h, bool srgb, bool mips, bool repeat) {
    auto t = std::make_unique<Texture>(); t->name = name;
    uint32_t levels = mips ? (uint32_t)std::floor(std::log2((double)std::max(w, h))) + 1 : 1;
    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    t->image = ctx_->createImage(w, h, fmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, levels);
    VkDeviceSize size = (VkDeviceSize)w * h * 4;
    Buffer staging = ctx_->createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, rgba, size);
    ctx_->immediate([&](VkCommandBuffer c) {
        ctx_->barrier(c, t->image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, levels);
        VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(c, staging.buffer, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        int32_t mw = (int32_t)w, mh = (int32_t)h;
        for (uint32_t i = 1; i < levels; ++i) {
            ctx_->barrier(c, t->image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1);
            VkImageBlit b{};
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1}; b.srcOffsets[1] = {mw, mh, 1};
            mw = std::max(1, mw / 2); mh = std::max(1, mh / 2);
            b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1}; b.dstOffsets[1] = {mw, mh, 1};
            vkCmdBlitImage(c, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
        }
        if (levels > 1) ctx_->barrier(c, t->image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, levels - 1);
        ctx_->barrier(c, t->image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, levels - 1, 1);
    });
    ctx_->destroyBuffer(staging);
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = sc.minFilter = VK_FILTER_LINEAR; sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkPhysicalDeviceFeatures f; vkGetPhysicalDeviceFeatures(ctx_->phys, &f);
    // anisotropic filtering only for mip-mapped textures: lookup tables (ramps/LUT) are sampled with constant
    // coordinates over large areas (zero derivatives), which yields NaN with anisotropy on some implementations.
    sc.anisotropyEnable = (f.samplerAnisotropy && levels > 1) ? VK_TRUE : VK_FALSE; sc.maxAnisotropy = std::min(8.f, ctx_->props.limits.maxSamplerAnisotropy); sc.maxLod = (float)levels;
    VK_OK(vkCreateSampler(ctx_->device, &sc, nullptr, &t->sampler));
    textures_.push_back(std::move(t));
    return textures_.back().get();
}

bool Renderer::setMorphTargets(GpuMesh* mesh, const std::vector<int>& targetIndices, const std::vector<std::vector<glm::vec3>>& deltas) {
    if (!mesh || deltas.empty()) return false;
    uint32_t need = (uint32_t)deltas.size() * mesh->vertexCount;
    if (morphCursor_ + need > kMaxMorphVec4) { SK_WARN("morph buffer full, skipping targets of a mesh"); return false; }
    glm::vec4* dst = (glm::vec4*)morphDeltas_.mapped + morphCursor_;
    for (size_t k = 0; k < deltas.size(); ++k)
        for (uint32_t v = 0; v < mesh->vertexCount; ++v) dst[k * mesh->vertexCount + v] = v < deltas[k].size() ? glm::vec4(deltas[k][v], 0.f) : glm::vec4(0.f);
    mesh->morphBase = morphCursor_; mesh->morphSlots = (uint32_t)deltas.size(); mesh->morphTargetIndex = targetIndices;
    morphCursor_ += need;
    return true;
}

Material* Renderer::createMaterial(const std::string& name) {
    auto m = std::make_unique<Material>(); m->name = name;
    for (int i = 0; i < TEX_COUNT; ++i) m->tex[i] = white_;
    m->tex[TEX_NORMAL] = bump_;
    for (int s : {TEX_MATCAP, TEX_FACE_SDF, TEX_HIGHLIGHT, TEX_HAIR_SPEC, TEX_EMISSION, TEX_FLOW}) m->tex[s] = black_;
    m->ubo = ctx_->createBuffer(sizeof(MaterialParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = pool_; da.descriptorSetCount = 1; da.pSetLayouts = &materialLayout_;
    VK_OK(vkAllocateDescriptorSets(ctx_->device, &da, &m->set));
    materials_.push_back(std::move(m));
    flushMaterial(materials_.back().get());
    return materials_.back().get();
}

void Renderer::flushMaterial(Material* m) {
    std::array<Texture*, TEX_COUNT> want;
    for (int i = 0; i < TEX_COUNT; ++i) want[i] = m->tex[i] ? m->tex[i] : white_;
    bool paramsChanged = !m->everUploaded || std::memcmp(&m->uploaded, &m->params, sizeof(MaterialParams)) != 0;
    bool texChanged = !m->everUploaded || want != m->bound;
    if (!paramsChanged && !texChanged) return;
    if (m->everUploaded) vkDeviceWaitIdle(ctx_->device);          // a frame may still be reading this material
    if (paramsChanged) { std::memcpy(m->ubo.mapped, &m->params, sizeof(MaterialParams)); m->uploaded = m->params; }
    if (texChanged) {
        VkDescriptorBufferInfo bi{m->ubo.buffer, 0, sizeof(MaterialParams)};
        VkDescriptorImageInfo ii[TEX_COUNT];
        VkWriteDescriptorSet wr[1 + TEX_COUNT]{};
        wr[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m->set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr};
        for (int i = 0; i < TEX_COUNT; ++i) {
            Texture* t = want[i];
            ii[i] = {t->sampler, t->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            wr[1 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m->set, (uint32_t)(1 + i), 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii[i], nullptr, nullptr};
        }
        vkUpdateDescriptorSets(ctx_->device, 1 + TEX_COUNT, wr, 0, nullptr);
        m->bound = want;
    }
    m->everUploaded = true;
}

// ===================================================================================== UI
std::vector<std::string> Renderer::uiFontCandidates() {
#if defined(_WIN32)
    return {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/tahoma.ttf", "C:/Windows/Fonts/verdana.ttf"};
#elif defined(__APPLE__)
    return {"/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf"};
#else
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"};
#endif
}

void Renderer::initUI(GLFWwindow* window, const UiTheme& theme, const std::string& fontPath, float fontPx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    uiTheme_ = theme;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;    // keyboard/gamepad navigation works in the editors
    // Font: an explicit --ui-font wins, then the platform's UI font, then ImGui's built-in bitmap font. A missing file is
    // not fatal - AddFontFromFileTTF asserts on failure, so the existence check happens here.
    ImFont* font = nullptr;
    auto tryFont = [&](const std::string& path) {
        if (font || path.empty()) return;
        std::error_code ec; if (!std::filesystem::exists(path, ec)) return;
        font = io.Fonts->AddFontFromFileTTF(path.c_str(), fontPx);
        SK_LOG("ui: font %s (%.0f px)", path.c_str(), fontPx);
    };
    tryFont(fontPath);
    if (fontPath.empty()) for (const std::string& c : uiFontCandidates()) { tryFont(c); if (font) break; }
    if (!font) {
#ifdef __ANDROID__
        ImFontConfig cfg; cfg.SizePixels = fontPx; io.Fonts->AddFontDefault(&cfg);
#else
        io.Fonts->AddFontDefault();
#endif
        SK_LOG("ui: built-in font (no system UI font found)");
    }
    io.FontGlobalScale = uiTheme_.fontScale;
    applyUiTheme(uiTheme_, ImGui::GetStyle());
#ifndef __ANDROID__
    ImGui_ImplGlfw_InitForVulkan(window, true);
#endif
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = ctx_->instance; ii.PhysicalDevice = ctx_->phys; ii.Device = ctx_->device;
    ii.QueueFamily = ctx_->queueFamily; ii.Queue = ctx_->queue;
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2; ii.ImageCount = 2; ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    static VkFormat fmt; fmt = finalFormat_;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &fmt;
    ImGui_ImplVulkan_Init(&ii);
    ui_ = true;
}
void Renderer::setUiTheme(const UiTheme& theme) {
    uiTheme_ = theme;
    if (!ui_) return;
    applyUiTheme(uiTheme_, ImGui::GetStyle());
    ImGui::GetIO().FontGlobalScale = uiTheme_.fontScale;
}

void Renderer::beginUI(float deltaTime) {
    if (!ui_ || uiFrameOpen_) return;
    ImGui_ImplVulkan_NewFrame();
#ifndef __ANDROID__
    (void)deltaTime;
    ImGui_ImplGlfw_NewFrame();
#else
    ImGui::GetIO().DisplaySize = ImVec2((float)width_, (float)height_);
    ImGui::GetIO().DeltaTime = std::max(deltaTime, 1e-6f);
#endif
    ImGui::NewFrame();
    uiFrameOpen_ = true;
}

// ===================================================================================== frame
void Renderer::drawList(VkCommandBuffer cmd, const FrameInput& in, DrawFilter filter) {
    VkDeviceSize zero = 0;
    std::vector<const DrawItem*> order;
    order.reserve(in.draws.size());
    for (auto& d : in.draws) {
        if (!d.mesh || !d.material) continue;
        bool tr = d.material->transparent;
        switch (filter) {
            case DrawFilter::Opaque:      if (tr) continue; break;
            case DrawFilter::Transparent: if (!tr) continue; break;
            case DrawFilter::Shadow:      if (!d.material->castShadow || tr) continue; break;
            case DrawFilter::Outline:     if (!d.material->outline() || tr) continue; break;
        }
        order.push_back(&d);
    }
    if (filter == DrawFilter::Transparent) {     // back to front by view-space depth of the mesh bounds centre
        auto depth = [&](const DrawItem* d) { glm::vec3 c = 0.5f * (d->mesh->boundsMin + d->mesh->boundsMax); return (in.view * d->model * glm::vec4(c, 1.f)).z; };
        std::stable_sort(order.begin(), order.end(), [&](const DrawItem* a, const DrawItem* b) { return depth(a) < depth(b); });   // view space looks down -Z: more negative = farther
    }
    for (const DrawItem* dp : order) {
        const DrawItem& d = *dp;
        bool morph = d.morphCount > 0 && d.mesh->morphSlots > 0;
        PushConstants pc{d.model, d.boneOffset, d.skinned ? 1u : 0u, d.faceFrame ? 1u : 0u, (uint32_t)cascadeIdx_, morph ? d.morphWeightOffset : 0u, morph ? std::min(d.morphCount, d.mesh->morphSlots) : 0u, d.mesh->morphBase, d.mesh->vertexCount};
        vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 1, 1, &d.material->set, 0, nullptr);
        vkCmdBindVertexBuffers(cmd, 0, 1, &d.mesh->vertex.buffer, &zero);
        vkCmdBindIndexBuffer(cmd, d.mesh->index.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, d.mesh->indexCount, 1, 0, 0, 0);
        if (filter != DrawFilter::Shadow) { stats.drawCalls++; stats.triangles += d.mesh->indexCount / 3; }
    }
}

// ===================================================================================== image based lighting
void Renderer::setEnvironmentHDR(const float* rgb, int w, int h) {
    std::vector<float> px((size_t)w * h * 4);
    float best = 0.f; glm::vec3 sun(0.f);
    for (size_t i = 0; i < (size_t)w * h; ++i) { px[i * 4] = rgb[i * 3]; px[i * 4 + 1] = rgb[i * 3 + 1]; px[i * 4 + 2] = rgb[i * 3 + 2]; px[i * 4 + 3] = 1.f;
        best = std::max(best, 0.2126f * rgb[i * 3] + 0.7152f * rgb[i * 3 + 1] + 0.0722f * rgb[i * 3 + 2]); }
    // dominant light direction = luminance-weighted centroid of the brightest pixels
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const float* c = &rgb[((size_t)y * w + x) * 3]; float l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
        if (l > best * 0.7f && best > 20.f) sun += equirectDir((x + .5f) / w, (y + .5f) / h) * l;
    }
    envSun_ = glm::length(sun) > 0 ? glm::normalize(sun) : glm::vec3(0.f);
    envHDR_ = true;
    uploadEnvironment(px, w, h);
}

void Renderer::updateProceduralEnvironment(const LightingSettings& L) {
    if (envHDR_) return;
    uint64_t hsh = 1469598103934665603ull;
    auto mix = [&](float v) { uint32_t b; std::memcpy(&b, &v, 4); hsh = (hsh ^ b) * 1099511628211ull; };
    for (int i = 0; i < 3; ++i) { mix(L.sky[i]); mix(L.ground[i]); mix(L.zenith[i]); mix(L.horizon[i]); }
    if (hsh == procHash_ && env_.image) return;
    procHash_ = hsh;
    const int w = 256, h = 128;
    std::vector<float> px((size_t)w * h * 4);
    glm::vec3 zenith = L.zenith, horizon = L.horizon;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float d = equirectDir((x + .5f) / w, (y + .5f) / h).y;
        auto ss = [](float a, float b, float v) { float t = glm::clamp((v - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); };
        glm::vec3 c = glm::mix(horizon, zenith, ss(0.f, 0.65f, d));
        c = glm::mix(c, L.ground, ss(0.f, -0.25f, d));
        float* o = &px[((size_t)y * w + x) * 4]; o[0] = c.x; o[1] = c.y; o[2] = c.z; o[3] = 1.f;
    }
    envSun_ = glm::vec3(0.f);
    uploadEnvironment(px, w, h);
}

void Renderer::uploadEnvironment(const std::vector<float>& rgba, int w, int h) {
    VkDevice dev = ctx_->device; vkDeviceWaitIdle(dev);
    std::vector<std::vector<float>> mips; std::vector<glm::ivec2> dims;
    mips.push_back(rgba); dims.push_back({w, h});
    for (auto& v : mips[0]) v = std::min(v, 64.f);                      // tame the sun disc: the directional light already provides it
    while (dims.back().x > 1 || dims.back().y > 1) {
        glm::ivec2 pd = dims.back(), nd = {std::max(1, pd.x / 2), std::max(1, pd.y / 2)};
        std::vector<float> n((size_t)nd.x * nd.y * 4); const auto& p = mips.back();
        for (int y = 0; y < nd.y; ++y) for (int x = 0; x < nd.x; ++x) for (int c = 0; c < 4; ++c) {
            float acc = 0; int cnt = 0;
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) { int sx = std::min(pd.x - 1, x * 2 + dx), sy = std::min(pd.y - 1, y * 2 + dy); acc += p[((size_t)sy * pd.x + sx) * 4 + c]; ++cnt; }
            n[((size_t)y * nd.x + x) * 4 + c] = acc / cnt;
        }
        mips.push_back(std::move(n)); dims.push_back(nd);
    }
    // --- SH9 from a ~64-wide level (radiance clamped to 16 so a visible sun does not dominate the ambient term)
    {
        size_t lv = 0; while (lv + 1 < dims.size() && dims[lv].x > 64) ++lv;
        glm::vec3 L[9] = {};
        for (int y = 0; y < dims[lv].y; ++y) for (int x = 0; x < dims[lv].x; ++x) {
            float v = (y + .5f) / dims[lv].y; glm::vec3 d = equirectDir((x + .5f) / dims[lv].x, v);
            float dw = (2.f * glm::pi<float>() / dims[lv].x) * (glm::pi<float>() / dims[lv].y) * std::sin(v * glm::pi<float>());
            const float* c = &mips[lv][((size_t)y * dims[lv].x + x) * 4]; glm::vec3 rad = glm::min(glm::vec3(c[0], c[1], c[2]), glm::vec3(16.f));
            float Y[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y, 1.092548f * d.y * d.z,
                          0.315392f * (3.f * d.z * d.z - 1.f), 1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y)};
            for (int i = 0; i < 9; ++i) L[i] += rad * Y[i] * dw;
        }
        const float A[9] = {3.141593f, 2.094395f, 2.094395f, 2.094395f, 0.785398f, 0.785398f, 0.785398f, 0.785398f, 0.785398f};
        for (int i = 0; i < 9; ++i) sh_[i] = glm::vec4(L[i] * A[i] / glm::pi<float>(), 0.f);
    }
    // --- GPU image (RGBA16F with the full mip chain)
    uint32_t levels = (uint32_t)dims.size();
    bool recreate = !env_.image || (int)env_.width != w || (int)env_.height != h;
    if (recreate) {
        ctx_->destroyImage(env_);
        env_ = ctx_->createImage(w, h, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, levels);
        for (auto& f : frames_) {
            VkDescriptorImageInfo ii{envSampler_, env_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.globalSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr};
            vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
        }
    }
    envMaxLod_ = (float)(levels - 1);
    size_t total = 0; std::vector<size_t> off;
    for (size_t i = 0; i < mips.size(); ++i) { off.push_back(total); total += (size_t)dims[i].x * dims[i].y * 8; }
    Buffer staging = ctx_->createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    for (size_t i = 0; i < mips.size(); ++i) {
        uint16_t* o = (uint16_t*)((uint8_t*)staging.mapped + off[i]);
        for (size_t k = 0; k < mips[i].size(); ++k) o[k] = floatToHalf(mips[i][k]);
    }
    ctx_->immediate([&](VkCommandBuffer c) {
        ctx_->barrier(c, env_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (size_t i = 0; i < mips.size(); ++i) {
            VkBufferImageCopy r{}; r.bufferOffset = off[i]; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, 0, 1}; r.imageExtent = {(uint32_t)dims[i].x, (uint32_t)dims[i].y, 1};
            vkCmdCopyBufferToImage(c, staging.buffer, env_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        }
        ctx_->barrier(c, env_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    ctx_->destroyBuffer(staging);
}

bool Renderer::renderFrame(const FrameInput& in, std::vector<uint8_t>* capture) {
    auto t0 = std::chrono::steady_clock::now();
    VkDevice dev = ctx_->device;
    FrameData& f = frames_[frameIndex_];
    VK_OK(vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX));
    if (hasPendingLut_) {                       // swap the grading LUT: all post descriptor sets are rebuilt
        hasPendingLut_ = false;
        vkDeviceWaitIdle(dev);
        lutTex_ = createTexture("lut", pendingLut_.rgba.data(), pendingLut_.size * pendingLut_.size, pendingLut_.size, false, false, false);
        lutSize_ = pendingLut_.size;
        vkResetDescriptorPool(dev, postPool_, 0);
        createPostSets();
    }

    uint32_t imgIdx = 0;
    if (presenting()) {
        int fw, fh; framebufferSize(fw, fh);
        if (fw == 0 || fh == 0) { if (uiFrameOpen_) { ImGui::EndFrame(); uiFrameOpen_ = false; } return false; }   // minimised
        if (needResize_ || fw != surfaceWidth_ || fh != surfaceHeight_) {
            vkDeviceWaitIdle(dev); destroySwapchain(); destroyTargets(); createSwapchain(); createTargets(); needResize_ = false;
        }
        VkResult r = vkAcquireNextImageKHR(dev, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imgIdx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { needResize_ = true; if (uiFrameOpen_) { ImGui::EndFrame(); uiFrameOpen_ = false; } return false; }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) SK_FATAL("vkAcquireNextImageKHR failed (%d)", (int)r);
    }
    VK_OK(vkResetFences(dev, 1, &f.fence));

    // ---------------- upload per-frame data
    GlobalUBO g{};
    const LightingSettings& L = in.light;
    if (!envHDR_) updateProceduralEnvironment(L);
    glm::vec3 ld = glm::normalize(L.direction);
    {   // two directional shadow cascades, snapped to shadow texels (gfx/ShadowFit.h).
        // Far: the whole arena around the focus. Near: a bounding sphere of the *camera frustum slice* [zNear, split], grown to
        // also contain the focus (hero), so the crisp cascade is spent on what is actually on screen instead of a fixed box around the hero.
        float rNear = L.shadowRadius;
        glm::vec3 nearC = in.shadowFocus;
        float rFar = std::max(L.shadowFarRadius, L.shadowRadius * 1.5f);
        if (L.fitCascadeToCamera) {
            float zn = 0.3f, zf = std::max(L.shadowRadius * 2.2f, 4.f);
            ShadowSphere s = cameraSliceSphere(in.proj, in.view, in.cameraPos, zn, zf);
            s = growSphereToInclude(s, in.shadowFocus, 1.2f);          // the hero must stay inside the crisp cascade
            s.radius = quantiseShadowRadius(s.radius, L.shadowFarRadius * 0.8f);
            nearC = s.centre; rNear = s.radius;
        }
        g.lightViewProj = fitShadowMatrix(nearC, rNear, ld, kShadowRes);
        g.lightViewProj1 = fitShadowMatrix(in.shadowFocus, rFar, ld, kShadowRes);
        g.shadowCascade = glm::vec4(rNear, rFar, 0.f, 2.f);
    }
    for (int i = 0; i < kMaxContacts; ++i) g.contactPos[i] = L.contacts[i];
    // Point-light shadows: the (up to) 2 lights nearest to the focus that are on and can reach it get a 6-face cube shadow atlas.
    int pointSlotLight[kMaxPointShadows] = {-1, -1}; int nSlots = 0;
    for (int i = 0; i < 4; ++i) g.pointShadow[i] = glm::vec4(-1.f, 0.f, 0.f, 0.f);
    if (L.pointShadows) {
        glm::vec3 pos[4]; float intensity[4], range[4];
        for (int i = 0; i < 4; ++i) { pos[i] = L.points[i].position; intensity[i] = L.points[i].intensity; range[i] = L.points[i].range; }
        int slotOfLight[4];
        // the (up to) 2 lights nearest to the focus that can reach it get a cube atlas (gfx/ShadowFit.h)
        nSlots = choosePointShadowLights(pos, intensity, range, in.shadowFocus, kMaxPointShadows, 8.f, slotOfLight);
        for (int i = 0; i < 4; ++i) {
            if (slotOfLight[i] < 0) continue;
            int slot = slotOfLight[i];
            pointSlotLight[slot] = i;
            g.pointShadow[i] = glm::vec4((float)slot, 0.035f, 0.0006f, 0.f);
            for (int f = 0; f < kPointShadowFaces; ++f) g.pointViewProj[slot * 6 + f] = pointShadowFaceMatrix(pos[i], range[i], f);
        }
    }
    stats.pointShadowLights = (uint32_t)nSlots;
    g.viewProj = in.proj * in.view; g.view = in.view; g.proj = in.proj;
    g.cameraPos = glm::vec4(in.cameraPos, in.time);
    g.screenParams = glm::vec4((float)width_, (float)height_, 1.f / width_, 1.f / height_);
    g.lightDir = glm::vec4(ld, L.intensity);
    g.lightColor = glm::vec4(L.color, L.shadowBias);
    g.skyColor = glm::vec4(L.sky, L.ambient);
    g.groundColor = glm::vec4(L.ground, 0);
    g.fogColor = glm::vec4(L.fogColor, L.fogDensity);
    g.misc = glm::vec4(1.f, 0.f, L.wetPreviewTime, 1.f / (float)kShadowRes);
    g.skyZenith = glm::vec4(L.zenith, L.cloudCover); g.skyHorizon = glm::vec4(L.horizon, L.cloudTime); g.skyParams = glm::vec4(L.starVisibility, L.sunDisc, L.sunSize, 0.f);
    g.style = glm::vec4(L.nprOnly ? 1.f : 0.f, 0.f, 0.f, 0.f);
    g.split = glm::vec4(L.splitOffsetPx, L.splitDir.x, L.splitDir.y, L.outlineScale);
    for (int i = 0; i < 9; ++i) g.sh[i] = sh_[i];
    g.envParams = glm::vec4(L.envIntensity, envMaxLod_, envHDR_ ? 1.f : 0.f, L.envRotation);
    for (int i = 0; i < kMaxPointLights; ++i) { const PointLight& pl = L.points[i];
        g.pointPos[i] = glm::vec4(pl.position, std::max(pl.range, 0.01f)); g.pointCol[i] = glm::vec4(pl.color * pl.intensity, 0.f); }
    g.faceForward = glm::vec4(in.faceForward, 0.f); g.faceRight = glm::vec4(in.faceRight, 0.f);
    std::memcpy(f.globalUBO.mapped, &g, sizeof(g));
    size_t nb = std::min<size_t>(in.bones.size(), kMaxBones);
    if (nb) std::memcpy(f.bones.mapped, in.bones.data(), nb * sizeof(glm::mat4));
    size_t nmw = std::min<size_t>(in.morphWeights.size(), kMaxMorphWeights);
    if (nmw) std::memcpy(f.morphWeights.mapped, in.morphWeights.data(), nmw * sizeof(float));
    size_t nl = std::min<size_t>(in.lines.size() & ~size_t(1), kMaxLineVerts);
    if (nl) std::memcpy(f.lines.mapped, in.lines.data(), nl * sizeof(LineVertex));

    // ---------------- record
    VkCommandBuffer cmd = f.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_OK(vkBeginCommandBuffer(cmd, &bi));
    stats.drawCalls = 0; stats.triangles = 0;

    // shadow pass
    {
        ctx_->barrier(cmd, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        da.imageView = shadow_.view; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE; da.clearValue.depthStencil = {1.f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {kShadowRes * 2, kShadowRes}}; ri.layerCount = 1; ri.pDepthAttachment = &da;
        ctx_->beginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeShadow_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &f.globalSet, 0, nullptr);
        for (int c = 0; c < 2; ++c) {
            VkViewport vp{(float)(c * kShadowRes), 0, (float)kShadowRes, (float)kShadowRes, 0, 1}; VkRect2D sc{{(int32_t)(c * kShadowRes), 0}, {kShadowRes, kShadowRes}};
            vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
            cascadeIdx_ = c;
            drawList(cmd, in, DrawFilter::Shadow);
        }
        cascadeIdx_ = 0;
        ctx_->endRendering(cmd);
        ctx_->barrier(cmd, shadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    // point-light cube shadow atlas (6 faces x 2 lights; always cleared so the descriptor stays valid, faces only drawn for active lights)
    {
        ctx_->barrier(cmd, pointShadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        da.imageView = pointShadow_.view; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE; da.clearValue.depthStencil = {1.f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {kPointShadowRes * 6, kPointShadowRes * kMaxPointShadows}}; ri.layerCount = 1; ri.pDepthAttachment = &da;
        ctx_->beginRendering(cmd, &ri);
        stats.pointShadowFaces = 0;
        if (nSlots > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeShadow_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &f.globalSet, 0, nullptr);
            for (int s = 0; s < nSlots; ++s) for (int face = 0; face < 6; ++face) {
                int x = face * (int)kPointShadowRes, y = s * (int)kPointShadowRes;
                VkViewport vp{(float)x, (float)y, (float)kPointShadowRes, (float)kPointShadowRes, 0, 1}; VkRect2D sc{{x, y}, {kPointShadowRes, kPointShadowRes}};
                vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
                cascadeIdx_ = 2 + s * 6 + face;
                drawList(cmd, in, DrawFilter::Shadow);
                ++stats.pointShadowFaces;
            }
            cascadeIdx_ = 0;
        }
        ctx_->endRendering(cmd);
        ctx_->barrier(cmd, pointShadow_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    // main pass (MSAA -> resolve into sceneColor)
    {
        ctx_->barrier(cmd, msaaColor_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        ctx_->barrier(cmd, msaaDepth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        ctx_->barrier(cmd, sceneColor_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (ctx_->msaa == VK_SAMPLE_COUNT_1_BIT) {
            ca.imageView = sceneColor_.view; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else {
            ca.imageView = msaaColor_.view; ca.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            ca.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; ca.resolveImageView = sceneColor_.view; ca.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ca.clearValue.color = {{0.1f, 0.1f, 0.12f, 1.f}};
        da.imageView = msaaDepth_.view; da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; da.clearValue.depthStencil = {1.f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {width_, height_}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca; ri.pDepthAttachment = &da;
        ctx_->beginRendering(cmd, &ri);
        VkViewport vp{0, 0, (float)width_, (float)height_, 0, 1}; VkRect2D sc{{0, 0}, {width_, height_}};
        vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &f.globalSet, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeSky_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeToon_);
        drawList(cmd, in, DrawFilter::Opaque);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeOutline_);
        drawList(cmd, in, DrawFilter::Outline);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeToonBlend_);          // alpha-blended materials, back to front
        drawList(cmd, in, DrawFilter::Transparent);
        if (nl) {
            VkDeviceSize zero = 0;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLine_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &f.lines.buffer, &zero);
            vkCmdDraw(cmd, (uint32_t)nl, 1, 0, 0);
        }
        ctx_->endRendering(cmd);
    }

    // post-processing: bloom chain + composite (tonemap, grade, vignette, sRGB) into finalColor_
    recordPost(cmd, in);

    // UI overlay (drawn on the final image so it also shows in captures)
    if (uiFrameOpen_) {
        ImGui::Render();
        if (uiVisible_) {
            VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            ca.imageView = finalColor_.view; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea = {{0, 0}, {width_, height_}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
            ctx_->beginRendering(cmd, &ri);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            ctx_->endRendering(cmd);
        }
        uiFrameOpen_ = false;
    }

    // present / capture
    ctx_->barrier(cmd, finalColor_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (capture || !presenting()) {
        VkBufferImageCopy r{}; r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; r.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(cmd, finalColor_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1, &r);
    }
    if (presenting()) {
        VkImage sw = swapImages_[imgIdx];
        ctx_->barrier(cmd, sw, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit b{}; b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; b.srcOffsets[1] = {(int32_t)width_, (int32_t)height_, 1};
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; b.dstOffsets[1] = {(int32_t)width_, (int32_t)height_, 1};
        vkCmdBlitImage(cmd, finalColor_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
        ctx_->barrier(cmd, sw, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    VK_OK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    if (presenting()) {
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &f.imageAvailable; si.pWaitDstStageMask = &wait;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderDone_[imgIdx];
    }
    VK_OK(vkQueueSubmit(ctx_->queue, 1, &si, f.fence));
    bool presented = true;
    if (presenting()) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_[imgIdx]; pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &imgIdx;
        VkResult r = vkQueuePresentKHR(ctx_->queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
            needResize_ = true;
            if (r == VK_ERROR_OUT_OF_DATE_KHR) presented = false;
        } else if (r != VK_SUCCESS) VK_OK(r);
    }
    if (capture || !presenting()) {
        VK_OK(vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX));
        if (capture) { capture->resize((size_t)width_ * height_ * 4); std::memcpy(capture->data(), readback_.mapped, capture->size()); }
    }
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    stats.cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return presented;
}

void Renderer::recordPost(VkCommandBuffer cmd, const FrameInput& in) {
    const PostSettings& P = in.post;
    const VkImageAspectFlags col = VK_IMAGE_ASPECT_COLOR_BIT;
    ctx_->barrier(cmd, sceneColor_.image, col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto pass = [&](Image& target, VkImageLayout from, VkPipeline pipe, VkDescriptorSet set, const PostPC& pc, bool load, bool toRead) {
        ctx_->barrier(cmd, target.image, col, from, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageView = target.view; ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {target.width, target.height}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
        ctx_->beginRendering(cmd, &ri);
        VkViewport vp{0, 0, (float)target.width, (float)target.height, 0, 1}; VkRect2D sc{{0, 0}, {target.width, target.height}};
        vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, postLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        ctx_->endRendering(cmd);
        if (toRead) ctx_->barrier(cmd, target.image, col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    bool bloomOn = P.enabled && P.bloom && P.bloomIntensity > 0.f;
    // prefilter (soft threshold) into level 0; when bloom is off the threshold is infinite so level 0 is black
    PostPC pf{glm::vec4(bloomOn ? P.bloomThreshold : 1e9f, P.bloomKnee, 1.f / width_, 1.f / height_), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
    pass(bloom_[0], VK_IMAGE_LAYOUT_UNDEFINED, pipePrefilter_, postSceneSet_, pf, false, true);
    if (bloomOn) {
        for (int i = 1; i < bloomLevels_; ++i) {
            PostPC pc{glm::vec4(1.f / bloom_[i - 1].width, 1.f / bloom_[i - 1].height, 0, 0), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
            pass(bloom_[i], VK_IMAGE_LAYOUT_UNDEFINED, pipeDown_, postBloomSet_[i - 1], pc, false, true);
        }
        for (int i = bloomLevels_ - 2; i >= 0; --i) {
            PostPC pc{glm::vec4(1.f / bloom_[i + 1].width, 1.f / bloom_[i + 1].height, P.bloomRadius, 0), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
            pass(bloom_[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, pipeUp_, postBloomSet_[i + 1], pc, true, true);
        }
    }
    PostPC pc;
    bool en = P.enabled;
    pc.a = glm::vec4(bloomOn ? P.bloomIntensity : 0.f, P.exposure, en ? (float)P.tonemap : 0.f, en ? P.vignette : 0.f);
    pc.b = glm::vec4(en ? P.saturation : 1.f, en ? P.contrast : 1.f, en ? P.grain : 0.f, P.vignetteSoftness);
    pc.c = glm::vec4(en ? P.shadowTint : glm::vec3(1.f), in.time);
    pc.d = glm::vec4(en ? P.highlightTint : glm::vec3(1.f), (float)width_ / (float)height_);
    pc.e = glm::vec4(en ? P.lutStrength : 0.f, (float)lutSize_, 0.f, 0.f);
    pass(finalColor_, VK_IMAGE_LAYOUT_UNDEFINED, pipeComposite_, postCompSet_, pc, false, false);
}

void Renderer::shutdown() {
    if (!ctx_ || !ctx_->device) return;
    VkDevice dev = ctx_->device;
    vkDeviceWaitIdle(dev);
    if (ui_) { ImGui_ImplVulkan_Shutdown();
#ifndef __ANDROID__
        ImGui_ImplGlfw_Shutdown();
#endif
        ImGui::DestroyContext(); ui_ = false; }
    for (auto& t : textures_) { vkDestroySampler(dev, t->sampler, nullptr); ctx_->destroyImage(t->image); }
    for (auto& m : materials_) ctx_->destroyBuffer(m->ubo);
    for (auto& m : meshes_) { ctx_->destroyBuffer(m->vertex); ctx_->destroyBuffer(m->index); }
    for (auto& f : frames_) {
        ctx_->destroyBuffer(f.globalUBO); ctx_->destroyBuffer(f.bones); ctx_->destroyBuffer(f.lines); ctx_->destroyBuffer(f.morphWeights);
        vkDestroyFence(dev, f.fence, nullptr); vkDestroySemaphore(dev, f.imageAvailable, nullptr);
    }
    for (auto p : {pipeToon_, pipeToonBlend_, pipeOutline_, pipeShadow_, pipeSky_, pipeLine_, pipePrefilter_, pipeDown_, pipeUp_, pipeComposite_}) vkDestroyPipeline(dev, p, nullptr);
    for (auto m : modules_) vkDestroyShaderModule(dev, m, nullptr);
    vkDestroyPipelineLayout(dev, pipeLayout_, nullptr); vkDestroyPipelineLayout(dev, postLayout_, nullptr);
    vkDestroyDescriptorSetLayout(dev, postSetLayout_, nullptr); vkDestroySampler(dev, postSampler_, nullptr);
    ctx_->destroyBuffer(morphDeltas_);
    vkDestroyDescriptorPool(dev, pool_, nullptr);
    vkDestroyDescriptorSetLayout(dev, globalLayout_, nullptr); vkDestroyDescriptorSetLayout(dev, materialLayout_, nullptr);
    vkDestroySampler(dev, shadowSampler_, nullptr); vkDestroySampler(dev, envSampler_, nullptr); ctx_->destroyImage(env_);
    destroyTargets(); ctx_->destroyImage(shadow_); ctx_->destroyImage(pointShadow_); vkDestroyDescriptorPool(dev, postPool_, nullptr); postPool_ = VK_NULL_HANDLE;
    if (presenting()) {
        destroySwapchain();
#ifndef __ANDROID__
        vkDestroySurfaceKHR(ctx_->instance, surface_, nullptr);
#endif
        surface_ = VK_NULL_HANDLE;
    }
}

}  // namespace sk
