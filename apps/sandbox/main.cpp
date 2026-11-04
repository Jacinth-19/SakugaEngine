// Sakuga Sandbox -- demo / test-bed for the engine:
//   * toon shader test stage (VRM character driven by motion matching, glTF skinned props, shader lab spheres)
//   * Unity-style input actions, ImGui material inspector, headless screenshot mode
#include "anim/Bvh.h"
#include "anim/MotionMatching.h"
#include "asset/GltfLoader.h"
#include "asset/Lut.h"
#include "asset/ProceduralTextures.h"
#include "core/FileUtil.h"
#include "core/Log.h"
#include "editor/MaterialInspector.h"
#include "gfx/Renderer.h"
#include "platform/Input.h"
#include "platform/SysInfo.h"
#include "platform/Window.h"
#include "platform/TouchControls.h"
#include "platform/FrameTiming.h"
#include "scene/Scene.h"
#include "MotionLab.h"
#ifdef __ANDROID__
#include "MobileSandbox.h"
#include "platform/SurfaceStateStore.h"
#include "platform/MobilePointerRouter.h"
#include "platform/MobileImGuiHitTest.h"
#include <android/native_window.h>
#include <android/log.h>
#include <cfloat>
#endif

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <set>
#include <fstream>

using namespace sk;
namespace fs = std::filesystem;

// ----------------------------------------------------------------------------------------- options
struct ScriptKey { float t; glm::vec2 move; bool sprint, crouch; };
struct Options {
    bool headless = false, validation = false, ui = true, outlines = true, mm = true, skeleton = false, trajectory = false;
    uint32_t width = 1600, height = 900;
    int frames = 120;
    std::string screenshot, recordDir;
    int recordEvery = 2;
    int showcase = -1, debugMode = 0;   // -1 = the Motion lab
    float timeOfDay = 15.f, todSpeed = 0.5f; bool todAuto = false, npr = false, navDebug = false, agentDebug = false, mmDebug = false, npcs = true, hands = true, lookAt = true, springs = true, postOff = false;
    bool fitCascade = true, bodyMask = true, pointShadows = true, performance = false; float cloud = 0.66f, exposure = 1.f;
    float wet = 0.f, cameraYaw = glm::pi<float>(), cameraPitch = 0.16f, cameraDist = 3.6f, lightAz = 40.f, lightEl = 38.f;
    float fixedDt = 1.f / 60.f;
    float splitOffset = 0.f; bool split = false;
    bool tonemap = false, touch = false;
    // Posture CLI
    float postureSpine = 0.f, postureChest = 0.f, postureNeck = 0.f, postureHead = 0.f;
    float postureHunch = 0.f, postureWeight = 1.f;
    // ZZZ spring tuning (post slider)
    float springTuningStiffness = 1.f, springTuningDrag = 1.f, springTuningGravity = 1.f, springTuningColliderRadius = 0.f;
    float timeStart = 0.f;
    std::vector<ScriptKey> script;
    std::vector<std::string> motionDirs; int mmClips = 0; long mmMask = -1; bool mmCache = true, mmContext = true; std::string playClip, env; float envIntensity = 1.f; bool ik = true, lightSet = false;
    std::string lut = "Anime Shinkai"; float lutStrength = 0.7f; bool hdriSun = false;
    std::string uiTheme, uiFont; float uiFontSize = 15.f;   // gfx/UiTheme.h preset name or a JSON file (default: config/ui_theme.json)
};

static void parseScript(const std::string& s, Options& o) {   // "t:x,y[,sprint|crouch];..."
    std::stringstream ss(s); std::string item;
    while (std::getline(ss, item, ';')) {
        ScriptKey k{0, {0, 0}, false, false}; char c;
        std::stringstream is(item); is >> k.t >> c >> k.move.x >> c >> k.move.y;
        std::string flag; if (std::getline(is, flag, ',') && std::getline(is, flag)) { k.sprint = flag == "sprint"; k.crouch = flag == "crouch"; }
        o.script.push_back(k);
    }
}

static Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--headless") o.headless = true;
        else if (a == "--validation") o.validation = true;
        else if (a == "--performance") {
            // An opt-in lighter starting point; later width/height flags can override it.
            o.performance = true; o.width = 1280; o.height = 720;
            o.npcs = false; o.springs = false; o.pointShadows = false;
        }
        else if (a == "--no-ui") o.ui = false;
        else if (a == "--no-outline") o.outlines = false;
        else if (a == "--no-mm") o.mm = false;
        else if (a == "--skeleton") o.skeleton = true;
        else if (a == "--trajectory") o.trajectory = true;
        else if (a == "--tonemap") o.tonemap = true;
        else if (a == "--width") o.width = (uint32_t)std::atoi(next());
        else if (a == "--height") o.height = (uint32_t)std::atoi(next());
        else if (a == "--frames") o.frames = std::atoi(next());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--record") o.recordDir = next();
        else if (a == "--record-every") o.recordEvery = std::atoi(next());
        else if (a == "--showcase") o.showcase = std::atoi(next());
        else if (a == "--debug-mode") o.debugMode = std::atoi(next());
        else if (a == "--wet") o.wet = (float)std::atof(next());
        else if (a == "--camera") { std::sscanf(next(), "%f,%f,%f", &o.cameraYaw, &o.cameraPitch, &o.cameraDist); o.cameraYaw = glm::radians(o.cameraYaw); o.cameraPitch = glm::radians(o.cameraPitch); }
        else if (a == "--light") { std::sscanf(next(), "%f,%f", &o.lightAz, &o.lightEl); o.lightSet = true; }
        else if (a == "--split") { o.split = true; o.splitOffset = (float)std::atof(next()); }
        else if (a == "--script") parseScript(next(), o);
        else if (a == "--motion-dir") o.motionDirs.push_back(next());
        else if (a == "--mm-clips") o.mmClips = std::atoi(next());
        else if (a == "--mm-categories") { std::string v = next(); o.mmMask = v == "all" ? (long)((1u << kMotionCategoryCount) - 1) : std::atol(v.c_str()); }
        else if (a == "--no-mm-context") o.mmContext = false;
        else if (a == "--ui-theme") o.uiTheme = next();
        else if (a == "--ui-font") o.uiFont = next();
        else if (a == "--ui-font-size") o.uiFontSize = (float)std::atof(next());
        else if (a == "--no-mm-cache") o.mmCache = false;
        else if (a == "--env") o.env = next();
        else if (a == "--env-intensity") o.envIntensity = (float)std::atof(next());
        else if (a == "--no-ik") o.ik = false;
        else if (a == "--time") o.timeOfDay = (float)std::atof(next());
        else if (a == "--time-auto") o.todAuto = true;
        else if (a == "--time-speed") o.todSpeed = (float)std::atof(next());
        else if (a == "--cloud") o.cloud = (float)std::atof(next());
        else if (a == "--lut") o.lut = next();
        else if (a == "--no-body-mask") o.bodyMask = false;
        else if (a == "--no-point-shadows") o.pointShadows = false;
        else if (a == "--no-fit-cascade") o.fitCascade = false;
        else if (a == "--posture-spine") o.postureSpine = std::stof(next());
        else if (a == "--posture-chest") o.postureChest = std::stof(next());
        else if (a == "--posture-neck") o.postureNeck = std::stof(next());
        else if (a == "--posture-head") o.postureHead = std::stof(next());
        else if (a == "--posture-hunch") o.postureHunch = std::stof(next());
        else if (a == "--posture-weight") o.postureWeight = std::stof(next());
        else if (a == "--spring-stiffness") o.springTuningStiffness = std::stof(next());
        else if (a == "--spring-drag") o.springTuningDrag = std::stof(next());
        else if (a == "--spring-gravity") o.springTuningGravity = std::stof(next());
        else if (a == "--spring-collider-radius") o.springTuningColliderRadius = std::stof(next());
        else if (a == "--lut-strength") o.lutStrength = (float)std::atof(next());
        else if (a == "--hdri-sun") o.hdriSun = true;
        else if (a == "--exposure") o.exposure = (float)std::atof(next());
        else if (a == "--npr") o.npr = true;
        else if (a == "--nav-debug") { o.navDebug = true; o.agentDebug = true; }
        else if (a == "--mm-debug") o.mmDebug = true;
        else if (a == "--no-npcs") o.npcs = false;
        else if (a == "--no-hand-ik") o.hands = false;
        else if (a == "--no-look-at") o.lookAt = false;
        else if (a == "--no-springs") o.springs = false;
        else if (a == "--no-post") o.postOff = true;
        else if (a == "--touch") o.touch = true;
        else if (a == "--play-clip") o.playClip = next();
        else if (a == "--help") {
            std::printf("sandbox [--headless] [--screenshot out.png] [--frames N] [--width W --height H] [--showcase 0..4 (default 4 = Motion lab)]\n"
                        "        [--performance] [--debug-mode 0..13] [--wet 0..1] [--camera yawDeg,pitchDeg,dist] [--light azDeg,elDeg]\n"
                        "        [--script \"t:x,y[,sprint];...\"] [--record dir --record-every N] [--no-ui] [--no-outline] [--no-mm]\n"
                        "        [--skeleton] [--trajectory] [--split pxOffset] [--tonemap] [--validation]\n"
                        "        [--motion-dir DIR]... [--mm-clips N] [--mm-categories all|bitmask] [--no-mm-context] [--no-mm-cache] [--play-clip clipName]\n"
                        "        [--env file.hdr|none] [--env-intensity X] [--no-ik] [--no-hand-ik] [--no-look-at] [--no-springs] [--no-npcs]\n"
                        "        [--time HOURS] [--time-auto] [--time-speed H/s] [--cloud 0..1] [--exposure X] [--npr] [--nav-debug] [--mm-debug] [--no-post]\n"
                        "        [--lut \"Anime Shinkai\"|none|file.cube] [--lut-strength 0..1] [--hdri-sun]\n"
                        "        [--ui-theme \"Sakuga Dark\"|\"Sakuga Light\"|\"Midnight\"|\"Monochrome\"|theme.json]\n"
                        "        [--ui-font file.ttf] [--ui-font-size px]\n"
                        "  In the window: F11 = ImGui demo, F12 = UI theme editor (live style editor + Save to JSON),\n"
                        "  F1 = hide the UI, F2 = screenshot, F3 = FPS counter, Tab = release the mouse.\n");
            std::exit(0);
        }
    }
    return o;
}

#ifdef __ANDROID__
// No Vulkan handles, scene pointers or navmesh objects survive surface teardown.
struct MobileEntityState {
    std::string name, modelPath;
    glm::vec3 pos{0}; float yaw = 0, scale = 1;
    int clip = -1; float clipTime = 0, clipSpeed = 1; bool loop = true, autoBlink = true;
    FootIKSettings ik; HandIKSettings hands; LookAtSettings look;
    SpringSettings spring; SpringTuning springTuning; PostureControl posture;
    BlinkClock blink; std::vector<float> exprTarget, exprCur, exprFinal;
    bool hasMatcher = false, browsing = false; int browserClip = -1;
    MMSettings matcher;
    std::vector<std::pair<std::string, MaterialParams>> materials;
};
struct MobileSnapshot {
    Options options;
    OrbitCamera camera; LightingSettings light; PostSettings post; UiTheme theme;
    std::string themeName, ini, environment;
    std::vector<MobileEntityState> entities;
    int showcase = 0, selectedMat = 0, lutIdx = -1, debugMode = 0, matForcedMode = -1;
    float tod = 0, todSpeed = 0, userExposure = 1, userEnv = 1, cloud = 0, cameraShoulder = 0;
    float lutStrength = 0, lightAz = 0, lightEl = 0, time = 0, rain = 0, splitOffset = 0, touchScale = 1;
    bool todAuto = false, todEnabled = false, hdriMode = false, nprFull = false, lampsOn = false;
    bool pointLightsOn = false, navDebug = false, agentDebug = false, mmDebug = false, showFps = false;
    bool hdriSun = false, lightAuto = false, rainOn = false, skeleton = false, trajectory = false;
    bool showUI = false, split = false, syncMat = false, showTheme = false, showDemo = false, npcs = false;
    bool touchEnabled = true, touchMouse = true;
};
static SurfaceStateStore<MobileSnapshot> mobileSnapshots;
#endif

// ----------------------------------------------------------------------------------------- sandbox
class Sandbox {
public:
    Sandbox(const Options& o, const std::string& root, const std::string& shaders) : opt_(o), root_(root), shaderDir_(shaders) {}
    bool init();
    void run();
    void shutdown();
#ifdef __ANDROID__
    bool initMobile(ANativeWindow* window, std::string& error);
    int frameMobile(float dt, float wallDt, float x, float y, int clip);
    void touchMobile(bool imgui, int action, int id, float x, float y);
    MobileSnapshot captureMobile() const;
    void restoreMobile(const MobileSnapshot& state);
#endif

private:
    struct Showcase;
    void loadAssets();
    void loadNpcModels();
    void spawnNpcs(Showcase& sc);
    void loadEnvironment(const std::string& path);
    void buildLab();
    void buildMotionLab();
    void applyTimeOfDay();
    void updateCameraAndGaze(float dt, bool lab);
    void drawCharacterUI();
    void setShowcase(int idx);
    void handleActions(float dt);
    void updateScene(float dt);
    FrameInput buildFrame(float dt);
    void drawUI(float wallDt = 1.f / 60.f);
    void drawUiThemeWindow();
    void loadUiTheme();
    void applyRain(float amount);
    std::vector<Material*> activeMaterials();

