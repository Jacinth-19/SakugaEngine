#include "MobileSession.h"
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
extern "C" JNIEXPORT void JNICALL Java_com_sakuga_preview_MainActivity_nativeNext(JNIEnv*, jclass, jlong h) {
    if (h) { session(h)->nextClip(); __android_log_print(ANDROID_LOG_INFO, tag, "next clip requested"); }
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
