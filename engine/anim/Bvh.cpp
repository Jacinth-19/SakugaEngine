#include "anim/Bvh.h"
#include "core/FileUtil.h"
#include <glm/gtc/constants.hpp>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace sk {
namespace {
struct Tok {
    const char* p; const char* e;
    bool next(std::string& out) {
        while (p < e && (unsigned char)*p <= ' ') ++p;
        if (p >= e) return false;
        const char* s = p;
        if (*p == '{' || *p == '}') { ++p; out.assign(s, 1); return true; }
        while (p < e && (unsigned char)*p > ' ' && *p != '{' && *p != '}') ++p;
        out.assign(s, p - s); return true;
    }
    float f() { std::string t; next(t); return (float)std::atof(t.c_str()); }
    int i() { std::string t; next(t); return std::atoi(t.c_str()); }
};
}

int BvhClip::findJoint(const std::string& n) const {
    for (size_t i = 0; i < joints.size(); ++i) if (joints[i].name == n) return (int)i;
    return -1;
}

bool loadBvh(const std::string& path, BvhClip& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    std::string text;
    if (!readTextFile(path, text)) return fail("cannot read file");
    Tok t{text.data(), text.data() + text.size()};
    std::string w;
    if (!t.next(w) || w != "HIERARCHY") return fail("missing HIERARCHY");
    out = BvhClip{};
    out.name = std::filesystem::path(path).stem().string();

    std::vector<int> stack;            // joint indices (or -1 for end-site blocks)
    int pendingParent = -1; bool haveJoint = false;
    int channels = 0;
    while (t.next(w)) {
        if (w == "ROOT" || w == "JOINT") {
            BvhJoint j; t.next(j.name); j.parent = stack.empty() ? -1 : stack.back();
            // stack top may be -1 marker if inside End Site (should not happen)
            out.joints.push_back(j); pendingParent = (int)out.joints.size() - 1; haveJoint = true;
        } else if (w == "End") {
            t.next(w);                 // "Site"
            pendingParent = -1; haveJoint = false;
        } else if (w == "{") {
            stack.push_back(haveJoint ? pendingParent : -1);
            haveJoint = false;
        } else if (w == "}") {
            if (stack.empty()) return fail("unbalanced braces");
            stack.pop_back();
            if (stack.empty()) break;
        } else if (w == "OFFSET") {
            float x = t.f(), y = t.f(), z = t.f();
            if (!stack.empty() && stack.back() >= 0) out.joints[stack.back()].offset = {x, y, z};
        } else if (w == "CHANNELS") {
            int n = t.i();
            BvhJoint& j = out.joints[stack.back()];
            j.channelStart = channels;
            for (int k = 0; k < n; ++k) {
                t.next(w); int c = -1;
                if (w == "Xposition") c = 0; else if (w == "Yposition") c = 1; else if (w == "Zposition") c = 2;
                else if (w == "Xrotation") c = 3; else if (w == "Yrotation") c = 4; else if (w == "Zrotation") c = 5;
                if (c < 0) return fail("unknown channel");
                j.channels.push_back(c);
            }
            channels += n;
        }
    }
    out.channelCount = channels;
    if (!t.next(w) || w != "MOTION") return fail("missing MOTION");
    t.next(w); out.frameCount = t.i();         // "Frames:" N
    t.next(w); t.next(w); out.frameTime = t.f();   // "Frame" "Time:" value
    if (out.frameCount <= 0 || channels <= 0) return fail("empty motion");
    out.data.resize((size_t)out.frameCount * channels);
    for (auto& v : out.data) {
        while (t.p < t.e && (unsigned char)*t.p <= ' ') ++t.p;
        char* end; v = std::strtof(t.p, &end); t.p = end;
    }
    return true;
}

void BvhClip::fk(int frame, float scale, std::vector<glm::vec3>& pos, std::vector<glm::quat>& rot) const {
    pos.resize(joints.size()); rot.resize(joints.size());
    const float* d = &data[(size_t)frame * channelCount];
    const float deg = glm::pi<float>() / 180.f;
    static const glm::vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (size_t i = 0; i < joints.size(); ++i) {
        const BvhJoint& j = joints[i];
        glm::vec3 tr(0); glm::quat q(1, 0, 0, 0);
        for (size_t c = 0; c < j.channels.size(); ++c) {
            float v = d[j.channelStart + c]; int ch = j.channels[c];
            if (ch < 3) tr[ch] = v;
            else q = q * glm::angleAxis(v * deg, axes[ch - 3]);      // listed order == matrix product order
        }
        if (j.parent < 0) { pos[i] = tr * scale; rot[i] = q; }       // root: translation channels already hold world position
        else {
            rot[i] = glm::normalize(rot[j.parent] * q);
            pos[i] = pos[j.parent] + rot[j.parent] * (j.offset * scale);
        }
    }
}
}  // namespace sk