    Options opt_; std::string root_, shaderDir_;
    Context ctx_; Renderer renderer_; Window window_; InputSystem input_; StyleTextures style_;
#ifdef __ANDROID__
    ANativeWindow* mobileWindow_ = nullptr;
    MobilePointerRouter mobilePointers_;
    std::string mobileError_;
    bool mobileLoggedControls_ = false, mobileLoggedMove_ = false, mobileLoggedPointShadows_ = false;
    bool mobileClipBrowser_ = false, mobileBrowserTargetLogged_ = false, mobileLastItemLogged_ = false; int mobileClipPage_ = 0;
    bool mobileLoadNpcs_ = false, mobileNpcsRequested_ = false, mobileNpcsPendingFrame_ = false, mobileRestored_ = false;
#endif
    struct Showcase { std::string name; std::vector<Entity*> entities; Entity* hero = nullptr; bool lab = false; };
    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<Showcase> showcases_;
    int showcase_ = 0;
    bool pointLightsOn_ = true; std::string envName_ = "procedural", envPath_ = "procedural";
    Entity* ground_ = nullptr; Entity* vrm_ = nullptr;
    OrbitCamera camera_;
    MotionLab lab_;
    std::vector<std::shared_ptr<Model>> npcModels_;
    // time of day / look
    float tod_ = 15.f, todSpeed_ = 0.5f; bool todAuto_ = false, todEnabled_ = true, hdriMode_ = false, nprFull_ = false;
    float userExposure_ = 1.f, userEnv_ = 1.f, cloud_ = 0.55f; bool lampsOn_ = true;
    bool navDebug_ = false, agentDebug_ = false, mmDebug_ = false, showFps_ = true;
    // Posture CLI
    float postureSpine = 0.f, postureChest = 0.f, postureNeck = 0.f, postureHead = 0.f;
    float postureHunch = 0.f, postureWeight = 1.f;
    TouchControls touch_; bool touchMouse_ = true;
    float cameraShoulder_ = 0.28f, camUsed_ = -1.f, todSun_ = 40.f; glm::vec3 camSmooth_{0};
    LightingSettings light_; PostSettings post_;
    std::vector<std::string> lutNames_, lutPaths_; int lutIdx_ = -1; float lutStrength_ = 0.7f; bool hdriSun_ = false; float envSunAz_ = 0.f;
    void scanLuts(); void setLut(int idx);
    float lightAz_ = 40, lightEl_ = 38; bool lightAuto_ = false;
    float time_ = 0, rain_ = 0; bool rainOn_ = false;
    int debugMode_ = 0; bool skeleton_ = false, trajectory_ = false, showUI_ = true, split_ = false; float splitOffset_ = 0;
    glm::vec3 desiredVel_{0};
    int selectedMat_ = 0; bool syncMat_ = true;
    float fps_ = 0; FrameTiming frameTiming_; bool cursorFree_ = true;
    int matForcedMode_ = -1;
    // UI look: the preset/file this session started from, and the live state the F12 editor writes back to it
    UiTheme uiTheme_; std::string uiThemePath_, uiThemeError_; fs::file_time_type uiThemeStamp_{};
    bool showThemeEditor_ = false, showImGuiDemo_ = false; char themeName_[64] = "Sakuga Dark";
    int frame_ = 0;
    bool screenshotRequested_ = false;
};

bool Sandbox::init() {
    bool windowed = !opt_.headless;
#ifdef __ANDROID__
    if (!mobileWindow_ || !ctx_.initAndroid(mobileWindow_, mobileError_)) return false;
    if (!renderer_.initAndroid(ctx_, mobileWindow_, ANativeWindow_getWidth(mobileWindow_), ANativeWindow_getHeight(mobileWindow_), shaderDir_)) return false;
#else
    if (windowed) {
        if (!window_.create((int)opt_.width, (int)opt_.height, "Sakuga Engine - Sandbox")) return false;
    }
    if (!ctx_.init(windowed, opt_.validation)) return false;
    if (!renderer_.init(ctx_, windowed ? window_.handle() : nullptr, opt_.width, opt_.height, shaderDir_)) return false;
#endif
    loadUiTheme();
    if (windowed && opt_.ui) renderer_.initUI(window_.handle(), uiTheme_, opt_.uiFont, opt_.uiFontSize);
    if (!input_.loadActions(root_ + "/config/InputSystem_Actions.inputactions.json")) return false;
    loadAssets();
    buildLab();
    camera_.yaw = opt_.cameraYaw; camera_.pitch = opt_.cameraPitch; camera_.distance = opt_.cameraDist;
    lightAz_ = opt_.lightAz; lightEl_ = opt_.lightEl; debugMode_ = opt_.debugMode; skeleton_ = opt_.skeleton; trajectory_ = opt_.trajectory;
    split_ = opt_.split; splitOffset_ = opt_.splitOffset; post_.tonemap = opt_.tonemap ? 2 : 1; showUI_ = opt_.ui;
    tod_ = opt_.timeOfDay; todSpeed_ = opt_.todSpeed; todAuto_ = opt_.todAuto; cloud_ = opt_.cloud; userExposure_ = opt_.exposure; nprFull_ = opt_.npr;
    navDebug_ = opt_.navDebug; agentDebug_ = opt_.agentDebug; mmDebug_ = opt_.mmDebug; post_.enabled = !opt_.postOff;
    if (opt_.performance) post_.bloom = false;
    time_ = opt_.timeStart;
    hdriSun_ = opt_.hdriSun; lutStrength_ = opt_.lutStrength;
    scanLuts();
    if (opt_.lut != "none") {
        int idx = -1; for (int i = 0; i < (int)lutNames_.size(); ++i) if (lutNames_[i] == opt_.lut) idx = i;
        if (idx < 0 && fs::exists(opt_.lut)) { lutNames_.push_back(fs::path(opt_.lut).stem().string()); lutPaths_.push_back(opt_.lut); idx = (int)lutNames_.size() - 1; }
        setLut(idx);
    }
    {   // image based lighting: --env file.hdr | none ; default = the bundled Poly Haven sky when present
        // default: procedural sky + procedural IBL driven by the time of day. --env file.hdr switches to image based lighting from that HDRI.
        if (!opt_.env.empty() && opt_.env != "none") { loadEnvironment(opt_.env); hdriMode_ = true; }
        userEnv_ = opt_.envIntensity; light_.envIntensity = opt_.envIntensity;
    }

    setShowcase(opt_.showcase < 0 ? (int)showcases_.size() - 1 : opt_.showcase);
    if (opt_.wet > 0) { rainOn_ = true; rain_ = opt_.wet; }
#ifndef __ANDROID__
    if (windowed) { window_.setCursorCaptured(false); window_.loadGamepadMappings(root_ + "/config/gamecontrollerdb.txt"); touch_.enabled = opt_.touch; }
#endif
    return true;
}

void Sandbox::scanLuts() {
    lutNames_.clear(); lutPaths_.clear();
    std::error_code ec; std::vector<std::pair<std::string, std::string>> v;
    for (auto& f : fs::directory_iterator(root_ + "/assets/luts", ec)) {
        std::string ext = f.path().extension().string(); for (auto& c : ext) c = (char)std::tolower(c);
        if (ext == ".cube" || ext == ".3dl" || ext == ".png") v.push_back({f.path().stem().string(), f.path().string()});
    }
    std::sort(v.begin(), v.end());
    for (auto& p : v) { lutNames_.push_back(p.first); lutPaths_.push_back(p.second); }
}
void Sandbox::setLut(int idx) {
    lutIdx_ = -1;
    if (idx < 0 || idx >= (int)lutPaths_.size()) return;
    LutStrip l;
    if (!loadLutFile(lutPaths_[idx], l)) { SK_WARN("cannot load LUT %s", lutPaths_[idx].c_str()); return; }
    renderer_.setPostLut(l); lutIdx_ = idx;
}

void Sandbox::loadEnvironment(const std::string& path) {
    if (path == "none" || path == "procedural") { renderer_.useProceduralEnvironment(); envName_ = "procedural"; envPath_ = "procedural"; return; }
    std::vector<float> rgb; int w = 0, h = 0;
    if (!loadHdrImage(path, rgb, w, h)) { SK_WARN("cannot load environment %s, using the procedural sky", path.c_str()); renderer_.useProceduralEnvironment(); envName_ = "procedural"; envPath_ = "procedural"; return; }
    renderer_.setEnvironmentHDR(rgb.data(), w, h);
    envPath_ = path;
    envName_ = fs::path(path).stem().string();
    glm::vec3 sun = renderer_.environmentSunDirection();
    envSunAz_ = glm::length(sun) > 0.5f ? std::atan2(sun.x, sun.z) : 0.f;
    if (glm::length(sun) > 0.5f && hdriSun_ && !opt_.lightSet) { todEnabled_ = false; lightAz_ = glm::degrees(std::atan2(sun.x, sun.z)); lightEl_ = std::clamp(glm::degrees(std::asin(sun.y)), 15.f, 80.f); }
    SK_LOG("environment %s (%dx%d), sun az %.0f el %.0f", envName_.c_str(), w, h, lightAz_, lightEl_);
}

void Sandbox::loadAssets() {
    style_ = createStyleTextures(renderer_);
    GltfOptions go; go.outlines = opt_.outlines; go.bodyUnderMask = opt_.bodyMask ? 0.006f : 0.f;
#ifdef __ANDROID__
    go.maxTextureSize = 1024;
#endif
    auto addEntity = [&](std::shared_ptr<Model> m, const char* name) -> Entity* {
        auto e = std::make_unique<Entity>(); e->name = name; e->init(std::move(m)); entities_.push_back(std::move(e)); return entities_.back().get();
    };

    // ground
    {
        Material* gm = renderer_.createMaterial("ground");
        gm->tex[TEX_BASE] = style_.checker; gm->tex[TEX_RAMP] = style_.ramp; gm->tex[TEX_LUT] = style_.lut; gm->tex[TEX_SPEC_RAMP] = style_.specRamp;
        gm->params.outlineAlpha = 0; gm->params.rimIntensity = 0; gm->params.matCapStrength = 0; gm->params.specRampStrength = 0.f;
        gm->params.metallicScale = 0; gm->params.shadowColor = glm::vec4(0.66f, 0.62f, 0.82f, 1.f); gm->params.rampBlend = 0.9f;
        gm->params.realtimeShadowStrength = 1.f; gm->params.wetClothMask = 0.3f; gm->params.wetGlossMask = 0.5f; gm->params.doubleSided = 1.f;
        gm->params.occlusionStrength = 0; gm->params.lutStrength = 0.15f; gm->params.baseColor = glm::vec4(0.90f, 0.92f, 0.97f, 1.f); gm->params.pbrBaseBlend = 0.5f; gm->params.baseTexST = glm::vec4(1, 1, 0, 0);
        renderer_.flushMaterial(gm);
        ground_ = addEntity(makePrimitiveModel(renderer_, "ground", MeshData::plane(60.f, 30.f), gm), "ground");
    }

    // VRM hero (motion matching)
    std::string vrmPath = root_ + "/assets/models/AvatarSample.vrm";
    if (fs::exists(vrmPath)) {
        if (std::shared_ptr<Model> m = loadGltfModel(renderer_, style_, vrmPath, go)) {
            vrm_ = addEntity(m, "Avatar (VRM)"); vrm_->ik.enabled = opt_.ik;
            vrm_->hands.enabled = opt_.hands; vrm_->look.enabled = opt_.lookAt; vrm_->spring.enabled = opt_.springs;
            vrm_->posture.spine = opt_.postureSpine; vrm_->posture.chest = opt_.postureChest;
            vrm_->posture.neck = opt_.postureNeck; vrm_->posture.head = opt_.postureHead;
            vrm_->posture.hunchCorrection = opt_.postureHunch; vrm_->posture.weight = opt_.postureWeight;
            vrm_->springTuning.stiffness = opt_.springTuningStiffness; vrm_->springTuning.drag = opt_.springTuningDrag;
            vrm_->springTuning.gravity = opt_.springTuningGravity; vrm_->springTuning.colliderRadiusPad = opt_.springTuningColliderRadius;
            if (opt_.mm && m->isVRM) {
                // every *.bvh under assets/motion (recursive), $SAKUGA_MOTION_DIR and --motion-dir (e.g. the Assets/lafan1 folder of the Unity project)
                std::vector<std::string> dirs = {root_ + "/assets/motion"}, files;
                const char* home = std::getenv("HOME");
#ifdef _WIN32
                if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
                if (home && !*home) home = nullptr;
                if (const char* e = std::getenv("SAKUGA_MOTION_DIR")) dirs.push_back(e);
                if (home) dirs.push_back(std::string(home) + "/.cache/lafan1");   // where tools/fetch_assets.sh puts the full dataset
                for (auto& d : opt_.motionDirs) dirs.push_back(d);
                std::set<std::string> seen;
                for (auto& d : dirs) {
                    std::error_code ec; if (!fs::exists(d, ec)) continue;
                    for (auto& f : fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec))
                        if (f.path().extension() == ".bvh" && seen.insert(f.path().stem().string()).second) files.push_back(f.path().string());
                }
                SK_LOG("mm: %zu BVH files found", files.size());
#ifdef __ANDROID__
                (void)home;
                std::string cacheDir = opt_.mmCache ? root_ + "/user/mm_cache" : "";
#else
                std::string cacheDir = opt_.mmCache ? (home ? std::string(home) + "/.cache/sakuga" : root_ + "/.cache") : "";
#endif
                auto db = std::make_shared<MotionDatabase>(); std::string err;
                if (!files.empty() && db->buildFromFiles(m->rig, files, cacheDir, opt_.mmClips, &err)) {
                    {   // report the library so it is obvious when only a subset was found
                        std::string cats;
                        for (int c = 0; c < kMotionCategoryCount; ++c) {
                            int n = db->clipsInCategory(c);
                            if (n) { if (!cats.empty()) cats += ", "; cats += std::string(kMotionCategoryNames[c]) + " x" + std::to_string(n); }
                        }
                        SK_LOG("mm: %zu clips / %d frames in use -> %s", db->clipNames.size(), db->frameCount, cats.c_str());
                        if (db->clipNames.size() <= 2) SK_WARN("mm: only %zu clip(s) found - run tools/fetch_assets.sh for the full 77-clip LAFAN1 set", db->clipNames.size());
                    }
#ifdef __ANDROID__
                    __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox motion library clips=%zu frames=%d", db->clipNames.size(), db->frameCount);
#endif
                    vrm_->mmDb = db; vrm_->mm = std::make_unique<MotionMatcher>(); vrm_->mm->init(&m->rig, db.get());
                    if (opt_.mmMask >= 0) vrm_->mm->settings.categoryMask = (uint32_t)opt_.mmMask;
                    vrm_->mm->settings.contextMask = opt_.mmContext;
                    if (!opt_.playClip.empty()) {
                        for (size_t c = 0; c < db->clipNames.size(); ++c) if (db->clipNames[c] == opt_.playClip) vrm_->mm->playClip((int)c, vrm_->pos, vrm_->yaw);
                    }
                } else SK_WARN("motion matching disabled: %s", err.c_str());
            }
        }
    } else SK_WARN("missing %s", vrmPath.c_str());

    // skinned glTF props
    struct Prop { const char* file; const char* name; float scale; int clip; };
    for (Prop p : {Prop{"Fox.glb", "Fox (glTF)", 0.012f, 2}, Prop{"CesiumMan.glb", "CesiumMan (glTF)", 1.0f, 0}}) {
        std::string path = root_ + "/assets/models/" + p.file;
        if (!fs::exists(path)) continue;
        if (std::shared_ptr<Model> m = loadGltfModel(renderer_, style_, path, go)) {
            Entity* e = addEntity(m, p.name); e->scale = p.scale;
            if (!m->clips.empty()) e->clip = std::min<int>(p.clip, (int)m->clips.size() - 1);
        }
    }
    if (opt_.npcs) loadNpcModels();
}

void Sandbox::loadNpcModels() {
    if (!vrm_ || !vrm_->mm || !npcModels_.empty()) return;
    GltfOptions go; go.outlines = opt_.outlines; go.bodyUnderMask = opt_.bodyMask ? 0.006f : 0.f;
#ifdef __ANDROID__
    go.maxTextureSize = 1024;
#endif
    for (const char* f : {"Seed-san.vrm", "three-vrm-girl.vrm"}) {
        std::string path = root_ + "/assets/models/" + f;
        if (!fs::exists(path)) continue;
        if (std::shared_ptr<Model> m = loadGltfModel(renderer_, style_, path, go)) {
            if (m->rig.isHumanoid()) npcModels_.push_back(m);
            else SK_WARN("%s: no humanoid rig, skipped", f);
        }
    }
}

