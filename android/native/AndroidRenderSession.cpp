#include "AndroidRenderSession.h"
#include "gfx/Renderer.h"
#include "asset/GltfLoader.h"
#include "scene/Scene.h"
#include <android/native_window.h>
#include <android/log.h>
#include <imgui.h>
#include <cfloat>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>

namespace sk {
class AndroidRenderSession {
public:
    Context ctx;
    Renderer renderer;
    bool rendererStarted = false;
    GpuMesh* ground = nullptr;
    Material* material = nullptr;
    Entity hero;
    int selectedClip = -1;
    float density = 1.f;
    bool bloom = false, pointShadows = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), last = start;
    ~AndroidRenderSession() {
        try { if (rendererStarted) renderer.shutdown(); } catch (...) {}
        ctx.shutdown();
    }
};
AndroidRenderSession* createAndroidRenderSession(ANativeWindow* window, const std::string& shaderDir, const std::string& modelPath,
                                                  const std::vector<std::string>& motionPaths, float density, std::string& error) {
    auto s = std::make_unique<AndroidRenderSession>();
    try {
        if (!s->ctx.initAndroid(window, error)) return nullptr;
        int width = ANativeWindow_getWidth(window), height = ANativeWindow_getHeight(window);
        if (width <= 0 || height <= 0) { error = "Android surface has no size"; return nullptr; }
        s->rendererStarted = true;
        if (!s->renderer.initAndroid(s->ctx, window, width, height, shaderDir)) { error = "Renderer initialization failed"; return nullptr; }
        s->density = std::clamp(density, .75f, 4.f);
        s->renderer.initUI(nullptr, UiTheme{}, "", 14.f * s->density);
        auto style = createStyleTextures(s->renderer);
        GltfOptions options; options.maxTextureSize = 1024;
        std::shared_ptr<Model> model = loadGltfModel(s->renderer, style, modelPath, options);
        if (!model || !model->rig.isHumanoid()) { error = "Could not load the bundled humanoid VRM"; return nullptr; }
        s->hero.name = "Android avatar"; s->hero.init(model);
        s->hero.spring.enabled = false; // lower-cost default; this is not yet a mobile performance profile
        auto db = std::make_shared<MotionDatabase>();
        if (!db->buildFromFiles(model->rig, motionPaths, "", 0, &error) || db->clipNames.empty()) {
            if (error.empty()) error = "Could not load motion clips for the avatar"; return nullptr;
        }
        s->hero.mmDb = db; s->hero.mm = std::make_unique<MotionMatcher>();
        s->hero.mm->init(&model->rig, db.get()); s->hero.mm->playClip(0, s->hero.pos, s->hero.yaw); s->selectedClip = 0;
        s->material = s->renderer.createMaterial("mobile ground");
        s->material->params.baseColor = glm::vec4(.65f, .72f, .78f, 1.f);
        s->material->params.outlineAlpha = 0; s->material->tex[TEX_BASE] = style.checker;
        s->renderer.flushMaterial(s->material);
        s->ground = s->renderer.createMesh(MeshData::plane(60.f, 30.f));
        s->start = s->last = std::chrono::steady_clock::now();
        return s.release();
    } catch (const std::exception& e) { error = e.what(); return nullptr; }
}
int renderAndroidFrame(AndroidRenderSession* s, float moveX, float moveY, int clip, std::string& error) {
    if (!s) { error = "Renderer unavailable"; return -1; }
    try {
        const auto now = std::chrono::steady_clock::now();
        float dt = std::clamp(std::chrono::duration<float>(now - s->last).count(), 0.f, .05f); s->last = now;
        if (!std::isfinite(moveX) || !std::isfinite(moveY)) moveX = moveY = 0;
        if (clip >= 0 && clip < (int)s->hero.mmDb->clipNames.size() && clip != s->selectedClip) {
            s->hero.mm->playClip(clip, s->hero.pos, s->hero.yaw); s->selectedClip = clip;
        }
        s->hero.update(dt, glm::vec3(moveX, 0, moveY) * 1.6f);
        FrameInput fi;
        glm::vec3 target = s->hero.pos + glm::vec3(0.f, .9f, 0.f);
        fi.cameraPos = target + glm::vec3(0.f, .7f, -3.8f);
        fi.view = glm::lookAt(fi.cameraPos, target, glm::vec3(0.f, 1.f, 0.f));
        fi.proj = Renderer::perspective(glm::radians(45.f), s->renderer.aspect(), .05f, 100.f);
        fi.time = std::chrono::duration<float>(now - s->start).count();
        s->renderer.beginUI(dt);
        ImGui::SetNextWindowPos(ImVec2(s->renderer.width() * .65f, 10.f * s->density), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(s->renderer.width() * .34f, s->renderer.height() * .42f), ImGuiCond_Always);
        ImGui::Begin("Sakuga mobile", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("VRM scene");
        ImGui::Text("Clip: %d / %d", s->hero.mm->debug.clip + 1, (int)s->hero.mmDb->clipNames.size());
        if (ImGui::Checkbox("Bloom", &s->bloom)) __android_log_print(ANDROID_LOG_INFO, "SakugaPreview", "imgui bloom changed");
        ImGui::Checkbox("Point shadows", &s->pointShadows);
        ImGui::End();
        fi.light.pointShadows = s->pointShadows; fi.shadowFocus = s->hero.pos;
        fi.post.bloom = s->bloom;
        DrawItem ground; ground.mesh = s->ground; ground.material = s->material; fi.draws.push_back(ground);
        s->hero.collect(fi);
        return s->renderer.renderFrame(fi) ? 1 : 0;
    } catch (const std::exception& e) { error = e.what(); return -1; }
}
void touchAndroidRenderSession(AndroidRenderSession* s, int action, float x, float y) {
    if (!s || !s->renderer.uiEnabled()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    if (action == 3) { io.AddMouseButtonEvent(0, false); io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); return; }
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    io.AddMousePosEvent(x, y);
    if (action == 0) io.AddMouseButtonEvent(0, true);
    else if (action == 2) io.AddMouseButtonEvent(0, false);
}
void destroyAndroidRenderSession(AndroidRenderSession* session) { delete session; }
}
