#pragma once
// Shared, GPU-free geometry for the visible buttons and their hit areas.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace sk {
struct TouchButtonGeometry { glm::vec2 center; float radius; };
inline void touchButtonLayout(float width, float height, float scale, TouchButtonGeometry (&out)[5]) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scale) || width <= 0 || height <= 0) {
        for (auto& b : out) b = {{0, 0}, 0};
        return;
    }
    float side = std::min(width, height);
    float radius = side * std::min(0.055f * std::clamp(scale, 0.6f, 2.f), 0.095f);
    float hit = radius * 1.15f;
    float margin = side * 0.025f;
    float right = width - margin - hit, bottom = height - margin - hit;
    float gap = radius * 2.4f; // hit circles cannot overlap
    out[0] = {{right, bottom}, radius};               // jump
    out[1] = {{right - gap, bottom}, radius};         // attack
    out[2] = {{right - gap, bottom - gap}, radius};   // use
    out[3] = {{right, bottom - gap}, radius};         // crouch
    out[4] = {{margin + hit, std::max(margin + hit, bottom - radius * 5.3f)}, radius * 0.85f};
}
}
