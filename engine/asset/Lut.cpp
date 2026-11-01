#include "asset/Lut.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cmath>
#include <stb/stb_image.h>

namespace sk {
bool loadCubeLut(const std::string& path, LutStrip& out) {
    std::ifstream f(path);
    if (!f) return false;
    int n = 0; float dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};
    std::vector<float> data;
    std::string line;
    while (std::getline(f, line)) {
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos || line[s] == '#') continue;
        std::istringstream is(line.substr(s));
        std::string key; is >> key;
        if (key == "LUT_3D_SIZE") { is >> n; data.reserve((size_t)n * n * n * 3); }
        else if (key == "DOMAIN_MIN") is >> dmin[0] >> dmin[1] >> dmin[2];
        else if (key == "DOMAIN_MAX") is >> dmax[0] >> dmax[1] >> dmax[2];
        else if (key == "TITLE" || key == "LUT_1D_SIZE") { if (key == "LUT_1D_SIZE") return false; }
        else if ((key[0] >= '0' && key[0] <= '9') || key[0] == '-' || key[0] == '.') {
            float r = (float)std::atof(key.c_str()), g = 0, b = 0; is >> g >> b;
            data.push_back(r); data.push_back(g); data.push_back(b);
        }
    }
    if (n < 2 || n > 128 || data.size() != (size_t)n * n * n * 3) return false;
    out.size = n; out.rgba.assign((size_t)n * n * n * 4, 255);
    for (int b = 0; b < n; ++b) for (int g = 0; g < n; ++g) for (int r = 0; r < n; ++r) {
        size_t src = ((size_t)b * n * n + (size_t)g * n + r) * 3;                 // red fastest
        size_t dst = ((size_t)g * (n * n) + (size_t)b * n + r) * 4;               // strip: x = b*n + r, y = g
        for (int c = 0; c < 3; ++c) {
            float v = (data[src + c] - dmin[c]) / std::max(1e-6f, dmax[c] - dmin[c]);
            out.rgba[dst + c] = (uint8_t)(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
        }
    }
    return true;
}
LutStrip identityLut(int n) {
    LutStrip l; l.size = n; l.rgba.assign((size_t)n * n * n * 4, 255);
    for (int b = 0; b < n; ++b) for (int g = 0; g < n; ++g) for (int r = 0; r < n; ++r) {
        size_t dst = ((size_t)g * (n * n) + (size_t)b * n + r) * 4;
        l.rgba[dst] = (uint8_t)(255.f * r / (n - 1) + 0.5f); l.rgba[dst + 1] = (uint8_t)(255.f * g / (n - 1) + 0.5f); l.rgba[dst + 2] = (uint8_t)(255.f * b / (n - 1) + 0.5f);
    }
    return l;
}

bool load3dlLut(const std::string& path, LutStrip& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line; int n = 0; std::vector<float> v; float maxv = 0.f;
    while (std::getline(f, line)) {
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos || line[s] == '#') continue;
        std::istringstream is(line.substr(s));
        if (n == 0) { int cnt = 0; float x; while (is >> x) ++cnt; if (cnt >= 2) { n = cnt; v.reserve((size_t)n * n * n * 3); } continue; }   // first line: grid positions
        float r, g, b; if (!(is >> r >> g >> b)) continue;
        v.push_back(r); v.push_back(g); v.push_back(b); maxv = std::max({maxv, r, g, b});
    }
    if (n < 2 || v.size() != (size_t)n * n * n * 3) return false;
    float denom = std::pow(2.f, std::ceil(std::log2(std::max(maxv, 1.f)))) - 1.f;                    // 1023 for 10-bit data
    out.size = n; out.rgba.assign((size_t)n * n * n * 4, 255);
    for (size_t i = 0; i < (size_t)n * n * n; ++i) {
        int b = (int)(i % n), g = (int)((i / n) % n), r = (int)((i / ((size_t)n * n)) % n);          // b fastest, then g, then r
        size_t dst = ((size_t)g * (n * n) + (size_t)b * n + r) * 4;
        for (int c = 0; c < 3; ++c) out.rgba[dst + c] = (uint8_t)(std::clamp(v[i * 3 + c] / denom, 0.f, 1.f) * 255.f + 0.5f);
    }
    return true;
}
bool loadImageLut(const std::string& path, LutStrip& out) {
    int w, h, c; stbi_uc* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px) return false;
    bool ok = true;
    if (w >= h) {                                   // horizontal strip: x = blue slice * N + red, y = green  (same layout as ours)
        int n = h; if (w != n * n) ok = false;
        else { out.size = n; out.rgba.assign(px, px + (size_t)w * h * 4); }
    } else {                                        // vertical stack of N slices of N x N
        int n = w; if (h != n * n) ok = false;
        else {
            out.size = n; out.rgba.assign((size_t)n * n * n * 4, 255);
            for (int b = 0; b < n; ++b) for (int y = 0; y < n; ++y) std::memcpy(&out.rgba[((size_t)y * n * n + (size_t)b * n) * 4], px + ((size_t)(b * n + y) * n) * 4, (size_t)n * 4);
        }
    }
    stbi_image_free(px);
    return ok;
}
bool loadLutFile(const std::string& path, LutStrip& out) {
    std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    if (ext == ".cube") return loadCubeLut(path, out);
    if (ext == ".3dl") return load3dlLut(path, out);
    if (ext == ".png") return loadImageLut(path, out);
    return false;
}
}
