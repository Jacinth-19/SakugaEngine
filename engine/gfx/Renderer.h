#pragma once
#include "gfx/Context.h"
#include "post/PostProcessing.h"
#include "gfx/LineBatch.h"
#include "gfx/Material.h"
#include "gfx/Mesh.h"
#include "asset/Lut.h"
#include "gfx/UiTheme.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>

struct GLFWwindow;

namespace sk {

struct PointLight { glm::vec3 position{0.f}; float range = 4.f; glm::vec3 color{1.f}; float intensity = 0.f; };   // intensity 0 = off
constexpr int kMaxPointLights = 4;
constexpr int kMaxContacts = 6;

struct LightingSettings {
    glm::vec3 direction{0.45f, 0.62f, 0.64f};     // direction TOWARDS the light (normalised by the renderer)
    glm::vec3 color{1.0f, 0.92f, 0.82f};
    float intensity = 0.95f;
    glm::vec3 sky{0.42f, 0.62f, 0.95f};
    glm::vec3 ground{0.34f, 0.30f, 0.30f};
    float ambient = 0.62f;
    glm::vec3 fogColor{0.78f, 0.86f, 0.97f};
    float fogDensity = 0.007f;
    glm::vec3 zenith{0.23f, 0.46f, 0.96f}, horizon{0.76f, 0.84f, 0.96f};   // procedural sky gradient (also feeds the procedural IBL)
    float cloudCover = 0.55f, cloudTime = 0.f, starVisibility = 0.f, sunDisc = 1.f;   // sky shader controls
    float sunSize = 1.f;
    bool nprOnly = false;                          // render every material with the pure NPR (Endfield) shading path
    float shadowBias = 0.0012f;
    float shadowFarRadius = 28.f;                 // second (far) cascade; both cascades share one 2x1 atlas
    glm::vec4 contacts[kMaxContacts]{};            // contact AO blobs under characters: xyz ground point, w radius (0 = unused)
    bool pointShadows = true;                      // each active point light renders six faces into the shadow atlas
    bool fitCascadeToCamera = true;              // near cascade = bounding sphere of the camera frustum slice (+ hero) instead of a box around the hero
    float shadowRadius = 5.0f;                    // half-extent of the shadow frustum around the focus point
    float wetPreviewTime = 0.f;                   // == _ZhuangfyWetPreviewTime (0 => use real time)
    glm::vec2 splitDir{0.f};                      // == _SplitLineDirX/Y   (zero disables the split view)
    float splitOffsetPx = 0.f;                    // == _SplitLineOffsetPx
    float outlineScale = 1.0f;
    PointLight points[kMaxPointLights];           // additive lights, each can use a cube shadow map
    float envIntensity = 1.0f;                    // image based lighting: diffuse SH + roughness-LOD reflections
    float envRotation = 0.f;                      // radians around +Y
};

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    Material* material = nullptr;
    glm::mat4 model{1.f};
    uint32_t boneOffset = 0;
    bool skinned = false;
    uint32_t morphWeightOffset = 0, morphCount = 0;   // index/count into FrameInput::morphWeights (count 0 = no morph targets)
    bool faceFrame = false;                       // per-entity head bone frame for face SDF
    glm::vec3 faceForward{0, 0, 1}, faceRight{1, 0, 0};
};

struct FrameInput {
    glm::mat4 view{1.f}, proj{1.f};               // proj must come from Renderer::perspective()
    glm::vec3 cameraPos{0.f};
    float time = 0.f;
    glm::vec3 shadowFocus{0.f};
    LightingSettings light;
    PostSettings post;
    std::vector<float> morphWeights;              // per draw: one weight per morph slot of the mesh
    std::vector<DrawItem> draws;
    std::vector<glm::mat4> bones;                 // skinning palette; DrawItem::boneOffset indexes into this
    std::vector<LineVertex> lines;                // line list (pairs of vertices)
};

