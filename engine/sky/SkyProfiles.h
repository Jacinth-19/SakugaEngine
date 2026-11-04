#pragma once
// Art direction for the existing gradient/procedural cloud sky, independent of Vulkan and quality settings.
// These are palette profiles, not physical atmosphere or volumetric-cloud implementations.
#include <algorithm>
#include <glm/glm.hpp>
namespace sk {
enum class SkyStyle { Celestial, Cinematic, Atmospheric, Painterly };
struct SkyPalette { glm::vec3 zenith, horizon, ambient, fog; };
inline SkyPalette applySkyStyle(SkyStyle style, SkyPalette p, float daylight) {
    float d = std::clamp(daylight, 0.f, 1.f);
    switch (style) {
    case SkyStyle::Cinematic:
        p.zenith = glm::mix(p.zenith, p.zenith * glm::vec3(.90f,.78f,1.15f) + glm::vec3(.04f,.01f,.06f)*d, .75f);
        p.horizon = glm::mix(p.horizon, p.horizon * glm::vec3(1.04f,.78f,.90f) + glm::vec3(.08f,.03f,.06f)*d, .7f);
        p.fog = glm::mix(p.fog, p.horizon, .15f); break;
    case SkyStyle::Atmospheric:
        p.zenith = glm::mix(p.zenith, p.zenith * glm::vec3(.58f,.76f,.88f), .65f);
        p.horizon = glm::mix(p.horizon, p.horizon * glm::vec3(.72f,.85f,.92f), .65f);
        p.ambient *= glm::vec3(.82f,.91f,1.f); p.fog = glm::mix(p.fog, p.horizon, .35f); break;
    case SkyStyle::Painterly:
        p.zenith = glm::mix(p.zenith, p.zenith * glm::vec3(.55f,1.03f,1.08f) + glm::vec3(.01f,.06f,.05f)*d, .8f);
        p.horizon = glm::mix(p.horizon, p.horizon * glm::vec3(.78f,1.03f,1.02f) + glm::vec3(0,.03f,.03f)*d, .7f); break;
    case SkyStyle::Celestial: break;
    }
    p.zenith = glm::max(p.zenith, glm::vec3(0)); p.horizon = glm::max(p.horizon, glm::vec3(0));
    p.ambient = glm::max(p.ambient, glm::vec3(0)); p.fog = glm::max(p.fog, glm::vec3(0));
    return p;
}
}
