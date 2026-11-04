#pragma once
#include "anim/MotionMatching.h"
#include "platform/MobileTouch.h"
#include <array>
#include <string>
#include <vector>

// GPU-free synthetic-rig animation fallback for the Android Vulkan sandbox.
namespace sk {
class MobileSession {
public:
    bool load(const std::vector<std::string>& paths, std::string& error, const std::string& cacheDir = "");
    void update(float dt, float moveX, float moveY);
    void nextClip();
    bool selectClip(int index);
    std::array<float, kHumanBoneCount * 3> joints() const;
    int clipIndex() const { return matcher_.debug.clip; }
    int clipCount() const { return static_cast<int>(db_.clipNames.size()); }
    std::string clipName() const;
    int frame() const { return matcher_.debug.frame; }
    bool browsing() const { return matcher_.browsing(); }
    MobileTouch touch;
private:
    Rig rig_;
    MotionDatabase db_;
    MotionMatcher matcher_;
    Pose pose_;
    glm::vec3 position_{0.f};
    float yaw_ = 0.f;
};
}