void Sandbox::buildLab() {
    auto mk = [&](const char* name, glm::vec4 base) {
        Material* m = renderer_.createMaterial(name);
        m->params.baseColor = base; m->params.outlineAlpha = opt_.outlines ? 1.f : 0.f; m->params.outlineWidth = 0.012f;
        m->tex[TEX_RAMP] = style_.ramp; m->tex[TEX_SPEC_RAMP] = style_.specRamp; m->tex[TEX_LUT] = style_.lut; m->tex[TEX_MATCAP] = style_.matcap; m->tex[TEX_FLOW] = style_.flow;
        m->params.shadowColor = glm::vec4(0.72f, 0.64f, 0.82f, 1.f); m->params.rampBlend = 0.85f; m->params.realtimeShadowStrength = 1.f; m->params.pbrBaseBlend = 0.45f; m->params.rampThreshold = 0.56f; m->params.rampFeather = 0.04f;
        return m;
    };
    // 1) face-SDF head
    Material* head = mk("Lab/FaceSDF", glm::vec4(0.97f, 0.80f, 0.72f, 1));
    head->tex[TEX_FACE_SDF] = style_.faceSDF; head->params.sdfShadowStrength = 1.f; head->params.sdfSoftness = 0.05f; head->params.faceColorStabilize = 0.6f;
    head->params.sdfShadowColor = glm::vec4(0.88f, 0.60f, 0.62f, 1.f); head->params.outlineWidth = 0.014f;
    // 2) wet metal ball (droplets)
    Material* wet = mk("Lab/WetMetal", glm::vec4(0.35f, 0.55f, 0.95f, 1));
    const uint8_t par[4] = {230, 40, 255, 140};
    Texture* paramTex = renderer_.createTexture("lab_param", par, 1, 1, false, false);
    wet->tex[TEX_PARAM] = paramTex; wet->params.metallicScale = 0.85f; wet->params.smoothnessMin = 0.5f; wet->params.smoothnessMax = 0.98f; wet->params.smoothnessScale = 1.f;
    wet->params.wetness = 1.f; wet->params.wetGlossMask = 1.f; wet->params.wetClothMask = 0.5f; wet->params.wetDropletVisibility = 0.55f; wet->params.wetDropletDensity = 0.55f;
    wet->params.pbrBaseBlend = 1.f; wet->params.specRampStrength = 0.6f;
    // 3) anisotropic hair ball
    Material* hair = mk("Lab/HairAniso", glm::vec4(0.28f, 0.20f, 0.42f, 1));
    hair->tex[TEX_HAIR_SPEC] = style_.hairSpec; hair->params.hairAnisoStrength = 1.4f; hair->params.hairAnisoPower = 40.f; hair->params.pbrBaseBlend = 0.3f;
    hair->params.rimIntensity = 0.35f; hair->params.rimColor = glm::vec4(0.7f, 0.8f, 1.f, 1.f);
    // 4) alpha-blended glass spheres (sorted back to front) to exercise the transparency path
    auto glass = [&](const char* name, glm::vec4 c) {
        Material* g = mk(name, c); g->transparent = true; g->castShadow = false; g->params.alphaClip = 0.f; g->params.outlineAlpha = 0.f;
        g->params.doubleSided = 1.f; g->params.smoothnessMin = 0.8f; g->params.smoothnessMax = 0.95f; g->params.pbrBaseBlend = 0.0f; g->params.rimIntensity = 0.3f; g->params.matCapStrength = 0.f; g->params.specRampStrength = 0.f; g->params.lutStrength = 0.f; g->params.occlusionStrength = 0.f; g->params.rimColor = glm::vec4(1, 1, 1, 1);
        renderer_.flushMaterial(g); return g; };
    Material* glassA = glass("Lab/GlassPink", glm::vec4(1.0f, 0.30f, 0.60f, 0.55f));
    Material* glassB = glass("Lab/GlassCyan", glm::vec4(0.15f, 0.75f, 1.0f, 0.55f));
    for (Material* m : {head, wet, hair}) renderer_.flushMaterial(m);

    auto sphere = [&](const char* name, Material* m, float x, float uShift) {
        auto model = makePrimitiveModel(renderer_, name, MeshData::sphere(0.6f, 64, 40, uShift), m);
        auto e = std::make_unique<Entity>(); e->name = name; e->init(model); e->pos = {x, 0.65f, 0}; entities_.push_back(std::move(e)); return entities_.back().get();
    };
    Entity *s1 = sphere("Lab: Face SDF", head, -1.5f, 0.25f), *s2 = sphere("Lab: Wet metal", wet, 0.f, 0.f), *s3 = sphere("Lab: Hair aniso", hair, 1.5f, 0.f);

    auto glassSphere = [&](const char* name, Material* m, glm::vec3 pos, float r) {
        auto model = makePrimitiveModel(renderer_, name, MeshData::sphere(r, 48, 32, 0.f), m);
        auto e = std::make_unique<Entity>(); e->name = name; e->init(model); e->pos = pos; entities_.push_back(std::move(e)); return entities_.back().get(); };
    Entity *g1 = glassSphere("Lab: glass A", glassA, {-0.75f, 0.55f, 1.3f}, 0.5f), *g2 = glassSphere("Lab: glass B", glassB, {0.75f, 0.55f, 1.0f}, 0.5f);

    // showcases
    auto byName = [&](const char* n) -> Entity* { for (auto& e : entities_) if (e->name == n) return e.get(); return nullptr; };
    Entity *fox = byName("Fox (glTF)"), *ces = byName("CesiumMan (glTF)");
    if (vrm_) showcases_.push_back({"Avatar (VRM) + motion matching", {ground_, vrm_}, vrm_});
    if (fox) { fox->pos = {0, 0, 0}; showcases_.push_back({"Fox (glTF, skinned)", {ground_, fox}, fox}); }
    if (ces) showcases_.push_back({"CesiumMan (glTF, skinned)", {ground_, ces}, ces});
    showcases_.push_back({"Shader lab", {ground_, s1, s2, s3, g2, g1}, s2});
    buildMotionLab();
}

void Sandbox::buildMotionLab() {
    if (!vrm_) return;
    auto add = [&](std::shared_ptr<Model> m, const char* name) -> Entity* {
        auto e = std::make_unique<Entity>(); e->name = name; e->init(std::move(m)); entities_.push_back(std::move(e)); return entities_.back().get(); };
    lab_.build(renderer_, style_, opt_.outlines, add);
    Showcase sc{"Motion lab (world, NPCs, navmesh)", {ground_, vrm_}, vrm_, true};
    for (Entity* e : lab_.visuals) sc.entities.push_back(e);
    spawnNpcs(sc);
    showcases_.push_back(sc);
}

void Sandbox::spawnNpcs(Showcase& sc) {
    auto add = [&](std::shared_ptr<Model> m, const char* name) -> Entity* {
        auto e = std::make_unique<Entity>(); e->name = name; e->init(std::move(m)); entities_.push_back(std::move(e)); return entities_.back().get(); };
    if (vrm_->mm && lab_.navReady && !npcModels_.empty()) {
        struct Spec { int model; const char* name; Npc::Mode mode; glm::vec3 pos; float side; };
        const Spec specs[] = {{0, "NPC Seed-san (follows)", Npc::Follow, {-2.5f, 0, -3.f}, 1.f}, {1, "NPC Girl (follows)", Npc::Follow, {2.5f, 0, -3.f}, -1.f},
                              {0, "NPC Seed-san (wanders)", Npc::Wander, {-6.f, 0, 12.f}, 1.f}, {1, "NPC Girl (wanders)", Npc::Wander, {13.f, 0, -2.f}, 1.f}};
        for (const Spec& sp : specs) {
            auto& mdl = npcModels_[std::min<size_t>(sp.model, npcModels_.size() - 1)];
            Entity* e = add(mdl, sp.name);
            e->mmModel = vrm_->model; e->mmDb = vrm_->mmDb;
            e->mm = std::make_unique<MotionMatcher>(); e->mm->init(&vrm_->model->rig, vrm_->mmDb.get()); e->mm->settings = vrm_->mm->settings;
            // Every character animates from the same database (all clip families it contains), but the crowd searches
            // less often than the player: one search is a full pass over the whole library (~6 ms for 77 clips).
            e->mm->settings.searchInterval = std::max(e->mm->settings.searchInterval, 0.18f);
            e->world = &lab_.world; e->collide = true; e->ik.enabled = opt_.ik; e->hands.enabled = opt_.hands; e->look.enabled = opt_.lookAt; e->spring.enabled = opt_.springs;
            lab_.addNpc(e, sp.mode, sp.pos, sp.side);
            e->mm->reset(e->pos, e->yaw);
            sc.entities.push_back(e);
        }
    }
}

void Sandbox::setShowcase(int idx) {
    if (showcases_.empty()) return;
    showcase_ = (idx % (int)showcases_.size() + (int)showcases_.size()) % (int)showcases_.size();
    for (auto& e : entities_) e->visible = false;
    for (Entity* e : showcases_[showcase_].entities) e->visible = true;
    Entity* h = showcases_[showcase_].hero;
    camera_.target = h->pos + glm::vec3(0, 1.0f, 0);
    if (h == vrm_) camera_.distance = std::max(camera_.distance, 2.8f);
    else if (showcases_[showcase_].name.rfind("Shader lab", 0) == 0) { camera_.distance = std::max(camera_.distance, 5.5f); camera_.target = {0, 0.8f, 0}; }
    else camera_.distance = std::max(camera_.distance, 3.0f);
    rain_ = rainOn_ ? std::max(rain_, 0.01f) : rain_;
    if (vrm_) {
        bool lab = showcases_[showcase_].lab;
        vrm_->world = lab ? &lab_.world : nullptr; vrm_->collide = lab;
        if (lab && vrm_->pos == glm::vec3(0)) { vrm_->pos = lab_.spawn; }
        if (lab) { camera_.distance = std::max(camera_.distance, 3.4f); camera_.target = vrm_->pos + glm::vec3(0, 1.3f, 0); camSmooth_ = camera_.target; }
    }
}

std::vector<Material*> Sandbox::activeMaterials() {
    std::vector<Material*> mats;
    Entity* h = showcases_[showcase_].hero;
    if (showcases_[showcase_].name.rfind("Shader lab", 0) == 0) {
        for (Entity* e : showcases_[showcase_].entities) if (e != ground_) for (Material* m : e->model->materials) mats.push_back(m);
    } else if (h) for (Material* m : h->model->materials) mats.push_back(m);
    mats.push_back(ground_->model->materials[0]);
    return mats;
}

void Sandbox::applyRain(float amount) {
    for (auto& e : entities_) for (Material* m : e->model->materials) {
        if (m->name.rfind("Lab/", 0) == 0) continue;           // lab materials keep their own wetness
        m->params.wetness = amount; renderer_.flushMaterial(m);
    }
}

void Sandbox::handleActions(float dt) {
    if (input_.pressed("Next")) setShowcase(showcase_ + 1);
    if (input_.pressed("Previous")) setShowcase(showcase_ - 1);
    if (input_.pressed("Interact")) { rainOn_ = !rainOn_; rain_ = rainOn_ ? 1.f : 0.f; }
    if (input_.pressed("Attack") && window_.cursorCaptured()) debugMode_ = (debugMode_ + 1) % 14;
    if (input_.pressed("Jump") && vrm_ && vrm_->mm) vrm_->mm->requestJump();      // allow the jump clips for a moment (the old light-rotate toggle lives in the UI only)
    // camera look
    glm::vec2 look = input_.vec2("Look");
    bool pad = input_.device().padConnected && !touch_.anyActive() && glm::length(look) > 0;
    if (window_.cursorCaptured() || pad || touch_.anyActive()) {
        float sens = pad ? 2.4f * dt : 0.0028f;
        camera_.yaw -= look.x * sens; camera_.pitch = glm::clamp(camera_.pitch + look.y * sens * (pad ? -1.f : 1.f), -0.5f, 1.35f);
    }
    float scroll = input_.device().scroll.y;
    if (scroll != 0 && !(renderer_.uiEnabled() && ImGui::GetIO().WantCaptureMouse)) camera_.distance = glm::clamp(camera_.distance - scroll * 0.3f, 1.2f, 14.f);

    // movement -> desired world velocity
    glm::vec2 mv = input_.vec2("Move");
    float mag = std::min(1.f, glm::length(mv));
    glm::vec3 dir = camera_.forwardFlat() * mv.y + camera_.rightFlat() * mv.x;
    float speed = 1.6f;
    if (vrm_ && vrm_->mm) { auto& s = vrm_->mm->settings; speed = input_.held("Sprint") ? s.runSpeed : input_.held("Crouch") ? s.slowSpeed : s.walkSpeed; }
    desiredVel_ = mag > 0.01f ? glm::normalize(dir) * speed * std::min(1.f, 0.35f + mag) : glm::vec3(0);
}

void Sandbox::updateScene(float dt) {
    time_ += dt;
    if (lightAuto_) lightAz_ = std::fmod(lightAz_ + 25.f * dt, 360.f);
    if (todAuto_) tod_ = std::fmod(tod_ + todSpeed_ * dt, 24.f);
    const Showcase& sc = showcases_[showcase_];
    Entity* hero = sc.hero;
    bool lab = sc.lab;
    std::set<Entity*> npcSet; if (lab) for (auto& n : lab_.npcs) npcSet.insert(n.e);
    if (vrm_) vrm_->sprinting = input_.held("Sprint");
    if (vrm_ && vrm_->mm && std::getenv("SAKUGA_TRACE") && (frame_ % 15) == 0) {      // debugging: what is the matcher playing?
        const MotionDatabase* db = vrm_->mm->database(); int c = vrm_->mm->debug.clip;
        std::fprintf(stderr, "trace f%4d t=%.1f pos(%.2f,%.2f,%.2f) speed %.2f clip %-28s cat %-10s cost %.2f\n", frame_, time_, vrm_->pos.x, vrm_->pos.y, vrm_->pos.z, glm::length(vrm_->velocity),
                     (db && c >= 0) ? db->clipNames[c].c_str() : "-", (db && c >= 0) ? kMotionCategoryNames[db->clipCategory[c]] : "-", vrm_->mm->debug.cost);
    }
    for (Entity* e : sc.entities) {
        if (e == ground_ || npcSet.count(e)) continue;
        bool isHero = e == vrm_;
        e->update(dt, isHero ? desiredVel_ : glm::vec3(0));
    }
    if (lab && vrm_) {
        vrm_->pos.x = glm::clamp(vrm_->pos.x, -28.5f, 28.5f); vrm_->pos.z = glm::clamp(vrm_->pos.z, -28.5f, 28.5f);
        if (vrm_->mm) lab_.updateNpcs(dt, *vrm_, vrm_->mm->settings.walkSpeed, vrm_->mm->settings.runSpeed);
        for (auto& n : lab_.npcs) { n.e->update(dt, n.e->velocityRequest); n.e->pos.x = glm::clamp(n.e->pos.x, -29.f, 29.f); n.e->pos.z = glm::clamp(n.e->pos.z, -29.f, 29.f); }
        // reflect the crowd's idea of the player back (the player is an avoidance obstacle for the NPCs)
    }
    updateCameraAndGaze(dt, lab);
    if (rainOn_) applyRain(rain_);
    else applyRain(0.f);
    // optional per-frame debug-mode override (cycled with Attack / UI) applied to the active materials
    for (Material* m : activeMaterials()) if (m->params.debugMode != (float)debugMode_) { m->params.debugMode = (float)debugMode_; renderer_.flushMaterial(m); }
    (void)hero;
}

