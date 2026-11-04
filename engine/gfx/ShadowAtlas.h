#pragma once
// CPU mirror of the directional shadow-atlas PCF clamping in toon.frag.
#include <glm/glm.hpp>
#include <algorithm>
namespace sk {
inline glm::vec2 clampCascadeTap(glm::vec2 uv, int cascade, glm::ivec2 atlasSize) {
    if (atlasSize.x <= 0 || atlasSize.y <= 0) return glm::vec2(0.f);
    float c = float(std::clamp(cascade, 0, 1));
    glm::vec2 halfTexel = .5f / glm::vec2(atlasSize);
    return glm::clamp(uv, glm::vec2(.5f * c, 0.f) + halfTexel,
                          glm::vec2(.5f * (c + 1.f), 1.f) - halfTexel);
}
}