class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;
#ifdef __ANDROID__
    // Smaller atlases lower mobile memory/bandwidth costs; desktop keeps its original quality.
    static constexpr uint32_t kShadowRes = 1024;
    static constexpr uint32_t kPointShadowRes = 256;
#else
    static constexpr uint32_t kShadowRes = 2048;
    static constexpr uint32_t kPointShadowRes = 512;
#endif
    static constexpr int kMaxPointShadows = kMaxPointLights;
    static constexpr uint32_t kMaxBones = 16384;
    static constexpr uint32_t kMaxLineVerts = 262144;
    static constexpr uint32_t kMaxMorphVec4 = 1u << 21;      // 32 MB of morph deltas
    static constexpr uint32_t kMaxMorphWeights = 1u << 14;

    bool init(Context& ctx, GLFWwindow* window, uint32_t width, uint32_t height, const std::string& shaderDir);
#ifdef __ANDROID__
    bool initAndroid(Context& ctx, ANativeWindow* window, uint32_t width, uint32_t height, const std::string& shaderDir);
#endif
    void shutdown();

    // --- resources
    GpuMesh* createMesh(const MeshData& data);
    Texture* createTexture(const std::string& name, const uint8_t* rgba, uint32_t w, uint32_t h, bool srgb, bool mips = true, bool repeat = true);
    Material* createMaterial(const std::string& name);
    // Uploads position deltas (one vector per slot, one entry per vertex) of a mesh into the shared morph buffer.
    bool setMorphTargets(GpuMesh* mesh, const std::vector<int>& targetIndices, const std::vector<std::vector<glm::vec3>>& deltas);
    void flushMaterial(Material* m);              // push params + textures to the GPU (call after edits)
    // Colour grading LUT for the post pass (applied after tonemap / sRGB). Takes effect at the start of the next frame.
    void setPostLut(const LutStrip& lut) { pendingLut_ = lut; hasPendingLut_ = true; }
    int postLutSize() const { return lutSize_; }
    bool supportsPostAtmosphere() const { return sampleSceneDepth_; }
    Texture* whiteTex() const { return white_; }
    Texture* blackTex() const { return black_; }
    Texture* bumpTex() const { return bump_; }

    // --- image based lighting
    // Uploads an equirectangular RGB float image (linear HDR), builds the mip chain used for roughness lookups and 9 SH coefficients for diffuse.
    void setEnvironmentHDR(const float* rgb, int w, int h);
    void useProceduralEnvironment() { envHDR_ = false; procHash_ = 0; }
    bool environmentIsHDR() const { return envHDR_; }
    glm::vec3 environmentSunDirection() const { return envSun_; }   // brightest region of the loaded HDR (zero if none)

    // --- UI
    // Creates the ImGui context, loads a font (the system UI font when `fontPath` is empty - see uiFontCandidates())
    // and applies `theme` (gfx/UiTheme.h). Must be called before the first frame.
    void initUI(GLFWwindow* window, const UiTheme& theme = UiTheme{}, const std::string& fontPath = {},
                float fontPx = 15.f);
    void beginUI(float deltaTime = 1.f / 60.f);  // starts an ImGui frame
    bool uiEnabled() const { return ui_; }
    void setUIVisible(bool v) { uiVisible_ = v; }
    // Applies a theme to the running UI (F12 in the sandbox edits the live style, this pushes a saved one back).
    void setUiTheme(const UiTheme& theme);
    const UiTheme& uiTheme() const { return uiTheme_; }
    // Candidate system UI fonts, in preference order, for the platform this was built for (may be empty).
    static std::vector<std::string> uiFontCandidates();

    // --- rendering
    // Windowed: draws and presents. If `capture` is set, also reads back the final RGBA8 image.
    // Headless (window == nullptr): renders to the offscreen target and reads it back into `capture`.
    bool renderFrame(const FrameInput& in, std::vector<uint8_t>* capture = nullptr);
    void resize(uint32_t w, uint32_t h);
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    float aspect() const { return (float)width_ / (float)height_; }
    static glm::mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar);
    static bool savePNG(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);

    struct Stats { uint32_t pointShadowLights = 0, pointShadowFaces = 0; uint32_t drawCalls = 0, triangles = 0; float cpuMs = 0; float gpuBloomLevels = 0; } stats;