// Third-person camera with a collision spring arm, and the hero's gaze (nearest NPC, otherwise where the camera is aiming).
void Sandbox::updateCameraAndGaze(float dt, bool lab) {
    Entity* hero = showcases_[showcase_].hero;
    if (hero == vrm_ && vrm_) {
        glm::vec3 t = vrm_->pos + glm::vec3(0, 1.3f, 0);
        camSmooth_ = glm::mix(camSmooth_, t, 1.f - std::exp(-dt * 9.f));
        glm::vec3 right = camera_.rightFlat();
        camera_.target = camSmooth_ + (lab ? right * cameraShoulder_ : glm::vec3(0));
        float want = camera_.distance;
        if (lab) {
            glm::vec3 cam = camera_.position(); camera_.usedDistance = -1.f; cam = camera_.position();
            glm::vec3 dir = cam - camera_.target; float len = glm::length(dir);
            if (len > 1e-3f) {
                dir /= len;
                RayHit h = lab_.world.raycast(camera_.target, dir, want + 0.35f);
                float allowed = h.hit ? std::max(0.5f, h.t - 0.35f) : want;
                float cur = camUsed_ < 0 ? want : camUsed_;
                cur += (allowed - cur) * (1.f - std::exp(-(allowed < cur ? 28.f : 3.f) * dt));
                camUsed_ = cur; camera_.usedDistance = std::min(cur, want);
            }
        } else camera_.usedDistance = -1.f;
        // gaze
        glm::vec3 camPos = camera_.position(), fwd = glm::normalize(camera_.target - camPos);
        vrm_->lookTarget = camera_.target + fwd * 12.f; vrm_->hasLookTarget = true;
        if (lab) {
            float best = 4.5f; Entity* bestN = nullptr;
            for (auto& n : lab_.npcs) {
                glm::vec3 d = n.e->pos - vrm_->pos; float dist = glm::length(glm::vec2(d.x, d.z));
                if (dist < best) { best = dist; bestN = n.e; }
            }
            if (bestN) { vrm_->lookTarget = bestN->pos + glm::vec3(0, 1.5f * bestN->scale, 0); vrm_->setExpression("happy", best < 3.f ? 0.35f : 0.f); }
            else vrm_->setExpression("happy", 0.f);
        }
    } else camera_.usedDistance = -1.f;
    applyTimeOfDay();
}

