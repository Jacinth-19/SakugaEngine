#pragma once
#include <algorithm>
#include <glm/glm.hpp>
namespace sk {
// The R8 mask requires an extra color attachment, blendable/sampleable linear filtering, and matching MSAA support.
inline bool canUseMaterialBloomMask(bool formatQueryOk, bool featuresOk, unsigned maxAttachments,
                                    unsigned formatSampleCounts, unsigned samples, bool resolveOk) {
    return formatQueryOk && featuresOk && maxAttachments >= 2 && samples != 0 &&
           (formatSampleCounts & samples) && resolveOk;
}
inline float materialBloomWeight(float weight, float emissionOnly, float emissionSignal) {
    const float e = std::clamp(emissionOnly, 0.f, 1.f);
    return std::clamp(weight, 0.f, 1.f) * ((1.f - e) + e * std::clamp(emissionSignal, 0.f, 1.f));
}
inline float blendBloomWeight(float behind, float materialWeight, float opacity) {
    float a = std::clamp(opacity, 0.f, 1.f);
    return std::clamp(materialWeight, 0.f, 1.f) * a + std::clamp(behind, 0.f, 1.f) * (1.f-a);
}
// CPU mirror of the prefilter's HDR colour selection. It is a colour key, not an object/material mask.
inline float bloomSelection(glm::vec3 c, int mode) {
    c = glm::max(c, glm::vec3(0.f));
    const float peak = std::max(c.r, std::max(c.g, c.b));
    const float low = std::min(c.r, std::min(c.g, c.b));
    auto smooth = [](float a, float b, float x) { float t = std::clamp((x-a)/(b-a), 0.f, 1.f); return t*t*(3.f-2.f*t); };
    float warm = smooth(-.1f, .3f, (c.r-c.b)/std::max(peak, .001f));
    if (mode == 1) return warm;
    if (mode == 2) return 1.f - warm;
    if (mode == 3) return smooth(.1f, .45f, (peak-low)/std::max(peak, .001f));
    return 1.f;
}
// Mirrors one prefilter tap; mask and threshold are evaluated before neighbour averaging.
inline glm::vec3 bloomPrefilterTap(glm::vec3 c, float threshold, float knee, int mode, float mask) {
    float br = std::max(c.r, std::max(c.g, c.b));
    knee = std::max(knee, 1e-4f);
    float soft = std::clamp(br - threshold + knee, 0.f, 2.f*knee);
    soft = soft * soft / (4.f*knee);
    float contribution = std::max(soft, br-threshold) / std::max(br, 1e-4f);
    return glm::max(c, glm::vec3(0.f)) * contribution * bloomSelection(c, mode) * std::clamp(mask, 0.f, 1.f);
}

}
