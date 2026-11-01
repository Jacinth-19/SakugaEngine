#include "world/World.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace sk {

glm::mat4 Collider::toWorld() const { return glm::translate(glm::mat4(1.f), center) * glm::mat4_cast(rot); }

namespace {
// Ray vs oriented box (slab method in box space). Returns entry distance and the face normal.
bool rayBox(const Collider& c, glm::vec3 o, glm::vec3 d, float maxT, float& tOut, glm::vec3& nOut) {
    glm::quat inv = glm::inverse(c.rot);
    glm::vec3 lo = inv * (o - c.center), ld = inv * d;
    float tmin = 0.f, tmax = maxT; int axis = -1; float sgn = 1.f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(ld[i]) < 1e-9f) { if (lo[i] < -c.half[i] || lo[i] > c.half[i]) return false; continue; }
        float inv_d = 1.f / ld[i], t1 = (-c.half[i] - lo[i]) * inv_d, t2 = (c.half[i] - lo[i]) * inv_d, s = -1.f;
        if (t1 > t2) { std::swap(t1, t2); s = 1.f; }
        if (t1 > tmin) { tmin = t1; axis = i; sgn = s; }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (axis < 0) return false;            // origin inside the box
    glm::vec3 ln(0); ln[axis] = sgn;
    tOut = tmin; nOut = c.rot * ln;
    return true;
}
}  // namespace

glm::vec3 World::closestPoint(const Collider& c, glm::vec3 p) const {
    glm::quat inv = glm::inverse(c.rot);
    glm::vec3 l = inv * (p - c.center);
    l = glm::clamp(l, -c.half, c.half);
    return c.center + c.rot * l;
}

RayHit World::raycast(glm::vec3 o, glm::vec3 d, float maxDist) const {
    RayHit best; float len = glm::length(d); if (len < 1e-9f) return best;
    d /= len; best.t = maxDist;
    if (hasGround && d.y < -1e-6f) {
        float t = (groundY - o.y) / d.y;
        if (t >= 0.f && t <= maxDist) { best.hit = true; best.t = t; best.point = o + d * t; best.normal = {0, 1, 0}; best.collider = -1; }
    }
    for (size_t i = 0; i < colliders_.size(); ++i) {
        float t; glm::vec3 n;
        if (rayBox(colliders_[i], o, d, best.t, t, n) && t <= best.t) { best.hit = true; best.t = t; best.point = o + d * t; best.normal = n; best.collider = (int)i; }
    }
    return best;
}

bool World::groundHeight(glm::vec3 from, float maxDown, float& h, glm::vec3* normal) const {
    RayHit r = raycast(from, glm::vec3(0, -1, 0), maxDown);
    if (!r.hit) return false;
    if (r.collider >= 0 && !colliders_[r.collider].walkable) return false;
    h = r.point.y; if (normal) *normal = r.normal; return true;
}

glm::vec3 World::depenetrate(glm::vec3 c, float radius) const {
    glm::vec3 total(0);
    for (const Collider& b : colliders_) {
        glm::vec3 p = closestPoint(b, c + total), d = (c + total) - p; float dist = glm::length(d);
        if (dist >= radius) continue;
        if (dist > 1e-5f) total += d / dist * (radius - dist);
        else {                                              // centre inside the box: leave through the nearest face
            glm::quat inv = glm::inverse(b.rot); glm::vec3 l = inv * (c + total - b.center);
            int ax = 0; float best = 1e9f; float sg = 1.f;
            for (int i = 0; i < 3; ++i) { float e = b.half[i] - std::fabs(l[i]); if (e < best) { best = e; ax = i; sg = l[i] >= 0 ? 1.f : -1.f; } }
            glm::vec3 n(0); n[ax] = sg; total += b.rot * n * (best + radius);
        }
    }
    return total;
}

void appendBoxTriangles(const Collider& c, std::vector<float>& v, std::vector<int>& t) {
    int base = (int)v.size() / 3;
    for (int i = 0; i < 8; ++i) {
        glm::vec3 l((i & 1) ? c.half.x : -c.half.x, (i & 2) ? c.half.y : -c.half.y, (i & 4) ? c.half.z : -c.half.z);
        glm::vec3 w = c.center + c.rot * l; v.push_back(w.x); v.push_back(w.y); v.push_back(w.z);
    }
    // indices ordered so that Recast sees upward facing top faces (CCW seen from above)
    static const int quads[6][4] = {{2, 3, 7, 6}, {0, 4, 5, 1}, {1, 5, 7, 3}, {0, 2, 6, 4}, {4, 6, 7, 5}, {0, 1, 3, 2}};
    for (auto& q : quads) {
        // pick winding by geometric normal vs the outward direction of the face
        glm::vec3 a(v[(base + q[0]) * 3], v[(base + q[0]) * 3 + 1], v[(base + q[0]) * 3 + 2]), b(v[(base + q[1]) * 3], v[(base + q[1]) * 3 + 1], v[(base + q[1]) * 3 + 2]),
                  d(v[(base + q[2]) * 3], v[(base + q[2]) * 3 + 1], v[(base + q[2]) * 3 + 2]);
        glm::vec3 fc(0); for (int k = 0; k < 4; ++k) fc += glm::vec3(v[(base + q[k]) * 3], v[(base + q[k]) * 3 + 1], v[(base + q[k]) * 3 + 2]) * 0.25f;
        glm::vec3 n = glm::cross(b - a, d - a); bool flip = glm::dot(n, fc - c.center) < 0;
        int i0 = base + q[0], i1 = base + q[1], i2 = base + q[2], i3 = base + q[3];
        if (!flip) { t.insert(t.end(), {i0, i1, i2, i0, i2, i3}); } else { t.insert(t.end(), {i0, i2, i1, i0, i3, i2}); }
    }
}

}  // namespace sk