private:
    struct FrameData;
    bool presenting() const;
    void framebufferSize(int& w, int& h) const;
    void createTargets();
    void destroyTargets();
    void createSwapchain();
    void destroySwapchain();
    void createPipelines(const std::string& shaderDir);
    void recordScene(VkCommandBuffer cmd, FrameData& f, const FrameInput& in, uint32_t drawCount);
    enum class DrawFilter { Opaque, Transparent, Shadow, Outline };
    int cascadeIdx_ = 0;
    void drawList(VkCommandBuffer cmd, const FrameInput& in, DrawFilter filter);
    VkPipeline makePipeline(VkShaderModule vs, VkShaderModule fs, int vertexKind /*0 none,1 mesh,2 line*/, VkCullModeFlags cull, bool depthTest, bool depthWrite,
                            VkSampleCountFlagBits samples, bool hasColor, VkFormat colorFmt, bool depthBias, VkPrimitiveTopology topo, bool blend);

    Context* ctx_ = nullptr;
    GLFWwindow* window_ = nullptr;
#ifdef __ANDROID__
    ANativeWindow* androidWindow_ = nullptr; // borrowed; lifetime is owned by the caller
#endif
    uint32_t width_ = 0, height_ = 0;
    int surfaceWidth_ = 0, surfaceHeight_ = 0;
    bool ui_ = false, uiVisible_ = true, uiFrameOpen_ = false;
    UiTheme uiTheme_;

    // swapchain
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage> swapImages_;
    std::vector<VkSemaphore> renderDone_;
    bool needResize_ = false;

    // render targets
    Image msaaColor_, msaaDepth_, sceneColor_, finalColor_, shadow_, pointShadow_;
    VkFormat colorFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;     // HDR scene format
    VkFormat finalFormat_ = VK_FORMAT_R8G8B8A8_UNORM;          // display-referred output (tonemapped, sRGB encoded, UI on top)
    PostProcessing postProcessing_;
    bool sampleSceneDepth_ = false;
    LutStrip pendingLut_; bool hasPendingLut_ = false; Texture* lutTex_ = nullptr; int lutSize_ = 16;
    Buffer morphDeltas_; uint32_t morphCursor_ = 0;
    Buffer readback_;

    // descriptors / pipelines
    VkDescriptorSetLayout globalLayout_ = VK_NULL_HANDLE, materialLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE, envSampler_ = VK_NULL_HANDLE;
    Image env_;
    bool envHDR_ = false; uint64_t procHash_ = 0; glm::vec3 envSun_{0.f};
    glm::vec4 sh_[9]{};
    float envMaxLod_ = 0.f;
    void uploadEnvironment(const std::vector<float>& rgba, int w, int h);
    void updateProceduralEnvironment(const LightingSettings& L);
    VkPipeline pipeToon_ = VK_NULL_HANDLE, pipeOutline_ = VK_NULL_HANDLE, pipeShadow_ = VK_NULL_HANDLE,
               pipeSky_ = VK_NULL_HANDLE, pipeLine_ = VK_NULL_HANDLE, pipeToonBlend_ = VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules_;

    struct FrameData {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        Buffer globalUBO, bones, lines, morphWeights;
        VkDescriptorSet globalSet = VK_NULL_HANDLE;
    };
    FrameData frames_[kFramesInFlight];
    uint32_t frameIndex_ = 0;

    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::vector<std::unique_ptr<Texture>> textures_;
    std::vector<std::unique_ptr<Material>> materials_;
    Texture *white_ = nullptr, *black_ = nullptr, *bump_ = nullptr;
};

}  // namespace sk
