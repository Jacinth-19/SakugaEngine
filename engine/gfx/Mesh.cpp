#include "gfx/Mesh.h"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace sk {

void MeshData::computeNormals() {
    for (auto& v : vertices) v.normal = glm::vec3(0);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto &a = vertices[indices[i]], &b = vertices[indices[i + 1]], &c = vertices[indices[i + 2]];
        glm::vec3 n = glm::cross(b.pos - a.pos, c.pos - a.pos);
        a.normal += n; b.normal += n; c.normal += n;
    }
    for (auto& v : vertices) v.normal = glm::length(v.normal) > 1e-8f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
}

void MeshData::computeTangents() {
    std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0)), bit(vertices.size(), glm::vec3(0));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex &a = vertices[i0], &b = vertices[i1], &c = vertices[i2];
        glm::vec3 e1 = b.pos - a.pos, e2 = c.pos - a.pos;
        glm::vec2 u1 = b.uv - a.uv, u2 = c.uv - a.uv;
        float det = u1.x * u2.y - u2.x * u1.y;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.f / det;
        glm::vec3 t = (e1 * u2.y - e2 * u1.y) * r, bt = (e2 * u1.x - e1 * u2.x) * r;
        for (uint32_t k : {i0, i1, i2}) { tan[k] += t; bit[k] += bt; }
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        glm::vec3 n = vertices[i].normal, t = tan[i];
        t -= n * glm::dot(n, t);
        if (glm::length(t) < 1e-8f) {                     // degenerate UVs: pick any perpendicular
            t = std::fabs(n.y) < 0.99f ? glm::cross(n, glm::vec3(0, 1, 0)) : glm::cross(n, glm::vec3(1, 0, 0));
        }
        t = glm::normalize(t);
        float w = glm::dot(glm::cross(n, t), bit[i]) < 0.f ? -1.f : 1.f;
        vertices[i].tangent = glm::vec4(t, w);
    }
}

MeshData MeshData::sphere(float radius, int seg, int rings, float uShift) {
    MeshData m;
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / rings, phi = v * glm::pi<float>();
        for (int x = 0; x <= seg; ++x) {
            float u = (float)x / seg, th = u * glm::two_pi<float>();
            // +Z is the "face" direction at u = 0.25 (so u spans 0..1 around, front-centre at u=.25)
            glm::vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            Vertex vt; vt.pos = n * radius; vt.normal = n; vt.uv = {u + uShift, v};
            // tangent runs along +u (increasing theta)
            vt.tangent = glm::vec4(-std::sin(th), 0, std::cos(th), 1);
            m.vertices.push_back(vt);
        }
    }
    for (int y = 0; y < rings; ++y)
        for (int x = 0; x < seg; ++x) {
            uint32_t a = y * (seg + 1) + x, b = a + seg + 1;
            // CCW seen from outside
            m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    // verify winding with the normal; flip if needed
    if (!m.indices.empty()) {
        auto &a = m.vertices[m.indices[3 * 10]], &b = m.vertices[m.indices[3 * 10 + 1]], &c = m.vertices[m.indices[3 * 10 + 2]];
        if (glm::dot(glm::cross(b.pos - a.pos, c.pos - a.pos), a.normal) < 0)
            for (size_t i = 0; i < m.indices.size(); i += 3) std::swap(m.indices[i + 1], m.indices[i + 2]);
    }
    return m;
}

MeshData MeshData::plane(float s, float rep) {
    MeshData m;
    glm::vec3 n(0, 1, 0);
    float h = s * 0.5f;
    Vertex v[4];
    glm::vec3 p[4] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
    glm::vec2 uv[4] = {{0, 0}, {rep, 0}, {rep, rep}, {0, rep}};
    for (int i = 0; i < 4; ++i) { v[i].pos = p[i]; v[i].normal = n; v[i].uv = uv[i]; v[i].tangent = {1, 0, 0, 1}; m.vertices.push_back(v[i]); }
    m.indices = {0, 3, 2, 0, 2, 1};   // CCW seen from +Y
    return m;
}

MeshData MeshData::box(glm::vec3 h) {
    MeshData m;
    struct Face { glm::vec3 n, t, b; };
    Face faces[6] = {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
                     {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
    for (auto& f : faces) {
        uint32_t base = (uint32_t)m.vertices.size();
        for (int i = 0; i < 4; ++i) {
            float sx = (i == 1 || i == 2) ? 1.f : -1.f, sy = (i >= 2) ? 1.f : -1.f;
            Vertex v; v.pos = (f.n + f.t * sx + f.b * sy) * h; v.normal = f.n; v.tangent = glm::vec4(f.t, 1);
            v.uv = {(sx + 1) * 0.5f, 1.f - (sy + 1) * 0.5f};
            m.vertices.push_back(v);
        }
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    for (size_t i = 0; i < m.indices.size(); i += 3) {   // fix winding against normals
        auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
        if (glm::dot(glm::cross(b.pos - a.pos, c.pos - a.pos), a.normal) < 0) std::swap(m.indices[i + 1], m.indices[i + 2]);
    }
    return m;
}

}  // namespace sk
