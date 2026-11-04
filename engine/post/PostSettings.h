#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
namespace sk {
struct PostSettings {
    bool enabled = true;
    float exposure = 1.0f;
    int tonemap = 1;                               // 0 none (clamp), 1 Khronos PBR Neutral, 2 ACES (Narkowicz)
    bool bloom = true; float bloomIntensity = 0.30f, bloomThreshold = 0.92f, bloomKnee = 0.5f, bloomRadius = 1.0f;
    float vignette = 0.28f, vignetteSoftness = 0.65f;
    float saturation = 1.10f, contrast = 1.05f;
    glm::vec3 shadowTint{0.93f, 0.95f, 1.06f}, highlightTint{1.04f, 1.0f, 0.95f};   // cheap split-toning (multiplies)
    float grain = 0.012f;
    bool atmosphere = false;                      // opt-in screen-space depth fog when supported
    float halation = 0.f;                          // HDR red-biased bloom diffusion, 0 = off
    float lutStrength = 0.f;                       // colour grading LUT (Renderer::setPostLut), 0 = off
};

// Pass selection stays CPU-only and is shared by the renderer and tests.
struct PostPlan { bool bloom, halation; uint32_t halationWidth, halationHeight; };
inline PostPlan planPost(const PostSettings& p, uint32_t width, uint32_t height) {
    bool bloom = p.enabled && p.bloom && p.bloomIntensity > 0.f;
    return {bloom, bloom && p.halation > 0.f, std::max(1u, width / 4), std::max(1u, height / 4)};
}
// Applying a creative profile preserves the user's exposure, LUT selection/strength and enabled switch.
enum class PostProfile { Celestial, Cinematic, Atmospheric, Painterly };
inline PostSettings applyPostProfile(PostProfile profile, PostSettings p) {
    p.bloom = true; p.bloomThreshold = 1.f; p.bloomKnee = .5f; p.bloomRadius = 1.f;
    p.tonemap = 1; p.vignetteSoftness = .65f;
    p.atmosphere = (profile == PostProfile::Atmospheric);
    switch (profile) {
    case PostProfile::Celestial:
        p.bloomIntensity=.20f; p.halation=0.f; p.saturation=1.08f; p.contrast=1.02f;
        p.shadowTint={.96f,.98f,1.04f}; p.highlightTint={1.03f,1.f,.96f}; p.vignette=.12f; p.grain=.004f; break;
    case PostProfile::Cinematic:
        p.bloomIntensity=.38f; p.halation=.22f; p.saturation=1.05f; p.contrast=1.08f;
        p.shadowTint={.94f,.95f,1.07f}; p.highlightTint={1.06f,.98f,.93f}; p.vignette=.25f; p.grain=.012f; break;
    case PostProfile::Atmospheric:
        p.bloomIntensity=.16f; p.halation=0.f; p.saturation=.92f; p.contrast=1.12f;
        p.shadowTint={.91f,.96f,1.04f}; p.highlightTint={1.02f,1.f,.98f}; p.vignette=.16f; p.grain=.006f; break;
    case PostProfile::Painterly:
        p.bloomIntensity=.08f; p.halation=0.f; p.saturation=1.15f; p.contrast=1.02f;
        p.shadowTint={.98f,1.f,1.02f}; p.highlightTint={1.02f,1.f,.96f}; p.vignette=.05f; p.grain=0.f; break;
    }
    return p;
}
}
