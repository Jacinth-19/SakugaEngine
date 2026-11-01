#include "asset/ProceduralTextures.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace sk {
namespace {
using glm::vec2; using glm::vec3;
uint8_t b(float v) { return (uint8_t)(glm::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }
float hash(int x, int y) { unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65535.f; }
float vnoise(float x, float y, int period) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y); float fx = x - xi, fy = y - yi;
    auto h = [&](int a, int c) { return hash(((a % period) + period) % period, ((c % period) + period) % period); };
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    return glm::mix(glm::mix(h(xi, yi), h(xi + 1, yi), sx), glm::mix(h(xi, yi + 1), h(xi + 1, yi + 1), sx), sy);
}
template <class F> std::vector<uint8_t> gen(int w, int h, F f) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        glm::vec4 c = f((x + 0.5f) / w, (y + 0.5f) / h);
        uint8_t* p = &px[((size_t)y * w + x) * 4]; p[0] = b(c.r); p[1] = b(c.g); p[2] = b(c.b); p[3] = b(c.a);
    }
    return px;
}
}

StyleTextures createStyleTextures(Renderer& r) {
    StyleTextures t;
    // Diffuse ramp (_RD): u = lighting term (already thresholded in the shader). Cool violet shadow -> neutral white.
    { auto px = gen(256, 4, [](float u, float) { float k = glm::smoothstep(0.f, 1.f, u);
        return glm::vec4(glm::mix(vec3(0.62f, 0.56f, 0.80f), vec3(1.f, 0.985f, 0.96f), k), 1.f); });
      t.ramp = r.createTexture("proc_ramp", px.data(), 256, 4, false, false, false); }
    // Spec ramp (_RS): u = N.H, v = per-pixel spec shape from _P.a
    { auto px = gen(128, 128, [](float u, float v) { float s = glm::smoothstep(0.55f, 0.9f, u) * (0.45f + 0.55f * v);
        return glm::vec4(s * vec3(1.f, 0.97f, 0.90f), 1.f); });
      t.specRamp = r.createTexture("proc_specramp", px.data(), 128, 128, false, false, false); }
    // Colour LUT: luma -> tint (cool shadows, warm highlights)
    { auto px = gen(256, 4, [](float u, float) { return glm::vec4(glm::mix(vec3(0.84f, 0.90f, 1.10f), vec3(1.08f, 1.02f, 0.95f), glm::smoothstep(0.1f, 0.9f, u)), 1.f); });
      t.lut = r.createTexture("proc_lut", px.data(), 256, 4, false, false, false); }
    // MatCap: soft top-left key + rim
    { auto px = gen(256, 256, [](float u, float v) {
        vec2 p(u * 2 - 1, v * 2 - 1); float d2 = glm::dot(p, p);
        if (d2 > 1.f) return glm::vec4(0, 0, 0, 1);
        vec3 n(p.x, -p.y, std::sqrt(1 - d2));
        vec3 l = glm::normalize(vec3(-0.5f, 0.6f, 0.65f)), h = glm::normalize(l + vec3(0, 0, 1));
        float key = std::pow(std::max(glm::dot(n, h), 0.f), 24.f), rim = std::pow(1.f - n.z, 3.f) * 0.4f;
        float soft = std::max(glm::dot(n, l), 0.f) * 0.25f;
        return glm::vec4(vec3(key * 0.9f + rim + soft), 1.f); });
      t.matcap = r.createTexture("proc_matcap", px.data(), 256, 256, true, true, false); }
    // Hair spec style: horizontal band (strand-shift) with noise
    { auto px = gen(256, 256, [](float u, float v) { float band = std::exp(-std::pow((v - 0.45f) / 0.09f, 2.f));
        float n = 0.65f + 0.35f * vnoise(u * 48.f, v * 4.f, 64); return glm::vec4(band * n, band * 0.4f, 0, 1); });
      t.hairSpec = r.createTexture("proc_hairspec", px.data(), 256, 256, false, true, true); }
    // Flow mask: smooth tileable noise (R) + vector (RG)
    { auto px = gen(256, 256, [](float u, float v) { float n = 0.6f * vnoise(u * 8, v * 8, 8) + 0.4f * vnoise(u * 16, v * 16, 16);
        return glm::vec4(n, vnoise(u * 6 + 3, v * 6, 6), vnoise(u * 6, v * 6 + 7, 6), 1.f); });
      t.flow = r.createTexture("proc_flow", px.data(), 256, 256, false, true, true); }
    // Face SDF for the test head sphere (front = u 0.5): R lit-from-left threshold map, G lit-from-right (see docs/SHADER_PORT.md)
    { auto px = gen(512, 256, [](float u, float) { float a = (u - 0.5f) * 4.f;
        float g = std::pow(glm::clamp(1.f - a, 0.f, 1.f), 3.f), rr = std::pow(glm::clamp(1.f + a, 0.f, 1.f), 3.f);
        return glm::vec4(rr, g, 0, 1); });
      t.faceSDF = r.createTexture("proc_facesdf", px.data(), 512, 256, false, true, true); }
    // Pastel checker for the ground
    { auto px = gen(512, 512, [](float u, float v) { int cx = (int)(u * 8), cy = (int)(v * 8); bool a = ((cx + cy) & 1) == 0;
        float edge = std::min(std::min(std::fmod(u * 8, 1.f), 1.f - std::fmod(u * 8, 1.f)), std::min(std::fmod(v * 8, 1.f), 1.f - std::fmod(v * 8, 1.f)));
        vec3 c = a ? vec3(0.80f, 0.84f, 0.88f) : vec3(0.64f, 0.70f, 0.78f); c *= glm::mix(0.9f, 1.f, glm::smoothstep(0.f, 0.02f, edge));
        return glm::vec4(c, 1.f); });
      t.checker = r.createTexture("proc_checker", px.data(), 512, 512, true, true, true); }
    return t;
}
}
