#include "MobileSession.h"
#include "AndroidVulkanSurface.h"
#include "AndroidRenderSession.h"
#include <android/native_window_jni.h>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <android/log.h>

namespace {
sk::MobileSession* session(jlong h) { return reinterpret_cast<sk::MobileSession*>(static_cast<intptr_t>(h)); }
constexpr const char* tag = "SakugaPreview";
}
extern "C" JNIEXPORT jlong JNICALL Java_com_sakuga_preview_MainActivity_nativeCreate(JNIEnv* env, jclass, jobjectArray files) {
    try {
        std::vector<std::string> paths;
        const jsize n = env->GetArrayLength(files);
        for (jsize i = 0; i < n; ++i) {
            auto str = static_cast<jstring>(env->GetObjectArrayElement(files, i));
            if (!str) return 0;
            const char* raw = env->GetStringUTFChars(str, nullptr);
            if (!raw) { env->DeleteLocalRef(str); return 0; }
            paths.emplace_back(raw); env->ReleaseStringUTFChars(str, raw); env->DeleteLocalRef(str);
        }
        auto s = std::make_unique<sk::MobileSession>(); std::string error;
        if (!s->load(paths, error)) { __android_log_print(ANDROID_LOG_ERROR, tag, "Motion database: %s", error.c_str()); return 0; }
        __android_log_print(ANDROID_LOG_INFO, tag, "ready clips=%d", s->clipCount());
        return static_cast<jlong>(reinterpret_cast<intptr_t>(s.release()));
    } catch (const std::exception& e) { __android_log_print(ANDROID_LOG_ERROR, tag, "initialization: %s", e.what()); return 0; }
}
extern "C" JNIEXPORT void JNICALL Java_com_sakuga_preview_MainActivity_nativeDestroy(JNIEnv*, jclass, jlong h) { delete session(h); }
extern "C" JNIEXPORT void JNICALL Java_com_sakuga_preview_MainActivity_nativeUpdate(JNIEnv*, jclass, jlong h, jfloat dt, jfloat x, jfloat y) { if (h) session(h)->update(dt, x, y); }
extern "C" JNIEXPORT jfloatArray JNICALL Java_com_sakuga_preview_MainActivity_nativeTouch(
        JNIEnv* env, jclass, jlong h, jint action, jint id, jfloat x, jfloat y, jfloat width, jfloat height, jfloat density) {
    if (!h || action < 0 || action > 3) return nullptr;
    auto* s = session(h);
    auto result = s->touch.event(static_cast<sk::MobileTouch::Action>(action), id, x, y, width, height, density);
    if (result.nextClip) { s->nextClip(); __android_log_print(ANDROID_LOG_INFO, tag, "next clip requested"); }
    const jfloat values[] = {result.stickX, result.stickY};
    jfloatArray array = env->NewFloatArray(2);
    if (array) env->SetFloatArrayRegion(array, 0, 2, values);
    return array;
}
extern "C" JNIEXPORT jfloatArray JNICALL Java_com_sakuga_preview_MainActivity_nativeJoints(JNIEnv* env, jclass, jlong h) {
    if (!h) return nullptr;
    auto joints = session(h)->joints();
    jfloatArray a = env->NewFloatArray(static_cast<jsize>(joints.size()));
    if (a) env->SetFloatArrayRegion(a, 0, static_cast<jsize>(joints.size()), joints.data());
    return a;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sakuga_preview_MainActivity_nativeInfo(JNIEnv* env, jclass, jlong h) {
    if (!h) return env->NewStringUTF("Loading");
    auto* s = session(h);
    std::string text = std::to_string(s->clipIndex() + 1) + " / " + std::to_string(s->clipCount()) + "   " + s->clipName()
        + "   |   frame " + std::to_string(s->frame()) + (s->browsing() ? "   |   browsing" : "   |   matching");
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL Java_com_sakuga_preview_MainActivity_nativeProbeSurface(JNIEnv* env, jclass, jobject surface) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); __android_log_print(ANDROID_LOG_INFO, tag, "surface probe finished: Android surface unavailable"); return env->NewStringUTF("Android surface unavailable"); }
    std::string status;
    try { status = sk::probeAndroidVulkanSurface(window); }
    catch (const std::exception& e) { status = std::string("Vulkan surface probe failed: ") + e.what(); }
    if (window) ANativeWindow_release(window);
    __android_log_print(ANDROID_LOG_INFO, tag, "surface probe finished: %s", status.c_str());
    return env->NewStringUTF(status.c_str());
}

namespace {
struct RendererHandle {
    ANativeWindow* window = nullptr;
    sk::AndroidRenderSession* session = nullptr;
    ~RendererHandle() { sk::destroyAndroidRenderSession(session); if (window) ANativeWindow_release(window); }
};
thread_local std::string rendererError;
RendererHandle* renderer(jlong h) { return reinterpret_cast<RendererHandle*>(static_cast<intptr_t>(h)); }
}
extern "C" JNIEXPORT jlong JNICALL Java_com_sakuga_preview_MainActivity_nativeRendererCreate(JNIEnv* env, jclass, jobject surface, jstring shaderDir) {
    rendererError.clear();
    if (!surface || !shaderDir) { rendererError = "Surface or shaders unavailable"; return 0; }
    try {
        auto h = std::make_unique<RendererHandle>();
        h->window = ANativeWindow_fromSurface(env, surface);
        if (env->ExceptionCheck()) { env->ExceptionClear(); rendererError = "Android surface unavailable"; return 0; }
        const char* path = env->GetStringUTFChars(shaderDir, nullptr);
        if (!path) { rendererError = "Shader path unavailable"; return 0; }
        std::string directory(path); env->ReleaseStringUTFChars(shaderDir, path);
        h->session = sk::createAndroidRenderSession(h->window, directory, rendererError);
        if (!h->session) { __android_log_print(ANDROID_LOG_WARN, tag, "renderer unavailable: %s", rendererError.c_str()); return 0; }
        __android_log_print(ANDROID_LOG_INFO, tag, "engine renderer initialized");
        return static_cast<jlong>(reinterpret_cast<intptr_t>(h.release()));
    } catch (const std::exception& e) { rendererError = e.what(); return 0; }
}
extern "C" JNIEXPORT jint JNICALL Java_com_sakuga_preview_MainActivity_nativeRendererFrame(JNIEnv* env, jclass, jlong handle, jfloatArray joints) {
    auto* h = renderer(handle); if (!h) return -1;
    float data[66]; int count = 0;
    if (joints && env->GetArrayLength(joints) >= 66) { env->GetFloatArrayRegion(joints, 0, 66, data); count = 66; }
    if (env->ExceptionCheck()) return -1;
    return sk::renderAndroidFrame(h->session, data, count, rendererError);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sakuga_preview_MainActivity_nativeRendererError(JNIEnv* env, jclass) {
    return env->NewStringUTF(rendererError.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_com_sakuga_preview_MainActivity_nativeRendererDestroy(JNIEnv*, jclass, jlong h) { delete renderer(h); }
