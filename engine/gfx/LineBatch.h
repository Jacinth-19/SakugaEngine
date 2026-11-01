#pragma once
// Debug-line buffer plumbing (skeleton, trajectory, contact points). Vulkan-free so it can be unit tested: the renderer
// happily binds a LINE_LIST pipeline, so every helper here emits vertex pairs (2 vertices = 1 segment).
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace sk {

struct LineVertex { glm::vec3 pos; uint32_t rgba; };

// R8G8B8A8_UNORM packed in memory order (the shader reads vec4 inColor, little-endian: r | g<<8 | b<<16 | a<<24).
inline uint32_t packColor(float r, float g, float b, float a = 1.f) {
    auto c = [](float v) { return (uint32_t)(glm::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

// Appends one segment. Degenerate (zero-length) segments are skipped so the line list never contains invisible junk.
inline void appendLine(std::vector<LineVertex>& out, const glm::vec3& a, const glm::vec3& b, uint32_t color) {
    if (glm::dot(b - a, b - a) < 1e-12f) return;
    out.push_back({a, color});
    out.push_back({b, color});
}

// Same, but honours the renderer's vertex budget (Renderer::kMaxLineVerts): a segment that does not fit whole is dropped
// rather than half-written (the buffer is memcpy'd as `size & ~1`, a dangling odd vertex would be lost anyway).
inline bool appendLineCapped(std::vector<LineVertex>& out, const glm::vec3& a, const glm::vec3& b, uint32_t color, size_t maxVerts) {
    if (out.size() + 2 > maxVerts) return false;
    size_t before = out.size();
    appendLine(out, a, b, color);
    return out.size() != before;
}

// Bone segments of a skeleton: one line per parent/child pair that is flagged as a joint.
inline void appendSkeletonSegments(std::vector<LineVertex>& out, const std::vector<int>& parent,
                                  const std::vector<glm::mat4>& globals, const std::vector<char>& isJoint,
                                  const glm::mat4& transform, uint32_t color) {
    size_t n = std::min(parent.size(), std::min(globals.size(), isJoint.size()));
    for (size_t i = 0; i < n; ++i) {
        int p = parent[i];
        if (p < 0 || (size_t)p >= n || !isJoint[i] || !isJoint[(size_t)p]) continue;
        appendLine(out, glm::vec3(transform * globals[(size_t)p][3]), glm::vec3(transform * globals[i][3]), color);
    }
}

// Polyline helper (trajectory prediction): consecutive samples become segments.
inline void appendPolyline(std::vector<LineVertex>& out, const std::vector<glm::vec3>& pts, uint32_t color) {
    for (size_t i = 1; i < pts.size(); ++i) appendLine(out, pts[i - 1], pts[i], color);
}

}  // namespace sk
