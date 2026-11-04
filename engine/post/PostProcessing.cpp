#include "post/PostProcessing.h"
#include <algorithm>
namespace sk {
namespace { struct PostPC { glm::vec4 a, b, c, d, e; }; static_assert(sizeof(PostPC) == 80); }
void PostProcessing::init(Context& ctx) {
    ctx_ = &ctx; VkDevice dev = ctx.device;
    {
        VkDescriptorSetLayoutBinding pb[4] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                                              {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; ci.bindingCount = 4; ci.pBindings = pb;
        VK_OK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &setLayout_));
        VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPC)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1; pl.pSetLayouts = &setLayout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &pcr;
        VK_OK(vkCreatePipelineLayout(dev, &pl, nullptr, &layout_));
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * (kBloomLevels + 13)};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = kBloomLevels + 13; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VK_OK(vkCreateDescriptorPool(dev, &pi, nullptr, &pool_));
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = VK_FILTER_LINEAR; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_OK(vkCreateSampler(dev, &sc, nullptr, &sampler_));
        sc.magFilter = sc.minFilter = VK_FILTER_NEAREST;
        VK_OK(vkCreateSampler(dev, &sc, nullptr, &depthSampler_));
    }

 }

VkPipeline PostProcessing::makePipeline(VkShaderModule fs, VkShaderModule vs, VkFormat fmt, bool additive) {
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
    gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dy; gp.layout = layout_;
    VkPipeline p; VK_OK(vkCreateGraphicsPipelines(ctx_->device, VK_NULL_HANDLE, 1, &gp, nullptr, &p));
    return p;
}

