#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
namespace sk {
struct PostSettings {
    bool enabled = true;
    bool gpuDetailedProfiling = false; // opt-in: per-stage timestamps can perturb GPU scheduling
    float exposure = 1.0f;
    int tonemap = 1;                               // 0 none (clamp), 1 Khronos PBR Neutral, 2 ACES (Narkowicz)
    bool bloom = true; float bloomIntensity = 0.30f, bloomThreshold = 0.92f, bloomKnee = 0.5f, bloomRadius = 1.0f;
    bool materialBloomMask = false;               // opt in to per-material selection where supported
    float skyBloomWeight = 1.f;                   // sky/cloud contribution to the selection mask
    int bloomSelection = 0;                       // 0 all, 1 warm, 2 cool, 3 saturated highlights
    float vignette = 0.28f, vignetteSoftness = 0.65f;
    float saturation = 1.10f, contrast = 1.05f;
    glm::vec3 shadowTint{0.93f, 0.95f, 1.06f}, highlightTint{1.04f, 1.0f, 0.95f};   // cheap split-toning (multiplies)
    float grain = 0.012f;
    bool atmosphere = false;                      // opt-in screen-space depth fog when supported
    bool physicalAerial = false;                  // bounded single-scattering mode instead of artistic fog
    float halation = 0.f;                          // HDR red-biased bloom diffusion, 0 = off
    float sunFlare = 0.f;                          // optional depth-masked procedural-sun ghosts
    float lensFlare = 0.f;                         // stylised HDR ghost reflections from bloom
    float chromatic = 0.f;                        // pixel displacement at screen edges
    float depthInk = 0.f, normalInk = 0.f;           // sampled depth silhouettes and depth-reconstructed normal edges
    float ink = 0.f, painterly = 0.f;              // colour-edge darkening and directional screen-space stroke approximation
    int colourLevels = 0;                         // 0 = off; otherwise 2..32 levels/channel
    float depthOfField = 0.f, focusDistance = 8.f, focusRange = 4.f; // opt-in, sampled scene depth required
    float lutStrength = 0.f;                       // colour grading LUT (Renderer::setPostLut), 0 = off
};

// Pass selection stays CPU-only and is shared by the renderer and tests.
struct PostPlan { bool bloom, halation, finish; uint32_t halationWidth, halationHeight; };
inline PostPlan planPost(const PostSettings& p, uint32_t width, uint32_t height) {
    bool bloom = p.enabled && p.bloom && p.bloomIntensity > 0.f;
    return {bloom, bloom && p.halation > 0.f,
            p.enabled && (p.chromatic > 0.f || p.ink > 0.f || p.depthInk > 0.f || p.normalInk > 0.f || p.painterly > 0.f || p.colourLevels >= 2), std::max(1u, width / 4), std::max(1u, height / 4)};
}
inline bool useGeometryInk(const PostSettings& p, bool depthSupported, bool projectionSupported) {
    return p.enabled && (p.depthInk > 0.f || p.normalInk > 0.f) && depthSupported && projectionSupported;
}
// Applying a creative profile preserves the user's exposure, LUT selection/strength and enabled switch.
enum class PostProfile { Celestial, Cinematic, Atmospheric, Painterly };
inline PostSettings applyPostProfile(PostProfile profile, PostSettings p) {
    p.bloom = true; p.bloomSelection = 0; p.bloomThreshold = 1.f; p.bloomKnee = .5f; p.bloomRadius = 1.f;
    p.tonemap = 1; p.vignetteSoftness = .65f;
    p.atmosphere = (profile == PostProfile::Atmospheric);
    p.sunFlare = p.lensFlare = p.chromatic = p.ink = p.depthInk = p.normalInk = p.painterly = p.depthOfField = 0.f; p.colourLevels = 0;
    switch (profile) {
    case PostProfile::Celestial:
        p.bloomIntensity=.20f; p.halation=0.f; p.saturation=1.08f; p.contrast=1.02f;
        p.shadowTint={.96f,.98f,1.04f}; p.highlightTint={1.03f,1.f,.96f}; p.vignette=.12f; p.grain=.004f; break;
    case PostProfile::Cinematic:
        p.bloomIntensity=.38f; p.halation=.22f; p.lensFlare=.10f; p.chromatic=.35f; p.saturation=1.05f; p.contrast=1.08f;
        p.shadowTint={.94f,.95f,1.07f}; p.highlightTint={1.06f,.98f,.93f}; p.vignette=.25f; p.grain=.012f; break;
    case PostProfile::Atmospheric:
        p.bloomIntensity=.16f; p.halation=0.f; p.saturation=.92f; p.contrast=1.12f;
        p.shadowTint={.91f,.96f,1.04f}; p.highlightTint={1.02f,1.f,.98f}; p.vignette=.16f; p.grain=.006f; break;
    case PostProfile::Painterly:
        p.bloomIntensity=.08f; p.halation=0.f; p.painterly=.16f; p.ink=.12f; p.saturation=1.15f; p.contrast=1.02f;
        p.shadowTint={.98f,1.f,1.02f}; p.highlightTint={1.02f,1.f,.96f}; p.vignette=.05f; p.grain=0.f; break;
    }
    return p;
}
}
