#pragma once
#include <algorithm>
#include <glm/glm.hpp>
namespace sk {
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
}