static float ss(float a, float b, float v) { float t = glm::clamp((v - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }

// Time of day: sun/moon direction and colour, sky gradient, ambient, fog, stars, exposure, lamp lights.
void Sandbox::applyTimeOfDay() {
    if (!todEnabled_) return;
    struct Key { float elev; glm::vec3 zenith, horizon, sky, ground, fog, sun; float sunI, ambient, exposure; };
    static const Key keys[] = {
        {-16.f, {0.012f, 0.022f, 0.085f}, {0.05f, 0.07f, 0.18f}, {0.16f, 0.22f, 0.46f}, {0.05f, 0.06f, 0.11f}, {0.04f, 0.06f, 0.15f}, {1.f, 0.9f, 0.8f}, 0.0f, 0.62f, 1.15f},
        {-5.f,  {0.06f, 0.08f, 0.26f}, {0.42f, 0.26f, 0.42f}, {0.26f, 0.28f, 0.52f}, {0.12f, 0.10f, 0.16f}, {0.30f, 0.20f, 0.34f}, {1.f, 0.5f, 0.3f}, 0.0f, 0.62f, 1.1f},
        {0.f,   {0.17f, 0.27f, 0.58f}, {1.00f, 0.52f, 0.34f}, {0.46f, 0.42f, 0.66f}, {0.30f, 0.22f, 0.24f}, {0.92f, 0.58f, 0.48f}, {1.f, 0.62f, 0.40f}, 0.85f, 0.62f, 1.1f},
        {9.f,   {0.21f, 0.41f, 0.84f}, {0.98f, 0.75f, 0.60f}, {0.46f, 0.56f, 0.86f}, {0.34f, 0.28f, 0.26f}, {0.90f, 0.77f, 0.70f}, {1.f, 0.74f, 0.50f}, 0.88f, 0.62f, 1.03f},
        {28.f,  {0.20f, 0.44f, 0.95f}, {0.64f, 0.82f, 0.99f}, {0.42f, 0.62f, 0.95f}, {0.34f, 0.30f, 0.30f}, {0.70f, 0.84f, 0.98f}, {1.f, 0.90f, 0.78f}, 0.95f, 0.62f, 1.0f},
        {70.f,  {0.16f, 0.38f, 0.95f}, {0.60f, 0.80f, 0.99f}, {0.42f, 0.62f, 0.95f}, {0.36f, 0.32f, 0.30f}, {0.68f, 0.84f, 0.98f}, {1.f, 0.95f, 0.88f}, 1.0f, 0.62f, 0.98f}};
    float t = std::fmod(std::fmod(tod_, 24.f) + 24.f, 24.f);
    float sunElev = 70.f * std::sin((t - 6.f) * glm::pi<float>() / 12.f);     // degrees; below the horizon at night
    float sunAz = 90.f + (t - 6.f) * 15.f;
    float tm = std::fmod(t - 18.f + 24.f, 24.f);                              // hours since 18:00
    float moonElev = 55.f * std::sin(tm * glm::pi<float>() / 12.f), moonAz = 270.f + tm * 15.f;
    int i = 0; while (i < 4 && sunElev > keys[i + 1].elev) ++i;
    float f = glm::clamp((sunElev - keys[i].elev) / (keys[i + 1].elev - keys[i].elev), 0.f, 1.f);
    auto L = [&](glm::vec3 Key::*m) { return glm::mix(keys[i].*m, keys[i + 1].*m, f); };
    auto Lf = [&](float Key::*m) { return glm::mix(keys[i].*m, keys[i + 1].*m, f); };
    float sunI = Lf(&Key::sunI), moonI = 0.34f * (1.f - ss(-12.f, 0.f, sunElev)) * ss(-4.f, 10.f, moonElev);
    bool useSun = sunI >= moonI;
    glm::vec3 sunCol = L(&Key::sun);
    float az = useSun ? sunAz : moonAz, el = useSun ? std::max(sunElev, 2.f) : std::max(moonElev, 14.f);
    auto q = [](glm::vec3 v) { return glm::round(v * 40.f) / 40.f; };      // quantised: the procedural IBL is re-baked whenever these change
    light_.zenith = q(L(&Key::zenith)); light_.horizon = q(L(&Key::horizon)); light_.sky = q(L(&Key::sky)); light_.ground = q(L(&Key::ground));
    light_.fogColor = L(&Key::fog);
    light_.color = useSun ? sunCol : glm::vec3(0.55f, 0.66f, 1.0f);
    light_.intensity = std::max(sunI, moonI); light_.ambient = Lf(&Key::ambient);
    lightAz_ = az; lightEl_ = el;
    light_.starVisibility = 1.f - ss(-14.f, -3.f, sunElev);
    light_.cloudCover = cloud_; light_.cloudTime = time_ * 0.02f;
    light_.sunDisc = 1.f;
    post_.exposure = userExposure_ * Lf(&Key::exposure);
    if (hdriMode_) light_.envIntensity = userEnv_ * (0.06f + 0.94f * ss(-10.f, 12.f, sunElev));
    else light_.envIntensity = userEnv_;
    light_.nprOnly = nprFull_;
    todSun_ = sunElev;
}

FrameInput Sandbox::buildFrame(float dt) {
    FrameInput fi;
    fi.proj = Renderer::perspective(camera_.fov, renderer_.aspect(), camera_.zNear, camera_.zFar);
    fi.view = camera_.view(); fi.cameraPos = camera_.position(); fi.time = time_;
    float az = glm::radians(lightAz_), el = glm::radians(lightEl_);
    light_.direction = glm::vec3(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
    if (hdriMode_ && !hdriSun_) { float w = envSunAz_ - az; light_.envRotation = std::fmod(std::fmod(w, 6.2831853f) + 6.2831853f, 6.2831853f); }   // rotate the HDRI so its sun sits where the directional light is
    post_.lutStrength = lutIdx_ >= 0 ? lutStrength_ : 0.f;
    light_.fitCascadeToCamera = opt_.fitCascade; light_.pointShadows = opt_.pointShadows;
    fi.light = light_; fi.post = post_;
    if (pointLightsOn_ && showcases_[showcase_].name.rfind("Shader lab", 0) == 0) {      // coloured extra lights to show the multi-light path
        fi.light.points[0] = {{-1.9f, 1.0f, 1.8f}, 4.5f, {1.0f, 0.35f, 0.65f}, 2.0f};
        fi.light.points[1] = {{1.9f, 0.9f, 1.6f}, 4.5f, {0.3f, 0.85f, 1.0f}, 2.0f};
    }
    if (showcases_[showcase_].lab && lampsOn_) {          // warm street lamps that fade in at dusk (the 4 point lights of the renderer)
        float night = 1.f - ss(-6.f, 6.f, todSun_);
        const glm::vec3 lamps[4] = {{-3.5f, 1.8f, 1.5f}, {3.5f, 1.8f, 1.5f}, {0.f, 2.6f, 10.f}, {-10.3f, 2.6f, 1.f}};
        for (int i = 0; i < 4; ++i) fi.light.points[i] = {lamps[i], 6.f, {1.0f, 0.72f, 0.42f}, 3.2f * night};
    }
    if (split_) { fi.light.splitDir = glm::vec2(1, 0); fi.light.splitOffsetPx = splitOffset_; }
    Entity* hero = showcases_[showcase_].hero;
    fi.shadowFocus = hero->pos + glm::vec3(0, 0.8f, 0);
    for (int i = 0; i < kMaxContacts; ++i) fi.light.contacts[i] = glm::vec4(0.f);
    { int ci = 0; for (Entity* e : showcases_[showcase_].entities) if (ci < kMaxContacts && e != nullptr && e->visible && (e->mm || e == vrm_)) fi.light.contacts[ci++] = glm::vec4(e->pos, 0.62f * e->scale); }
    if (std::getenv("SAKUGA_TRACE")) for (int i = 0; i < kMaxContacts; ++i) if (fi.light.contacts[i].w > 0) std::fprintf(stderr, "contact %d: %.2f %.2f %.2f r %.2f\n", i, fi.light.contacts[i].x, fi.light.contacts[i].y, fi.light.contacts[i].z, fi.light.contacts[i].w);
    for (Entity* e : showcases_[showcase_].entities) e->collect(fi);
    if (skeleton_ && hero) hero->debugSkeleton(fi.lines, packColor(1.f, 0.9f, 0.2f));
    if (showcases_[showcase_].lab) lab_.debugDraw(fi.lines, navDebug_, agentDebug_);
    if (mmDebug_ && vrm_ && vrm_->mm && hero == vrm_ && vrm_->mm->debug.hasSearch) {
        // Motion-matching feature visualisation (cf. the SIGGRAPH / GDC talks): current pose features (white), the best database match (magenta),
        // the query trajectory (cyan, the --trajectory view) and the matched trajectory (magenta); both feet = position cross + velocity line.
        const MMDebug& d = vrm_->mm->debug; float c = std::cos(vrm_->yaw), sn = std::sin(vrm_->yaw);
        auto W = [&](float x, float y, float z) { return vrm_->pos + glm::vec3(x * c + z * sn, y, -x * sn + z * c); };
        auto cross = [&](glm::vec3 p, uint32_t col, float r) { fi.lines.push_back({p - glm::vec3(r, 0, 0), col}); fi.lines.push_back({p + glm::vec3(r, 0, 0), col});
            fi.lines.push_back({p - glm::vec3(0, r, 0), col}); fi.lines.push_back({p + glm::vec3(0, r, 0), col}); fi.lines.push_back({p - glm::vec3(0, 0, r), col}); fi.lines.push_back({p + glm::vec3(0, 0, r), col}); };
        uint32_t cw = packColor(1.f, 1.f, 1.f), cm = packColor(1.f, 0.25f, 0.85f), cc = packColor(0.2f, 0.9f, 1.f);
        for (int set = 0; set < 2; ++set) {
            const float* r = set == 0 ? d.currentRaw : d.matchRaw; uint32_t col = set == 0 ? cw : cm;
            for (int f = 0; f < 2; ++f) {
                glm::vec3 p = W(r[f * 3], r[f * 3 + 1] + 0.02f, r[f * 3 + 2]);
                glm::vec3 v = W(r[6 + f * 3], 0, r[6 + f * 3 + 2]) - vrm_->pos; v.y = r[6 + f * 3 + 1];
                cross(p, col, 0.05f); fi.lines.push_back({p, col}); fi.lines.push_back({p + v * 0.35f, col});
            }
            glm::vec3 hp = W(0, 0.95f, 0); glm::vec3 hv = W(r[12], 0, r[14]) - vrm_->pos; hv.y = r[13];
            fi.lines.push_back({hp, col}); fi.lines.push_back({hp + hv * 0.35f, col});
        }
        glm::vec3 prev = vrm_->pos + glm::vec3(0, 0.05f, 0);
        for (int t = 0; t < kTrajSamples; ++t) {
            glm::vec3 p = W(d.matchRaw[15 + t * 2], 0.05f, d.matchRaw[15 + t * 2 + 1]);
            glm::vec3 dir = W(d.matchRaw[21 + t * 2], 0, d.matchRaw[21 + t * 2 + 1]) - vrm_->pos; (void)dir;
            glm::vec3 fwd = glm::vec3(std::sin(vrm_->yaw + std::atan2(d.matchRaw[21 + t * 2], d.matchRaw[21 + t * 2 + 1])), 0, std::cos(vrm_->yaw + std::atan2(d.matchRaw[21 + t * 2], d.matchRaw[21 + t * 2 + 1])));
            fi.lines.push_back({prev, cm}); fi.lines.push_back({p, cm}); prev = p; cross(p, cm, 0.07f);
            fi.lines.push_back({p, cm}); fi.lines.push_back({p + fwd * 0.4f, cm});
            glm::vec3 q = d.predicted[t] + glm::vec3(0, 0.05f, 0); cross(q, cc, 0.07f); fi.lines.push_back({p, packColor(1.f, 0.8f, 0.2f)}); fi.lines.push_back({q, packColor(1.f, 0.8f, 0.2f)});
        }
    }
    if (trajectory_ && vrm_ && vrm_->mm && hero == vrm_) {
        const MMDebug& d = vrm_->mm->debug; uint32_t cy = packColor(0.2f, 0.9f, 1.f), cg = packColor(0.3f, 1.f, 0.4f);
        glm::vec3 prev = vrm_->pos + glm::vec3(0, 0.03f, 0);
        for (int i = 0; i < kTrajSamples; ++i) {
            glm::vec3 p = d.predicted[i] + glm::vec3(0, 0.03f, 0);
            fi.lines.push_back({prev, cy}); fi.lines.push_back({p, cy}); prev = p;
            fi.lines.push_back({p, cg}); fi.lines.push_back({p + d.predictedDir[i] * 0.35f, cg});
            fi.lines.push_back({p - glm::vec3(0.06f, 0, 0), cy}); fi.lines.push_back({p + glm::vec3(0.06f, 0, 0), cy});
            fi.lines.push_back({p - glm::vec3(0, 0, 0.06f), cy}); fi.lines.push_back({p + glm::vec3(0, 0, 0.06f), cy});
        }
        uint32_t cr = packColor(1.f, 0.3f, 0.3f);
        fi.lines.push_back({vrm_->pos + glm::vec3(0, 0.05f, 0), cr}); fi.lines.push_back({vrm_->pos + glm::vec3(0, 0.05f, 0) + d.desiredVelocity * 0.4f, cr});
    }
    return fi;
}

void Sandbox::drawCharacterUI() {
    if (vrm_ && ImGui::CollapsingHeader("Character: IK, look-at, springs, expressions")) {
        Entity& h = *vrm_;
        ImGui::SeparatorText("Look-at (head / neck / eyes)");
        ImGui::Checkbox("Look-at", &h.look.enabled); ImGui::SliderFloat("Look weight", &h.look.weight, 0, 1); ImGui::SliderFloat("Head half-life", &h.look.headHalflife, 0.03f, 0.5f);
        ImGui::SliderFloat("Eye yaw limit", &h.look.eyeYawLimit, 0.1f, 0.9f); ImGui::Checkbox("Eye saccades", &h.look.saccades);
        ImGui::SeparatorText("Hand IK (wall touch, lab only)");
        ImGui::Checkbox("Hand IK", &h.hands.enabled); ImGui::SliderFloat("Reach scale", &h.hands.reachScale, 0.6f, 1.3f); ImGui::SliderFloat("Palm offset", &h.hands.palmOffset, 0.02f, 0.15f);
        ImGui::Text("hand weight L %.2f  R %.2f", h.handState.hand[0].w, h.handState.hand[1].w);
        ImGui::SeparatorText("Foot IK + pelvis");
        ImGui::Checkbox("Pelvis offset", &h.ik.pelvis); ImGui::SameLine(); ImGui::Checkbox("Align foot to ground", &h.ik.alignFoot);
        ImGui::Text("pelvis %.1f cm | sole height L %.1f R %.1f cm | ground L %.2f R %.2f m", h.ikState.pelvisOffset * 100, h.ikState.soleHeight[0] * 100, h.ikState.soleHeight[1] * 100, h.ikState.groundAtFoot[0], h.ikState.groundAtFoot[1]);
        ImGui::SeparatorText("Spring bones (hair, clothes, accessories)");
        ImGui::Checkbox("Springs", &h.spring.enabled); ImGui::SliderFloat("Stiffness x", &h.spring.stiffnessScale, 0, 3); ImGui::SliderFloat("Drag x", &h.spring.dragScale, 0, 2);
        ImGui::SliderFloat("Gravity x", &h.spring.gravityScale, 0, 3); ImGui::SliderFloat("Wind", &h.spring.windStrength, 0, 3);
        // ZZZ spring tuning (post slider)
        ImGui::SliderFloat("Tuning Stiffness", &h.springTuning.stiffness, 0.1, 3.0);
        ImGui::SliderFloat("Tuning Drag", &h.springTuning.drag, 0.1, 3.0);
        ImGui::SliderFloat("Tuning Gravity", &h.springTuning.gravity, 0.1, 3.0);
        ImGui::SliderFloat("Tuning Collider Radius (m)", &h.springTuning.colliderRadiusPad, -0.02f, 0.05f);
        ImGui::Text("%zu chains, %zu colliders (VRM %s)", h.model->springs.chains.size(), h.model->springs.colliders.size(), h.model->vrmVersion == 1 ? "1.0" : "0.x");
        
        
        ImGui::SeparatorText("Posture correction (Animation Rigging analogue)");
        ImGui::SliderFloat("Spine pitch", &h.posture.spine, -20.f, 20.f);
        ImGui::SliderFloat("Chest pitch", &h.posture.chest, -20.f, 20.f);
        ImGui::SliderFloat("Neck pitch", &h.posture.neck, -20.f, 20.f);
        ImGui::SliderFloat("Head pitch", &h.posture.head, -20.f, 20.f);
        ImGui::SliderFloat("Lean/Hunch correction", &h.posture.hunchCorrection, 0.f, 1.f);
        ImGui::SliderFloat("Posture weight", &h.posture.weight, 0.f, 1.f);
        ImGui::SeparatorText("Expressions (morph targets)");
        ImGui::Checkbox("Auto blink", &h.autoBlink);
        for (size_t i = 0; i < h.model->expressions.size(); ++i) {
            const std::string& n = h.model->expressions[i].name;
            if (n == "lookUp" || n == "lookDown" || n == "lookLeft" || n == "lookRight" || n == "blinkLeft" || n == "blinkRight") continue;
            ImGui::SliderFloat(n.c_str(), &h.exprTarget[i], 0.f, 1.f);
        }
    }
    if (showcases_[showcase_].lab && ImGui::CollapsingHeader("Navigation + NPC crowd (Recast / Detour)")) {
        ImGui::Text("navmesh: %d polys, %d verts, built in %.0f ms", lab_.nav.polyCount(), lab_.nav.vertexCount(), lab_.nav.buildMs());
        ImGui::Checkbox("Draw navmesh (orange = mud, cost x6)", &navDebug_); ImGui::SameLine(); ImGui::Checkbox("Agents / paths", &agentDebug_);
        ImGui::Checkbox("Motion-matching feature debug", &mmDebug_);
        for (auto& n : lab_.npcs) ImGui::BulletText("%s: %s  (%.1f m/s)", n.e->name.c_str(), n.status.c_str(), glm::length(glm::vec2(n.e->velocity.x, n.e->velocity.z)));
    }
}

// The look is data (gfx/UiTheme.h): a preset name, or a .json file next to config/ that can be edited while the app runs.
void Sandbox::loadUiTheme() {
    uiThemeError_.clear();
    uiThemePath_.clear();
    // No --ui-theme: use the theme file committed next to the config, so it can be edited while the app runs.
    std::string want = opt_.uiTheme;
    if (want.empty()) {
        const std::string shipped = root_ + "/config/ui_theme.json";
        std::error_code ec;
#ifdef __ANDROID__
        const std::string userTheme = root_ + "/user/ui_theme.json";
        if (fs::exists(userTheme, ec)) want = userTheme;
        else
#endif
        want = fs::exists(shipped, ec) ? shipped : std::string(uiThemeDefaultName());
    }
    uiTheme_ = uiThemeLoad(want, root_ + "/config", &uiThemePath_, &uiThemeError_);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "ui theme loaded name=%s user=%d", uiTheme_.name.c_str(), (int)(uiThemePath_ == root_ + "/user/ui_theme.json"));
#endif
    if (!uiThemeError_.empty()) SK_WARN("ui: %s", uiThemeError_.c_str());
    std::snprintf(themeName_, sizeof themeName_, "%s", uiTheme_.name.c_str());
    uiThemeStamp_ = fs::file_time_type{};
    if (!uiThemePath_.empty()) {
        std::error_code ec; uiThemeStamp_ = fs::last_write_time(uiThemePath_, ec);
        SK_LOG("ui: theme %s from %s", uiTheme_.name.c_str(), uiThemePath_.c_str());
    } else {
        SK_LOG("ui: theme %s (built-in preset; F12 to tweak and save)", uiTheme_.name.c_str());
    }
}

void Sandbox::drawUiThemeWindow() {
    if (!showThemeEditor_) return;
    ImGui::SetNextWindowSize(ImVec2(430, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 12, 120), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    if (!ImGui::Begin("UI theme (F12)", &showThemeEditor_)) { ImGui::End(); return; }

    bool changed = false;
    if (ImGui::BeginCombo("Preset", uiTheme_.name.c_str())) {
        for (const std::string& n : uiThemeNames())
            if (ImGui::Selectable(n.c_str(), n == uiTheme_.name)) {
                uiTheme_ = uiThemePreset(n);
                std::snprintf(themeName_, sizeof themeName_, "%s", n.c_str());
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::InputText("Name", themeName_, sizeof themeName_);
    changed |= ImGui::SliderFloat("Font scale", &uiTheme_.fontScale, 0.5f, 2.5f, "%.2fx");
    changed |= ImGui::SliderFloat("Panel alpha", &uiTheme_.panelAlpha, 0.2f, 1.f, "%.2f");
    ImGui::SameLine(); ImGui::Checkbox("Demo (F11)", &showImGuiDemo_);
    if (changed) renderer_.setUiTheme(uiTheme_);

    ImGui::SeparatorText("Live style");
    ImGui::TextDisabled("Edits below apply immediately; Save writes the current state back to JSON.");
    ImGui::ShowStyleEditor(nullptr);        // imgui_demo.cpp, operates on ImGui::GetStyle()

    ImGui::SeparatorText("File");
#ifdef __ANDROID__
    ImGui::TextWrapped("Save: %s/user/ui_theme.json", root_.c_str());
#else
    ImGui::TextWrapped("%s", uiThemePath_.empty() ? "(built-in preset - Save creates config/ui_theme.json)" : uiThemePath_.c_str());
#endif
    if (!uiThemeError_.empty()) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.35f, 1.f), "%s", uiThemeError_.c_str());

    if (ImGui::Button("Save")) {
        UiTheme live = uiThemeFromLiveStyle(ImGui::GetStyle(), themeName_);
        live.fontScale = uiTheme_.fontScale;
        uiThemeSanitize(&live);
#ifdef __ANDROID__
        const std::string path = root_ + "/user/ui_theme.json";
#else
        const std::string path = uiThemePath_.empty() ? root_ + "/config/ui_theme.json" : uiThemePath_;
#endif
        std::error_code ec; fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        f << uiThemeToJson(live);
        const bool ok = f.good();
        f.close();
        if (ok) {
            uiTheme_ = live; uiThemePath_ = path; uiThemeError_.clear();
            uiThemeStamp_ = fs::last_write_time(path, ec);        // do not immediately "reload" what we just wrote
            SK_LOG("ui: saved theme %s", path.c_str());
        } else {
            uiThemeError_ = "could not write " + path;
            SK_ERR("ui: %s", uiThemeError_.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        std::string used, err;
        UiTheme t = uiThemeLoad(uiThemePath_.empty() ? std::string(themeName_) : uiThemePath_, root_ + "/config", &used, &err);
        uiTheme_ = t; uiThemeError_ = err;
        std::snprintf(themeName_, sizeof themeName_, "%s", uiTheme_.name.c_str());
        renderer_.setUiTheme(uiTheme_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to preset")) {
        uiTheme_ = uiThemePreset(themeName_);
        renderer_.setUiTheme(uiTheme_);
    }
    ImGui::TextDisabled("Editing the .json on disk reloads it automatically.");
    ImGui::End();
}

void Sandbox::drawUI(float wallDt) {
    if (!renderer_.uiEnabled()) return;
    renderer_.beginUI(wallDt);
    if (showFps_) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 12, 12), ImGuiCond_Always, ImVec2(1, 0)); ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
        ImGui::Text("%.0f FPS  (%.1f ms)", fps_, 1000.f / std::max(fps_, 1.f)); ImGui::TextDisabled("%u draws, %u tris", renderer_.stats.drawCalls, renderer_.stats.triangles);
        ImGui::End();
    }
    touch_.draw(ImGui::GetForegroundDrawList());
    // Hot reload: a theme file that changed on disk (editor save) is picked up without restarting the app.
    if (!uiThemePath_.empty()) {
        std::error_code ec;
        const auto stamp = fs::last_write_time(uiThemePath_, ec);
        if (!ec && stamp != uiThemeStamp_) {
            uiThemeStamp_ = stamp;
            std::string used, err;
            UiTheme t = uiThemeLoad(uiThemePath_, root_ + "/config", &used, &err);
            if (err.empty()) {
                uiTheme_ = t;
                std::snprintf(themeName_, sizeof themeName_, "%s", uiTheme_.name.c_str());
                renderer_.setUiTheme(uiTheme_);
                SK_LOG("ui: reloaded %s", uiThemePath_.c_str());
            } else {
                uiThemeError_ = err;
                SK_WARN("ui: %s", err.c_str());
            }
        }
    }
    drawUiThemeWindow();
    if (showImGuiDemo_) ImGui::ShowDemoWindow(&showImGuiDemo_);
    if (!showUI_) return;
#ifdef __ANDROID__
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * .50f, ImGui::GetIO().DisplaySize.y * .48f), ImGuiCond_FirstUseEver);
#else
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize(ImVec2(430, 760), ImGuiCond_FirstUseEver);
#endif
    ImGui::Begin("Sakuga Engine");
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "SAKUGA"); ImGui::SameLine();
    ImGui::TextDisabled("/  CREATIVE SANDBOX");
    ImGui::TextWrapped("%s", ctx_.props.deviceName);
    ImGui::TextDisabled("%.0f FPS   |   %u draws   |   %u triangles", fps_, renderer_.stats.drawCalls, renderer_.stats.triangles);
#ifndef __ANDROID__
    ImGui::TextDisabled("Tab mouse  /  F1 hide  /  F2 capture  /  F12 theme");
#endif
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Animation license / credits")) {
        ImGui::TextWrapped("LAFAN1 motion capture by Ubisoft La Forge. CC BY-NC-ND 4.0: non-commercial use only; sharing adapted material is not permitted.");
        ImGui::TextWrapped("Source: github.com/ubisoft/ubisoft-laforge-animation-dataset");
        ImGui::TextWrapped("License: creativecommons.org/licenses/by-nc-nd/4.0/  |  Full notice and disclaimer: assets/licenses/");
    }
    if (ImGui::CollapsingHeader("Device and performance")) {
        // Refresh slowly: filesystem and OS queries do not belong in the per-frame hot path.
        static SysInfo device;
        static double nextRefresh = 0;
        if (ImGui::GetTime() >= nextRefresh) {
            device = collectSysInfo(root_);
            nextRefresh = ImGui::GetTime() + 2.0;
        }
        constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
        ImGui::TextWrapped("CPU: %s (%u logical threads)", device.cpu.empty() ? "unknown" : device.cpu.c_str(), device.logicalCores);
        if (device.totalMemory) ImGui::Text("System RAM: %.2f GiB total, %.2f GiB available", device.totalMemory / GiB, device.availableMemory / GiB);
        else ImGui::TextUnformatted("System RAM: unavailable");
        ImGui::TextWrapped("Storage for %s", device.storagePath.c_str());
        if (device.storageTotal) ImGui::Text("%.1f GiB total, %.1f GiB available; medium: %s", device.storageTotal / GiB, device.storageAvailable / GiB, device.storageType.c_str());
        else ImGui::TextUnformatted("Storage capacity: unavailable");
        ImGui::TextWrapped("GPU: %s", ctx_.props.deviceName);
        ImGui::Text("Vulkan: %u.%u.%u", VK_VERSION_MAJOR(ctx_.props.apiVersion), VK_VERSION_MINOR(ctx_.props.apiVersion), VK_VERSION_PATCH(ctx_.props.apiVersion));
        VkPhysicalDeviceMemoryProperties heaps{}; vkGetPhysicalDeviceMemoryProperties(ctx_.phys, &heaps);
        VkDeviceSize local = 0;
        for (uint32_t i = 0; i < heaps.memoryHeapCount; ++i) if (heaps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += heaps.memoryHeaps[i].size;
        ImGui::Text("Device-local memory heaps: %.2f GiB (may be shared)", double(local) / GiB);
        int fw = 0, fh = 0;
#ifdef __ANDROID__
        fw = (int)renderer_.width(); fh = (int)renderer_.height();
#else
        if (window_.handle()) window_.framebufferSize(fw, fh);
#endif
        ImGui::Text("Framebuffer: %d x %d | FPS: %.1f | frame: %.2f ms", fw, fh, ImGui::GetIO().Framerate, ImGui::GetIO().Framerate > 0 ? 1000.f / ImGui::GetIO().Framerate : 0.f);
        ImGui::Text("Renderer CPU: %.2f ms | Draws: %u | Triangles: %u", renderer_.stats.cpuMs, renderer_.stats.drawCalls, renderer_.stats.triangles);
        ImGui::TextDisabled("No GPU timing or storage-medium detection is available yet.");
    }
    if (ImGui::CollapsingHeader("Showcase", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)showcases_.size(); ++i) if (ImGui::RadioButton(showcases_[i].name.c_str(), showcase_ == i)) setShowcase(i);
        Entity* h = showcases_[showcase_].hero;
        if (h && !h->model->clips.empty() && !h->mm) {
            if (ImGui::BeginCombo("Clip", h->clip >= 0 ? h->model->clips[h->clip].name.c_str() : "none")) {
                for (int i = 0; i < (int)h->model->clips.size(); ++i) if (ImGui::Selectable(h->model->clips[i].name.c_str(), i == h->clip)) { h->clip = i; h->clipTime = 0; }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox("Skeleton", &skeleton_); ImGui::SameLine(); ImGui::Checkbox("MM trajectory", &trajectory_);
    }
    if (ImGui::CollapsingHeader("Time of day / sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Time of day drives sun, sky, fog, ambient", &todEnabled_);
        ImGui::SliderFloat("Hour", &tod_, 0.f, 24.f, "%.2f h"); ImGui::Checkbox("Auto advance", &todAuto_); ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::SliderFloat("hours/s", &todSpeed_, 0.05f, 3.f);
        if (ImGui::Button("Morning")) tod_ = 8.f; ImGui::SameLine(); if (ImGui::Button("Noon")) tod_ = 12.f; ImGui::SameLine(); if (ImGui::Button("Afternoon")) tod_ = 15.f; ImGui::SameLine();
        if (ImGui::Button("Sunset")) tod_ = 17.8f; ImGui::SameLine(); if (ImGui::Button("Night")) tod_ = 22.f;
        ImGui::SliderFloat("Cloud cover", &cloud_, 0.f, 1.f); ImGui::Checkbox("Street lamps at night", &lampsOn_);
        if (ImGui::Checkbox("HDRI image-based lighting (otherwise procedural sky IBL)", &hdriMode_)) {
            if (hdriMode_) { std::error_code ec; for (auto& f : fs::directory_iterator(root_ + "/assets/env", ec)) if (f.path().extension() == ".hdr") { loadEnvironment(f.path().string()); break; } }
            else loadEnvironment("procedural");
        }
        ImGui::Text("sun elevation %.1f deg", todSun_);
    }
    if (ImGui::CollapsingHeader("Post-processing (HDR)")) {
        ImGui::Checkbox("Enabled", &post_.enabled); ImGui::SameLine(); ImGui::Checkbox("Bloom", &post_.bloom);
        ImGui::Combo("Tonemap", &post_.tonemap, "Clamp\0Khronos PBR Neutral\0ACES (Narkowicz)\0");
        ImGui::SliderFloat("Bloom intensity", &post_.bloomIntensity, 0.f, 1.5f); ImGui::SliderFloat("Bloom threshold", &post_.bloomThreshold, 0.2f, 3.f); ImGui::SliderFloat("Bloom knee", &post_.bloomKnee, 0.f, 1.f);
        ImGui::SliderFloat("Saturation", &post_.saturation, 0.f, 2.f); ImGui::SliderFloat("Contrast", &post_.contrast, 0.5f, 1.5f);
        ImGui::SliderFloat("Vignette", &post_.vignette, 0.f, 1.f); ImGui::SliderFloat("Film grain", &post_.grain, 0.f, 0.1f);
        {
            const char* cur = lutIdx_ >= 0 ? lutNames_[lutIdx_].c_str() : "(none)";
            if (ImGui::BeginCombo("Colour grade (LUT)", cur)) {
                if (ImGui::Selectable("(none)", lutIdx_ < 0)) setLut(-1);
                for (int i = 0; i < (int)lutNames_.size(); ++i) if (ImGui::Selectable(lutNames_[i].c_str(), lutIdx_ == i)) setLut(i);
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("LUT strength", &lutStrength_, 0.f, 1.f);
        }
        ImGui::ColorEdit3("Shadow tint", &post_.shadowTint.x, ImGuiColorEditFlags_NoInputs); ImGui::SameLine(); ImGui::ColorEdit3("Highlight tint", &post_.highlightTint.x, ImGuiColorEditFlags_NoInputs);
    }
    if (ImGui::CollapsingHeader("Lighting / Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Pure NPR look (every material uses the NPR shading path)", &nprFull_);
        ImGui::SliderFloat("Light azimuth", &lightAz_, 0, 360); ImGui::SliderFloat("Light elevation", &lightEl_, 5, 85);
        ImGui::Checkbox("Auto-rotate light", &lightAuto_);
        ImGui::SliderFloat("Intensity", &light_.intensity, 0, 3); ImGui::ColorEdit3("Light color", &light_.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit3("Sky", &light_.sky.x, ImGuiColorEditFlags_NoInputs); ImGui::SameLine(); ImGui::ColorEdit3("Ground", &light_.ground.x, ImGuiColorEditFlags_NoInputs);
        ImGui::Text("Environment: %s", envName_.c_str());
        ImGui::Checkbox("Light follows the HDRI sun (otherwise the HDRI is rotated to the light)", &hdriSun_);
        ImGui::SliderFloat("Env intensity", &light_.envIntensity, 0, 3); ImGui::SliderFloat("Env rotation", &light_.envRotation, 0, 6.283f);
        if (ImGui::Button("Procedural sky")) loadEnvironment("procedural");
        { std::error_code ec; for (auto& f : fs::directory_iterator(root_ + "/assets/env", ec)) if (f.path().extension() == ".hdr") { ImGui::SameLine(); if (ImGui::Button(f.path().stem().string().substr(0, 14).c_str())) loadEnvironment(f.path().string()); } }
        ImGui::Checkbox("Extra point lights (shader lab)", &pointLightsOn_);
        ImGui::Checkbox("Point-light shadows", &opt_.pointShadows);
        ImGui::TextDisabled("%u / %d shadowed point lights  |  %u cube faces", renderer_.stats.pointShadowLights, kMaxPointLights, renderer_.stats.pointShadowFaces);
        ImGui::SliderFloat("Ambient", &light_.ambient, 0, 2); ImGui::SliderFloat("Exposure", &userExposure_, 0.2f, 3);
        ImGui::SliderFloat("Shadow bias", &light_.shadowBias, 0, 0.01f, "%.4f"); ImGui::Checkbox("Fit near cascade to camera", &opt_.fitCascade); ImGui::SliderFloat("Shadow radius (near cascade / split)", &light_.shadowRadius, 2, 20); ImGui::SliderFloat("Far cascade radius", &light_.shadowFarRadius, 12, 45);
        ImGui::SliderFloat("Fog", &light_.fogDensity, 0, 0.08f, "%.3f"); ImGui::SliderFloat("Outline scale", &light_.outlineScale, 0, 4);
        if (ImGui::Checkbox("Rain (Interact)", &rainOn_)) rain_ = rainOn_ ? 1.f : 0.f;
        if (rainOn_) ImGui::SliderFloat("Wetness", &rain_, 0, 1);
        ImGui::Checkbox("Split view: left = hybrid, right = pure NPR", &split_); if (split_) ImGui::SliderFloat("Split offset (px)", &splitOffset_, -800, 800);
        ImGui::SliderFloat("Camera distance", &camera_.distance, 1.2f, 14.f);
        int dm = debugMode_; if (ImGui::Combo("Shader debug view (Attack)", &dm, "0 Final\0" "1 Base\0" "2 Param\0" "3 Style\0" "4 Mask\0" "5 Face SDF RGB\0" "6 SDF channel\0" "7 SDF shadow\0" "8 Realtime shadow\0" "9 Ramp shadow\0" "10 Normal\0" "11 N.L\0" "12 Shadow atten\0" "13 UV\0")) debugMode_ = dm;
    }
    drawCharacterUI();
    if (ImGui::CollapsingHeader("Material inspector (Sakuga/Hybrid)")) {
        auto mats = activeMaterials();
        drawMaterialInspector(renderer_, mats, selectedMat_, syncMat_);
    }
    if (vrm_ && vrm_->mm && ImGui::CollapsingHeader("Motion matching")) {
        auto& mm = *vrm_->mm; auto& s = mm.settings; auto& d = mm.debug; const MotionDatabase* db = mm.database();
        const bool validClip = db && d.clip >= 0 && d.clip < (int)db->clipNames.size();
        ImGui::Text("Animation %d / %d: %s", validClip ? d.clip + 1 : 0, db ? (int)db->clipNames.size() : 0,
                    validClip ? db->clipNames[d.clip].c_str() : "none");
        ImGui::Text("Frame %d  |  Matcher transitions %d  |  Mode: %s", d.frame, d.transitions, mm.browsing() ? "clip browser" : "matching");
        ImGui::Text("cost best %.3f  current %.3f  speed %.2f m/s", d.cost, d.currentCost, glm::length(d.velocity));
        ImGui::SliderFloat("Search interval", &s.searchInterval, 0.02f, 0.5f); ImGui::SliderFloat("Switch margin", &s.switchMargin, 0, 1);
        ImGui::SliderFloat("Inertialization half-life", &s.inertHalflife, 0.02f, 0.4f);
        ImGui::SliderFloat("Velocity half-life", &s.velHalflife, 0.05f, 0.6f); ImGui::SliderFloat("Yaw half-life", &s.yawHalflife, 0.05f, 0.6f);
        const char* g[] = {"w: foot pos", "w: foot vel", "w: hip vel", "w: traj pos", "w: traj dir"};
        for (int i = 0; i < FG_Count; ++i) ImGui::SliderFloat(g[i], &s.weights[i], 0, 3);
        ImGui::SliderFloat("Walk speed", &s.walkSpeed, 0.5f, 3); ImGui::SliderFloat("Run speed (Sprint)", &s.runSpeed, 2, 6); ImGui::SliderFloat("Slow speed (Crouch)", &s.slowSpeed, 0.3f, 2);
        ImGui::SeparatorText("Foot IK (two-bone, foot planting)");
        ImGui::Checkbox("Enable foot IK", &vrm_->ik.enabled); ImGui::SliderFloat("IK weight", &vrm_->ik.weight, 0, 1);
        ImGui::SliderFloat("Contact height", &vrm_->ik.contactHeight, 0.02f, 0.2f); ImGui::SliderFloat("Contact speed", &vrm_->ik.contactSpeed, 0.1f, 1.5f);
        ImGui::Text("planted: L %s (%.1f cm)  R %s (%.1f cm)", vrm_->ikState.foot[0].locked ? "yes" : "no", vrm_->ikState.debugLift[0] * 100, vrm_->ikState.foot[1].locked ? "yes" : "no", vrm_->ikState.debugLift[1] * 100);
        ImGui::SeparatorText("LAFAN1 families the search may pick");
        for (int c = 0; c < kMotionCategoryCount; ++c) {
            bool on = (s.categoryMask >> c) & 1u; char lbl[96]; std::snprintf(lbl, sizeof lbl, "%s (%d clips)", kMotionCategoryNames[c], db->clipsInCategory(c));
            if (ImGui::Checkbox(lbl, &on)) s.categoryMask = on ? (s.categoryMask | (1u << c)) : (s.categoryMask & ~(1u << c));
        }
        ImGui::SeparatorText("Mocap browser (click a clip to play it, move to return to matching)");
        static char filter[48] = ""; ImGui::InputText("filter", filter, sizeof filter);
        if (ImGui::BeginListBox("##clips", ImVec2(-1, 160))) {
            for (size_t c = 0; c < db->clipNames.size(); ++c) {
                if (filter[0] && db->clipNames[c].find(filter) == std::string::npos) continue;
                if (ImGui::Selectable(db->clipNames[c].c_str(), mm.browsing() && d.clip == (int)c)) mm.playClip((int)c, vrm_->pos, vrm_->yaw);
            }
            ImGui::EndListBox();
        }
        if (ImGui::Button("Reset character")) { vrm_->pos = {0, 0, 0}; vrm_->yaw = 0; mm.reset(vrm_->pos, vrm_->yaw); }
    }
    if (ImGui::CollapsingHeader("Input actions (from InputSystem_Actions.inputactions)")) {
        ImGui::Text("gamepads connected: %d (all merged; hot-plug supported)", input_.device().padCount);
        for (auto& n : window_.padNames()) ImGui::BulletText("%s", n.c_str());
        ImGui::Checkbox("Touch controls (mouse = touch 0)", &touch_.enabled); ImGui::SameLine(); ImGui::SliderFloat("scale", &touch_.uiScale, 0.6f, 2.f);
        for (auto& a : input_.actions()) {
            if (a.map != "Player") continue;
            if (a.control == "Vector2") ImGui::Text("%-9s (%+.2f, %+.2f)", a.name.c_str(), a.value.x, a.value.y);
            else ImGui::Text("%-9s %s", a.name.c_str(), a.held ? "HELD" : "-");
        }
        ImGui::TextDisabled("1/2 prev/next showcase | E rain | LMB/Enter debug view | Space auto-light | Shift sprint | C slow");
    }
    ImGui::End();
}

#ifndef __ANDROID__
void Sandbox::run() {
    auto last = std::chrono::steady_clock::now();
    std::vector<uint8_t> pixels;
    int recorded = 0;
    if (!opt_.recordDir.empty()) fs::create_directories(opt_.recordDir);
    while (true) {
        float dt;
        DeviceState dev;
        if (opt_.headless) {
            dt = opt_.fixedDt;
            float t = frame_ * dt; glm::vec2 mv(0); bool sprint = false, crouch = false;
            for (auto& k : opt_.script) if (k.t <= t) { mv = k.move; sprint = k.sprint; crouch = k.crouch; }
            dev.key[GLFW_KEY_W] = mv.y > 0.5f; dev.key[GLFW_KEY_S] = mv.y < -0.5f; dev.key[GLFW_KEY_D] = mv.x > 0.5f; dev.key[GLFW_KEY_A] = mv.x < -0.5f;
            dev.key[GLFW_KEY_LEFT_SHIFT] = sprint; dev.key[GLFW_KEY_C] = crouch;
        } else {
            if (window_.shouldClose()) break;
            window_.poll();
            auto now = std::chrono::steady_clock::now();
            float wallDt = std::chrono::duration<float>(now - last).count(); last = now;
            dt = std::min(0.05f, wallDt);
            if (frame_ > 0) fps_ = frameTiming_.update(wallDt);
            dev = window_.device();
            {   // touch controls: the mouse acts as touch id 0 (desktop GLFW has no multi-touch); real touch front ends call touch_.injectTouch()
                int fw, fh; window_.framebufferSize(fw, fh); touch_.setScreen((float)fw, (float)fh);
                if (touch_.enabled && touchMouse_) touch_.injectTouch(0, dev.mousePos.x, dev.mousePos.y, dev.mouseButton[0] && !(renderer_.uiEnabled() && ImGui::GetIO().WantCaptureMouse && !touch_.anyActive()));
                touch_.apply(dev);
                if (touch_.enabled && touchMouse_ && touch_.anyActive()) dev.mouseButton[0] = false;
            }
            bool uiWantsKeys = renderer_.uiEnabled() && ImGui::GetIO().WantCaptureKeyboard;
            if (uiWantsKeys) { std::memset(dev.key, 0, sizeof(dev.key)); }
            if (renderer_.uiEnabled() && ImGui::GetIO().WantCaptureMouse && !window_.cursorCaptured()) { std::memset(dev.mouseButton, 0, sizeof(dev.mouseButton)); dev.mouseDelta = {0, 0}; }
            static bool tabPrev = false, f1Prev = false, f2Prev = false, f3Prev = false, f11Prev = false, f12Prev = false;
            bool tab = window_.device().key[GLFW_KEY_TAB] && !uiWantsKeys, f1 = window_.device().key[GLFW_KEY_F1], f2 = window_.device().key[GLFW_KEY_F2], f3 = window_.device().key[GLFW_KEY_F3];
            bool f11 = window_.device().key[GLFW_KEY_F11], f12 = window_.device().key[GLFW_KEY_F12] && !uiWantsKeys;
            if (tab && !tabPrev) window_.setCursorCaptured(!window_.cursorCaptured());
            if (f1 && !f1Prev) showUI_ = !showUI_;
            if (f2 && !f2Prev) screenshotRequested_ = true;
            if (f3 && !f3Prev) showFps_ = !showFps_;
            if (f11 && !f11Prev) showImGuiDemo_ = !showImGuiDemo_;
            if (f12 && !f12Prev) showThemeEditor_ = !showThemeEditor_;
            tabPrev = tab; f1Prev = f1; f2Prev = f2; f3Prev = f3; f11Prev = f11; f12Prev = f12;
            if (window_.device().key[GLFW_KEY_ESCAPE]) break;
        }
        input_.update(dev);
        handleActions(dt);
        updateScene(dt);
        FrameInput fi = buildFrame(dt);
        bool lastFrame = opt_.headless || !opt_.screenshot.empty() ? frame_ + 1 >= opt_.frames : false;
        bool record = !opt_.recordDir.empty() && (frame_ % std::max(1, opt_.recordEvery) == 0);
        if (!opt_.headless) drawUI();
        std::vector<uint8_t>* cap = (lastFrame && !opt_.screenshot.empty()) || record || screenshotRequested_ ? &pixels : nullptr;
        if (opt_.headless && !cap) { ++frame_; continue; }          // simulate only: nothing needs to be drawn
        bool ok = renderer_.renderFrame(fi, cap);
        if (ok && cap) {
            if (record) { char name[512]; std::snprintf(name, sizeof name, "%s/frame_%05d.png", opt_.recordDir.c_str(), recorded++); Renderer::savePNG(name, pixels, renderer_.width(), renderer_.height()); }
            if (screenshotRequested_) { screenshotRequested_ = false; std::string p = "screenshot_" + std::to_string(frame_) + ".png"; Renderer::savePNG(p, pixels, renderer_.width(), renderer_.height()); SK_LOG("saved %s", p.c_str()); }
            if (lastFrame && !opt_.screenshot.empty()) { Renderer::savePNG(opt_.screenshot, pixels, renderer_.width(), renderer_.height()); SK_LOG("saved %s", opt_.screenshot.c_str()); }
        }
        ++frame_;
        if (lastFrame) break;
    }
}

#endif
void Sandbox::shutdown() {
    if (opt_.headless && std::getenv("SAKUGA_DUMP"))
        for (Entity* e : showcases_[showcase_].entities) if (e->mm || e == vrm_) std::fprintf(stderr, "entity %-28s pos %6.2f %5.2f %6.2f yaw %5.1f vel %.2f mm frame %d clip %d cost %.2f armDelta %.2f hasLook %d\n", e->name.c_str(), e->pos.x, e->pos.y, e->pos.z, glm::degrees(e->yaw), glm::length(e->velocity), e->mm ? e->mm->debug.frame : -1, e->mm ? e->mm->debug.clip : -1, e->mm ? e->mm->debug.cost : 0.f, [&]{ int n = e->model->rig.bone(HumanBone::LUpperArm); return n < 0 ? -1.f : glm::degrees(glm::angle(glm::normalize(e->pose.r[n] * glm::inverse(e->model->rig.nodes[n].r)))); }(), (int)e->hasLookTarget);

    renderer_.shutdown(); ctx_.shutdown();
    if (window_.handle()) window_.destroy();
}

#ifndef __ANDROID__
static int touchSelfTest() {
    TouchControls t; t.enabled = true; t.setScreen(1280, 720); DeviceState d;
    t.injectTouch(1, 200, 500, true); t.apply(d); t.injectTouch(1, 290, 450, true);          // stick: drag right + up
    t.injectTouch(2, 900, 300, true); t.apply(d); t.injectTouch(2, 940, 280, true);          // look drag
    DeviceState d2; t.apply(d2); glm::vec2 st = t.stick();
    std::printf("stick (%.2f, %.2f) -> padAxis LX %.2f LY %.2f | look delta (%.1f, %.1f)\n", st.x, st.y, d2.padAxis[0], d2.padAxis[1], d2.mouseDelta.x, d2.mouseDelta.y);
    t.injectTouch(3, 1100, 560, true); DeviceState d3; t.apply(d3);
    bool any = false; for (int i = 0; i < TouchControls::kButtons; ++i) any |= t.buttonHeld(i);
    std::printf("touch button held: %d, padConnected %d\n", (int)any, (int)d3.padConnected);
    return (st.x > 0.3f && st.y < -0.3f && d2.mouseDelta.x > 30.f) ? 0 : 1;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--touch-selftest") return touchSelfTest();
    // Writes a theme preset to JSON and exits - no window, no device. This is how config/ui_theme.json is produced:
    //   sandbox --dump-ui-theme config/ui_theme.json ["Sakuga Dark"]
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--dump-ui-theme") {
        const std::string out = i + 1 < argc && argv[i + 1][0] != '-' ? argv[i + 1] : "ui_theme.json";
        const std::string preset = i + 2 < argc ? argv[i + 2] : uiThemeDefaultName();
        UiTheme t = uiThemePreset(preset);
        std::error_code ec; fs::create_directories(fs::path(out).parent_path(), ec);
        std::ofstream f(out, std::ios::binary);
        f << uiThemeToJson(t);
        f.close();
        if (!f.good()) { SK_ERR("could not write %s", out.c_str()); return 1; }
        SK_LOG("wrote %s (%s, %zu colours)", out.c_str(), t.name.c_str(), t.colors.size());
        return 0;
    }
    Options opt = parseArgs(argc, argv);
    std::string root = findRoot(argv[0]);
    SK_LOG("Sakuga Engine sandbox | root: %s", root.c_str());
    std::string shaders = exeDir(argv[0]) + "/shaders";
    if (!fs::exists(shaders)) shaders = root + "/build/shaders";
    Sandbox app(opt, root, shaders);
    if (!app.init()) { SK_ERR("initialisation failed"); return 1; }
    app.run();
    app.shutdown();
    return 0;
}

#endif // !__ANDROID__

#ifdef __ANDROID__
bool Sandbox::initMobile(ANativeWindow* window, std::string& error) {
    mobileWindow_ = window;
    if (!init()) { error = mobileError_.empty() ? "Sandbox initialization failed" : mobileError_; return false; }
    if (!vrm_ || !vrm_->mm || !vrm_->mmDb) { error = "Sandbox avatar or motion library failed to load"; return false; }
    if (vrm_->mmDb->clipNames.size() != 77) { error = "The complete 77-clip motion library could not be loaded"; return false; }
    // Conservative starting values, editable in the mobile panel and the full sandbox UI.
    post_.bloom = false; light_.pointShadows = false; showUI_ = false; showFps_ = false;
    touch_.enabled = true; touch_.setScreen((float)renderer_.width(), (float)renderer_.height());
    return true;
}
static int mobileClipIndex(const Entity* e) {
    if (!e || !e->mm || !e->mmDb || e->mmDb->frameClip.empty()) return -1;
    int frame = std::clamp((int)e->mm->currentFrame(), 0, (int)e->mmDb->frameClip.size() - 1);
    return e->mmDb->frameClip[frame];
}
int Sandbox::frameMobile(float dt, float wallDt, float x, float y, int clip) {
    if (!std::isfinite(x) || !std::isfinite(y)) x = y = 0.f;
    (void)x; (void)y; (void)clip;
    if (mobileLoadNpcs_) {
        mobileLoadNpcs_ = false;
        loadNpcModels();
        for (Showcase& sc : showcases_) if (sc.lab) { spawnNpcs(sc); break; }
        setShowcase(showcase_);
        mobileNpcsPendingFrame_ = !lab_.npcs.empty();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox npcs loaded %zu", lab_.npcs.size());
    }
    touch_.setScreen((float)renderer_.width(), (float)renderer_.height());
    DeviceState dev; touch_.apply(dev);
    input_.update(dev); handleActions(dt);
    const glm::vec3 previousPos = vrm_ ? vrm_->pos : glm::vec3(0);
    updateScene(dt);
    if (!mobileLoggedMove_ && vrm_ && glm::length(touch_.stick()) > .2f && glm::length(vrm_->pos - previousPos) > 1e-5f) {
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox movement reached the avatar"); mobileLoggedMove_ = true;
    }
    FrameInput fi = buildFrame(dt);
    if (frame_ > 0) fps_ = frameTiming_.update(wallDt);
    drawUI(wallDt);
    const float density = opt_.uiFontSize / 14.f;
    ImGui::SetNextWindowPos(ImVec2(renderer_.width() * .65f, 10.f * density), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(renderer_.width() * .34f, renderer_.height() * .58f), ImGuiCond_Always);
    ImGui::Begin("SAKUGA / MOBILE", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Sandbox"); ImGui::SameLine();
    if (ImGui::Button((tod_ >= 19.f || tod_ < 5.f) ? "DAY" : "NIGHT")) {
        todEnabled_ = true; tod_ = (tod_ >= 19.f || tod_ < 5.f) ? 15.f : 22.f;
    }
    if (frame_ == 0) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox night target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
    }
    ImGui::SameLine(); ImGui::Text("%.0f FPS", fps_);
    ImGui::Text("Clip %d/%d", vrm_ && vrm_->mm ? vrm_->mm->debug.clip + 1 : 0, vrm_ && vrm_->mmDb ? (int)vrm_->mmDb->clipNames.size() : 0);
    ImGui::SameLine();
    if (ImGui::Button("NEXT") && vrm_ && vrm_->mm && vrm_->mmDb && !vrm_->mmDb->clipNames.empty()) {
        const int next = (std::max(-1, vrm_->mm->debug.clip) + 1) % (int)vrm_->mmDb->clipNames.size();
        vrm_->mm->playClip(next, vrm_->pos, vrm_->yaw);
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox clip selected %d", next);
    }
    if (frame_ == 0) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox next target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
    }
    ImGui::SameLine();
    if (ImGui::Button("CLIPS")) { mobileClipBrowser_ = !mobileClipBrowser_; mobileBrowserTargetLogged_ = false; mobileLastItemLogged_ = false; }
    if (frame_ == 0) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox clips target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
    }
    if (ImGui::Checkbox("Bloom", &post_.bloom)) __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "imgui bloom changed");
    if (!mobileLoggedControls_) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "imgui bloom target x=%d y=%d", (int)(a.x + (b.y-a.y)*.5f), (int)((a.y+b.y)*.5f));
        mobileLoggedControls_ = true;
    }
    ImGui::SameLine();
    if (!mobileNpcsRequested_) {
        if (ImGui::Button("NPCs")) { mobileNpcsRequested_ = true; mobileLoadNpcs_ = true; }
        if (frame_ == 0) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox npcs target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
        }
    } else ImGui::TextUnformatted(lab_.npcs.empty() ? "NPCs unavailable" : "NPCs on");
    ImGui::Checkbox("Point shadows", &opt_.pointShadows);
    if (frame_ == 0) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox shadows target x=%d y=%d", (int)(a.x + (b.y-a.y)*.5f), (int)((a.y+b.y)*.5f));
    }
    ImGui::SameLine();
    if (ImGui::Button("UI theme")) showThemeEditor_ = !showThemeEditor_;
    if (ImGui::Checkbox("Sandbox editor", &showUI_)) __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox editor toggled");
    if (frame_ == 0) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox editor target x=%d y=%d", (int)(a.x + (b.y-a.y)*.5f), (int)((a.y+b.y)*.5f));
    }
    if (vrm_) {
        if (ImGui::Checkbox("Hair physics", &vrm_->spring.enabled)) {
            vrm_->springState.reset();
            __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "hair physics enabled=%d chains=%zu", (int)vrm_->spring.enabled, vrm_->model->springs.chains.size());
        }
        if (frame_ == 0) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "hair physics target x=%d y=%d", (int)(a.x + (b.y-a.y)*.5f), (int)((a.y+b.y)*.5f));
        }
        ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(40.f * density, ImGui::GetContentRegionAvail().x));
        ImGui::SliderFloat("##upright", &vrm_->posture.hunchCorrection, 0.f, 1.f, "Upright %.2f");
    }
    ImGui::End();
    if (mobileClipBrowser_ && vrm_ && vrm_->mm && vrm_->mmDb) {
        const auto& names = vrm_->mmDb->clipNames;
        constexpr int perPage = 6;
        const int pages = std::max(1, ((int)names.size() + perPage - 1) / perPage);
        mobileClipPage_ = std::clamp(mobileClipPage_, 0, pages - 1);
        ImGui::SetNextWindowPos(ImVec2(renderer_.width() * .03f, renderer_.height() * .08f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(renderer_.width() * .56f, renderer_.height() * .80f), ImGuiCond_Always);
        ImGui::Begin("Animations", &mobileClipBrowser_, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::Button("FIRST")) mobileClipPage_ = 0; ImGui::SameLine();
        if (ImGui::Button("PREV")) mobileClipPage_ = std::max(0, mobileClipPage_ - 1); ImGui::SameLine();
        ImGui::Text("%d / %d", mobileClipPage_ + 1, pages); ImGui::SameLine();
        if (ImGui::Button("NEXT")) mobileClipPage_ = std::min(pages - 1, mobileClipPage_ + 1); ImGui::SameLine();
        if (ImGui::Button("LAST")) mobileClipPage_ = pages - 1;
        if (!mobileBrowserTargetLogged_) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "browser last target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
            mobileBrowserTargetLogged_ = true;
        }
        ImGui::TextDisabled("Tap to play; move to resume matching.");
        ImGui::BeginChild("clip-items", ImVec2(0, 0), true);
        for (int i = mobileClipPage_ * perPage; i < std::min((int)names.size(), (mobileClipPage_ + 1) * perPage); ++i) {
            if (ImGui::Selectable(names[i].c_str(), vrm_->mm->browsing() && mobileClipIndex(vrm_) == i)) {
                vrm_->mm->playClip(i, vrm_->pos, vrm_->yaw);
                __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox browser selected %d of %zu", i, names.size());
                mobileClipBrowser_ = false;
            }
            if (i == (int)names.size() - 1 && !mobileLastItemLogged_) {
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "browser final clip target x=%d y=%d", (int)((a.x+b.x)*.5f), (int)((a.y+b.y)*.5f));
                mobileLastItemLogged_ = true;
            }
        }
        ImGui::EndChild(); ImGui::End();
    }
    // The UI changes are reflected on the next frame, as with desktop sandbox controls.
    ++frame_;
    bool rendered = renderer_.renderFrame(fi);
    if (rendered && mobileRestored_) {
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "surface state frame bloom=%d editor=%d tod=%.2f clip=%d browse=%d npcs=%zu x=%.3f z=%.3f",
                            (int)post_.bloom, (int)showUI_, tod_, mobileClipIndex(vrm_), vrm_ && vrm_->mm && vrm_->mm->browsing(),
                            lab_.npcs.size(), vrm_ ? vrm_->pos.x : 0.f, vrm_ ? vrm_->pos.z : 0.f);
        mobileRestored_ = false;
    }
    if (rendered && !mobileLoggedPointShadows_ && renderer_.stats.pointShadowLights == 4 && renderer_.stats.pointShadowFaces == 24) {
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox point shadow atlas: 4 lights 24 faces"); mobileLoggedPointShadows_ = true;
    }
    if (rendered && mobileNpcsPendingFrame_) {
        __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "sandbox npcs frame presented"); mobileNpcsPendingFrame_ = false;
    }
    return rendered ? 1 : 0;
}
void Sandbox::touchMobile(bool, int action, int id, float x, float y) {
    if (action == 3) {
        mobilePointers_.cancel(); touch_.cancel();
        if (renderer_.uiEnabled()) ImGui::GetIO().AddMouseButtonEvent(0, false);
        return;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    MobilePointerRouter::Route route;
    if (action == 0) {
        const bool overUi = renderer_.uiEnabled() && mobileHitsImGui(ImVec2(x, y));
        route = mobilePointers_.down(id, overUi);
    } else if (action == 2) route = mobilePointers_.up(id);
    else route = mobilePointers_.get(id);
    if (route == MobilePointerRouter::Route::Game) {
        touch_.injectTouch(id, x, y, action != 2);
    } else if (route == MobilePointerRouter::Route::Ui && renderer_.uiEnabled()) {
        ImGuiIO& io = ImGui::GetIO(); io.AddMousePosEvent(x, y);
        if (action == 0) io.AddMouseButtonEvent(0, true);
        else if (action == 2) io.AddMouseButtonEvent(0, false);
    }
}
MobileSnapshot Sandbox::captureMobile() const {
    MobileSnapshot s; s.options = opt_; s.camera = camera_; s.light = light_; s.post = post_;
    s.theme = uiThemeFromLiveStyle(ImGui::GetStyle(), themeName_); s.theme.fontScale = uiTheme_.fontScale;
    s.themeName = themeName_; s.ini = ImGui::SaveIniSettingsToMemory(); s.environment = envPath_;
    s.showTheme = showThemeEditor_; s.showDemo = showImGuiDemo_; s.npcs = mobileNpcsRequested_;
    s.touchEnabled = touch_.enabled; s.touchScale = touch_.uiScale; s.touchMouse = touchMouse_;
    s.showcase = showcase_;
    s.selectedMat = selectedMat_;
    s.lutIdx = lutIdx_;
    s.debugMode = debugMode_;
    s.matForcedMode = matForcedMode_;
    s.tod = tod_;
    s.todSpeed = todSpeed_;
    s.userExposure = userExposure_;
    s.userEnv = userEnv_;
    s.cloud = cloud_;
    s.cameraShoulder = cameraShoulder_;
    s.lutStrength = lutStrength_;
    s.lightAz = lightAz_;
    s.lightEl = lightEl_;
    s.time = time_;
    s.rain = rain_;
    s.splitOffset = splitOffset_;
    s.todAuto = todAuto_;
    s.todEnabled = todEnabled_;
    s.hdriMode = hdriMode_;
    s.nprFull = nprFull_;
    s.lampsOn = lampsOn_;
    s.pointLightsOn = pointLightsOn_;
    s.navDebug = navDebug_;
    s.agentDebug = agentDebug_;
    s.mmDebug = mmDebug_;
    s.showFps = showFps_;
    s.hdriSun = hdriSun_;
    s.lightAuto = lightAuto_;
    s.rainOn = rainOn_;
    s.skeleton = skeleton_;
    s.trajectory = trajectory_;
    s.showUI = showUI_;
    s.split = split_;
    s.syncMat = syncMat_;
    for (const auto& owned : entities_) {
        const Entity& e = *owned; MobileEntityState x;
        x.name = e.name; x.modelPath = e.model->path; x.pos = e.pos; x.yaw = e.yaw; x.scale = e.scale;
        x.clip = e.clip; x.clipTime = e.clipTime; x.clipSpeed = e.clipSpeed; x.loop = e.loop; x.autoBlink = e.autoBlink;
        x.ik = e.ik; x.hands = e.hands; x.look = e.look; x.spring = e.spring;
        x.springTuning = e.springTuning; x.posture = e.posture; x.blink = e.blink; x.exprTarget = e.exprTarget; x.exprCur = e.exprCur; x.exprFinal = e.exprFinal;
        if (e.mm) {
            x.hasMatcher = true; x.matcher = e.mm->settings; x.browsing = e.mm->browsing();
            x.browserClip = mobileClipIndex(&e);
        }
        for (const Material* m : e.model->materials) x.materials.emplace_back(m->name, m->params);
        s.entities.push_back(std::move(x));
    }
    __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "surface state saved bloom=%d editor=%d tod=%.2f clip=%d browse=%d npcs=%zu x=%.3f z=%.3f",
                        (int)s.post.bloom, (int)s.showUI, s.tod, mobileClipIndex(vrm_), vrm_ && vrm_->mm && vrm_->mm->browsing(),
                        lab_.npcs.size(), vrm_ ? vrm_->pos.x : 0.f, vrm_ ? vrm_->pos.z : 0.f);
    return s;
}
void Sandbox::restoreMobile(const MobileSnapshot& s) {
    if (s.npcs && !mobileNpcsRequested_) {
        mobileNpcsRequested_ = true; loadNpcModels();
        if (!showcases_.empty() && showcases_.back().lab) spawnNpcs(showcases_.back());
    }
    setShowcase(s.showcase);
    opt_ = s.options; camera_ = s.camera; light_ = s.light; post_ = s.post;
    uiTheme_ = s.theme; std::snprintf(themeName_, sizeof themeName_, "%s", s.themeName.c_str());
    renderer_.setUiTheme(uiTheme_); ImGui::LoadIniSettingsFromMemory(s.ini.c_str(), s.ini.size());
    showThemeEditor_ = s.showTheme; showImGuiDemo_ = s.showDemo;
    touch_.enabled = s.touchEnabled; touch_.uiScale = s.touchScale; touchMouse_ = s.touchMouse;
    if (s.environment != envPath_) loadEnvironment(s.environment);
    setLut(s.lutIdx);
    showcase_ = s.showcase;
    selectedMat_ = s.selectedMat;
    lutIdx_ = s.lutIdx;
    debugMode_ = s.debugMode;
    matForcedMode_ = s.matForcedMode;
    tod_ = s.tod;
    todSpeed_ = s.todSpeed;
    userExposure_ = s.userExposure;
    userEnv_ = s.userEnv;
    cloud_ = s.cloud;
    cameraShoulder_ = s.cameraShoulder;
    lutStrength_ = s.lutStrength;
    lightAz_ = s.lightAz;
    lightEl_ = s.lightEl;
    time_ = s.time;
    rain_ = s.rain;
    splitOffset_ = s.splitOffset;
    todAuto_ = s.todAuto;
    todEnabled_ = s.todEnabled;
    hdriMode_ = s.hdriMode;
    nprFull_ = s.nprFull;
    lampsOn_ = s.lampsOn;
    pointLightsOn_ = s.pointLightsOn;
    navDebug_ = s.navDebug;
    agentDebug_ = s.agentDebug;
    mmDebug_ = s.mmDebug;
    showFps_ = s.showFps;
    hdriSun_ = s.hdriSun;
    lightAuto_ = s.lightAuto;
    rainOn_ = s.rainOn;
    skeleton_ = s.skeleton;
    trajectory_ = s.trajectory;
    showUI_ = s.showUI;
    split_ = s.split;
    syncMat_ = s.syncMat;
    // Entity ordering is deterministic for a given asset set. Guard every entry against mismatches.
    for (size_t i = 0; i < s.entities.size() && i < entities_.size(); ++i) {
        const auto& x = s.entities[i]; Entity& e = *entities_[i];
        if (x.name != e.name || x.modelPath != e.model->path) continue;
        e.pos = x.pos; e.yaw = x.yaw; e.scale = x.scale; e.clip = x.clip; e.clipTime = x.clipTime; e.clipSpeed = x.clipSpeed; e.loop = x.loop; e.autoBlink = x.autoBlink;
        e.ik = x.ik; e.hands = x.hands; e.look = x.look; e.spring = x.spring; e.springTuning = x.springTuning;
        e.posture = x.posture; e.blink = x.blink;
        if (e.exprTarget.size() == x.exprTarget.size()) { e.exprTarget = x.exprTarget; e.exprCur = x.exprCur; e.exprFinal = x.exprFinal; }
        if (e.mm && x.hasMatcher) {
            e.mm->settings = x.matcher; e.mm->reset(e.pos, e.yaw);
            if (x.browsing && e.mmDb && x.browserClip >= 0 && x.browserClip < (int)e.mmDb->clipNames.size())
                e.mm->playClip(x.browserClip, e.pos, e.yaw);
        }
        for (size_t j = 0; j < x.materials.size() && j < e.model->materials.size(); ++j) {
            Material* m = e.model->materials[j];
            if (m->name == x.materials[j].first) { m->params = x.materials[j].second; renderer_.flushMaterial(m); }
        }
    }
    camSmooth_ = camera_.target; camUsed_ = -1.f; mobileRestored_ = true;
    __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "surface state restored bloom=%d editor=%d tod=%.2f clip=%d browse=%d npcs=%zu x=%.3f z=%.3f",
                        (int)post_.bloom, (int)showUI_, tod_, mobileClipIndex(vrm_), vrm_ && vrm_->mm && vrm_->mm->browsing(),
                        lab_.npcs.size(), vrm_ ? vrm_->pos.x : 0.f, vrm_ ? vrm_->pos.z : 0.f);
}
namespace sk {
class MobileSandbox {
public:
    std::unique_ptr<Sandbox> sandbox;
    std::string root; bool ready = false;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    ~MobileSandbox() { if (sandbox) { try { if (ready) mobileSnapshots.save(root, sandbox->captureMobile()); } catch (...) {} try { sandbox->shutdown(); } catch (...) {} } }
};
MobileSandbox* createMobileSandbox(ANativeWindow* window, const std::string& root, const std::string& shaders, float density, std::string& error) {
    auto m = std::make_unique<MobileSandbox>();
    try {
        Options opt;
        opt.npcs = false; opt.springs = false; opt.pointShadows = false; opt.mmCache = true;
        opt.uiFontSize = 14.f * std::clamp(density, .75f, 4.f);
        auto previous = mobileSnapshots.load(root);
        if (previous) opt = previous->options;
        m->root = root; m->sandbox = std::make_unique<Sandbox>(opt, root, shaders);
        if (!m->sandbox->initMobile(window, error)) return nullptr;
        if (previous) m->sandbox->restoreMobile(*previous);
        m->ready = true; m->last = std::chrono::steady_clock::now(); return m.release();
    } catch (const std::exception& e) { error = e.what(); return nullptr; }
}
int mobileSandboxFrame(MobileSandbox* m, float x, float y, int clip, std::string& error) {
    if (!m || !m->sandbox) { error = "Sandbox unavailable"; return -1; }
    try {
        auto now = std::chrono::steady_clock::now();
        float wallDt = std::max(std::chrono::duration<float>(now - m->last).count(), 1e-5f); m->last = now;
        return m->sandbox->frameMobile(std::min(wallDt, .05f), wallDt, x, y, clip);
    } catch (const std::exception& e) { error = e.what(); return -1; }
}
void mobileSandboxPause(MobileSandbox* m) {
    if (!m || !m->sandbox) return;
    m->sandbox->touchMobile(false, 3, -1, 0, 0);
}
void mobileSandboxResume(MobileSandbox* m) { if (m) m->last = std::chrono::steady_clock::now(); }
void mobileSandboxTouch(MobileSandbox* m, bool imgui, int action, int id, float x, float y) { if (m && m->sandbox) m->sandbox->touchMobile(imgui, action, id, x, y); }
void destroyMobileSandbox(MobileSandbox* m) { delete m; }
}
#endif