void PostProcessing::createPipelines(VkShaderModule vs, VkShaderModule prefilter, VkShaderModule down, VkShaderModule up,
                                     VkShaderModule halation, VkShaderModule composite, VkShaderModule atmosphere, VkShaderModule dof, VkShaderModule finish, VkFormat hdrFormat, VkFormat finalFormat) {
    prefilter_ = makePipeline(prefilter, vs, hdrFormat, false); down_ = makePipeline(down, vs, hdrFormat, false);
    up_ = makePipeline(up, vs, hdrFormat, true); halationPipe_ = makePipeline(halation, vs, hdrFormat, false);
    composite_ = makePipeline(composite, vs, finalFormat, false);
    atmospherePipe_ = makePipeline(atmosphere, vs, hdrFormat, false);
    dofPipe_ = makePipeline(dof, vs, hdrFormat, false); finishPipe_ = makePipeline(finish, vs, finalFormat, false);
}
void PostProcessing::createTargets(uint32_t width, uint32_t height, VkFormat format, VkFormat finalFormat, const Image& scene, const Image& depth, bool depthSupported, const Image& mask, bool maskSupported, const Texture& lut) {
    width_ = width; height_ = height; bloomLevels_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) {
        uint32_t w = std::max(1u, width >> (i + 1)), h = std::max(1u, height >> (i + 1));
        if (i > 0 && (w < 4 || h < 4)) break;
        bloom_[i] = ctx_->createImage(w, h, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        ++bloomLevels_;
    }
    const auto plan = planPost(PostSettings{}, width, height);
    halation_ = ctx_->createImage(plan.halationWidth, plan.halationHeight, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    display_ = ctx_->createImage(width, height, finalFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if (depthSupported) {
        atmosphere_ = ctx_->createImage(width, height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        dof_ = ctx_->createImage(width, height, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    rebuildSets(scene, depth, depthSupported, mask, maskSupported, lut);
}
void PostProcessing::rebuildSets(const Image& scene, const Image& depth, bool depthSupported, const Image& mask, bool maskSupported, const Texture& lut) {
    VkDevice dev = ctx_->device;
    VK_OK(vkResetDescriptorPool(dev, pool_, 0));
    auto make = [&](VkImageView a, VkImageView b, VkImageView h, bool sampleDepth = false) {
        VkDescriptorSet s; VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &setLayout_;
        VK_OK(vkAllocateDescriptorSets(dev, &ai, &s));
        VkDescriptorImageInfo info[4] = {{sampler_, a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
             {sampleDepth ? depthSampler_ : sampler_, b, sampleDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
             {lut.sampler, lut.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
             {sampler_, h, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet w[4]{};
        for (uint32_t i=0; i<4; ++i) w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, i, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &info[i], nullptr, nullptr};
        vkUpdateDescriptorSets(dev, 4, w, 0, nullptr); return s;
    };
    const VkImageView maskView = maskSupported ? mask.view : scene.view;
    sceneSet_ = make(scene.view, maskView, scene.view);
    for (int i=0; i<bloomLevels_; ++i) bloomSet_[i] = make(bloom_[i].view, bloom_[i].view, bloom_[i].view);
    compSet_ = make(scene.view, bloom_[0].view, halation_.view);
    compNoHalationSet_ = make(scene.view, bloom_[0].view, bloom_[0].view);
    finishSet_ = make(display_.view, display_.view, display_.view);
    if (depthSupported) {
        atmosphereSet_ = make(scene.view, depth.view, scene.view, true);
        foggedSceneSet_ = make(atmosphere_.view, maskView, atmosphere_.view);
        foggedCompSet_ = make(atmosphere_.view, bloom_[0].view, halation_.view);
        foggedCompNoHalationSet_ = make(atmosphere_.view, bloom_[0].view, bloom_[0].view);
        dofSet_ = make(scene.view, depth.view, scene.view, true);
        fogDofSet_ = make(atmosphere_.view, depth.view, scene.view, true);
        dofSceneSet_ = make(dof_.view, maskView, dof_.view);
        dofCompSet_ = make(dof_.view, bloom_[0].view, halation_.view);
        dofCompNoHalationSet_ = make(dof_.view, bloom_[0].view, bloom_[0].view);
    }
}
void PostProcessing::destroyTargets() {
    if (!ctx_) return;
    if (pool_) vkResetDescriptorPool(ctx_->device, pool_, 0);
    for (auto& image : bloom_) ctx_->destroyImage(image);
    ctx_->destroyImage(halation_); ctx_->destroyImage(atmosphere_); ctx_->destroyImage(dof_); ctx_->destroyImage(display_); bloomLevels_ = 0;
}
void PostProcessing::shutdown() {
    if (!ctx_ || !ctx_->device) return;
    VkDevice dev = ctx_->device;
    for (auto p : {prefilter_, down_, up_, halationPipe_, composite_, atmospherePipe_, dofPipe_, finishPipe_}) vkDestroyPipeline(dev, p, nullptr);
    vkDestroyPipelineLayout(dev, layout_, nullptr); vkDestroyDescriptorSetLayout(dev, setLayout_, nullptr);
    vkDestroySampler(dev, sampler_, nullptr); vkDestroySampler(dev, depthSampler_, nullptr); vkDestroyDescriptorPool(dev, pool_, nullptr);
    ctx_ = nullptr;
}
void PostProcessing::record(VkCommandBuffer cmd, const Image& scene, const Image& depth, const Image& mask, bool maskSupported, Image& final, const PostSettings& P,
                            bool atmosphereOn, bool dofOn, const glm::mat4& proj, const glm::vec3& fogColor, float fogDensity, float time, int lutSize) {
    const VkImageAspectFlags col = VK_IMAGE_ASPECT_COLOR_BIT;
    ctx_->barrier(cmd, scene.image, col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (maskSupported) ctx_->barrier(cmd, mask.image, col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        ctx_->endRendering(cmd);
        if (toRead) ctx_->barrier(cmd, target.image, col, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    if (atmosphereOn || dofOn) {
        ctx_->barrier(cmd, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    if (atmosphereOn) {
        PostPC ap{};
        ap.a = glm::vec4(proj[0][0], proj[1][1], proj[2][2], proj[3][2]);
        ap.b = glm::vec4(fogColor, fogDensity);
        ap.c = glm::vec4((float)depth.samples, 0.f, 0.f, 0.f);
        pass(atmosphere_, VK_IMAGE_LAYOUT_UNDEFINED, atmospherePipe_, atmosphereSet_, ap, false, true);
    }
    if (dofOn) {
        PostPC dp{};
        dp.a = glm::vec4(proj[2][2], proj[3][2], P.focusDistance, std::max(P.focusRange, .01f));
        dp.b = glm::vec4(P.depthOfField, 1.f / width_, 1.f / height_, (float)depth.samples);
        pass(dof_, VK_IMAGE_LAYOUT_UNDEFINED, dofPipe_, atmosphereOn ? fogDofSet_ : dofSet_, dp, false, true);
    }
    const auto plan = planPost(P, width_, height_);
    bool bloomOn = plan.bloom;
    // prefilter (soft threshold) into level 0; when bloom is off the threshold is infinite so level 0 is black
    PostPC pf{glm::vec4(bloomOn ? P.bloomThreshold : 1e9f, P.bloomKnee, 1.f / width_, 1.f / height_), glm::vec4((float)P.bloomSelection, (P.enabled && P.materialBloomMask && maskSupported) ? 1.f : 0.f, 0, 0), glm::vec4(0), glm::vec4(0)};
    pass(bloom_[0], VK_IMAGE_LAYOUT_UNDEFINED, prefilter_, dofOn ? dofSceneSet_ : (atmosphereOn ? foggedSceneSet_ : sceneSet_), pf, false, true);
    if (bloomOn) {
        for (int i = 1; i < bloomLevels_; ++i) {
            PostPC pc{glm::vec4(1.f / bloom_[i - 1].width, 1.f / bloom_[i - 1].height, 0, 0), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
            pass(bloom_[i], VK_IMAGE_LAYOUT_UNDEFINED, down_, bloomSet_[i - 1], pc, false, true);
        }
        for (int i = bloomLevels_ - 2; i >= 0; --i) {
            PostPC pc{glm::vec4(1.f / bloom_[i + 1].width, 1.f / bloom_[i + 1].height, P.bloomRadius, 0), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
            pass(bloom_[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, up_, bloomSet_[i + 1], pc, true, true);
        }
    }
    bool halationOn = plan.halation;
    if (halationOn) {
        PostPC hp{};
        hp.a = glm::vec4(1.f / bloom_[0].width, 1.f / bloom_[0].height, 2.f, 0.f);
        pass(halation_, VK_IMAGE_LAYOUT_UNDEFINED, halationPipe_, bloomSet_[0], hp, false, true);
    }
    PostPC pc;
    bool en = P.enabled;
    pc.a = glm::vec4(bloomOn ? P.bloomIntensity : 0.f, P.exposure, en ? (float)P.tonemap : 0.f, en ? P.vignette : 0.f);
    pc.b = glm::vec4(en ? P.saturation : 1.f, en ? P.contrast : 1.f, en ? P.grain : 0.f, P.vignetteSoftness);
    pc.c = glm::vec4(en ? P.shadowTint : glm::vec3(1.f), time);
    pc.d = glm::vec4(en ? P.highlightTint : glm::vec3(1.f), (float)width_ / (float)height_);
    pc.e = glm::vec4(en ? P.lutStrength : 0.f, (float)lutSize, halationOn ? P.halation : 0.f, bloomOn ? P.lensFlare : 0.f);
    VkDescriptorSet comp = dofOn ? (halationOn ? dofCompSet_ : dofCompNoHalationSet_)
        : atmosphereOn ? (halationOn ? foggedCompSet_ : foggedCompNoHalationSet_)
        : (halationOn ? compSet_ : compNoHalationSet_);
    if (plan.finish) {
        pass(display_, VK_IMAGE_LAYOUT_UNDEFINED, composite_, comp, pc, false, true);
        PostPC fp{}; fp.a = glm::vec4(P.chromatic, P.ink, (float)P.colourLevels, P.painterly);
        pass(final, VK_IMAGE_LAYOUT_UNDEFINED, finishPipe_, finishSet_, fp, false, false);
    } else pass(final, VK_IMAGE_LAYOUT_UNDEFINED, composite_, comp, pc, false, false);
}

}
