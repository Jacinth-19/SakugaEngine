#include "AndroidRenderSession.h"
#include "gfx/Renderer.h"
#include <android/native_window.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
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
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~AndroidRenderSession() {
        try { if (rendererStarted) renderer.shutdown(); } catch (...) {}
        ctx.shutdown();
    }
};
AndroidRenderSession* createAndroidRenderSession(ANativeWindow* window, const std::string& shaderDir, std::string& error) {
    auto s = std::make_unique<AndroidRenderSession>();
    try {
        if (!s->ctx.initAndroid(window, error)) return nullptr;
        int width = ANativeWindow_getWidth(window), height = ANativeWindow_getHeight(window);
        if (width <= 0 || height <= 0) { error = "Android surface has no size"; return nullptr; }
        s->rendererStarted = true;
        if (!s->renderer.initAndroid(s->ctx, window, width, height, shaderDir)) { error = "Renderer initialization failed"; return nullptr; }
        s->material = s->renderer.createMaterial("mobile ground");
        s->material->params.baseColor = glm::vec4(.65f, .72f, .78f, 1.f);
        s->material->params.outlineAlpha = 0;
        s->renderer.flushMaterial(s->material);
        s->ground = s->renderer.createMesh(MeshData::plane(12.f, 4.f));
        return s.release();
    } catch (const std::exception& e) { error = e.what(); return nullptr; }
}
int renderAndroidFrame(AndroidRenderSession* s, const float* joints, int count, std::string& error) {
    if (!s) { error = "Renderer unavailable"; return -1; }
    try {
        FrameInput fi;
        fi.cameraPos = glm::vec3(0.f, 1.65f, -3.8f);
        fi.view = glm::lookAt(fi.cameraPos, glm::vec3(0.f, .9f, 0.f), glm::vec3(0.f, 1.f, 0.f));
        fi.proj = Renderer::perspective(glm::radians(45.f), s->renderer.aspect(), .05f, 40.f);
        fi.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - s->start).count();
        fi.light.pointShadows = false;
        fi.post.bloom = false;
        DrawItem ground; ground.mesh = s->ground; ground.material = s->material; fi.draws.push_back(ground);
        static const int bones[][2] = {{0,1},{1,2},{2,3},{3,4},{4,5},{3,6},{6,7},{7,8},{8,9},{3,10},{10,11},{11,12},{12,13},{0,14},{14,15},{15,16},{16,17},{0,18},{18,19},{19,20},{20,21}};
        if (joints && count >= 66) for (const auto& b : bones) {
            glm::vec3 a(joints[b[0]*3], joints[b[0]*3+1], joints[b[0]*3+2]);
            glm::vec3 c(joints[b[1]*3], joints[b[1]*3+1], joints[b[1]*3+2]);
            appendLine(fi.lines, a, c, packColor(.25f, .9f, 1.f));
        }
        return s->renderer.renderFrame(fi) ? 1 : 0;
    } catch (const std::exception& e) { error = e.what(); return -1; }
}
void destroyAndroidRenderSession(AndroidRenderSession* session) { delete session; }
}
