#include "anim/Bvh.h"
#include "core/FileUtil.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace sk {
namespace {
// Tokenizer for BVH text. Deliberately as forgiving as the reference parser (BVHParser.cs in the Unity project, kept in
// reference/unity/): keywords and channel names are case-insensitive, a comma is accepted as a decimal separator, and
// scientific notation is allowed - exporters vary. Corrupt input must fail loudly, never be silently zero-filled.
struct Tok {
    const char* p; const char* e;
    static bool space(char c) { return (unsigned char)c <= ' '; }

    // Next whitespace- or brace-delimited token.
    bool next(std::string& out) {
        while (p < e && space(*p)) ++p;
        if (p >= e) return false;
        const char* s = p;
        if (*p == '{' || *p == '}') { ++p; out.assign(s, 1); return true; }
        while (p < e && !space(*p) && *p != '{' && *p != '}') ++p;
        out.assign(s, p - s);
        return true;
    }
    // Case-insensitive keyword match (BVHParser.cs uppercases everything through its char map).
    static bool kw(const std::string& tok, const char* word) {
        size_t i = 0;
        for (; i < tok.size(); ++i) {
            char a = tok[i], b = word[i];
            if (!b) return false;
            if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
            if (a != b) return false;
        }
        return word[i] == 0;
    }
    // Reads up to and including the next '{', joining the tokens in between (a joint name may contain spaces).
    bool nameUntilBrace(std::string& name) {
        name.clear();
        std::string t;
        while (next(t)) {
            if (t == "{") return true;
            if (!name.empty()) name += ' ';
            name += t;
        }
        return false;
    }
    static bool normalizeNumber(std::string& t) {
        for (char& c : t) if (c == ',') c = '.';      // "0,5" -> "0.5" (BVHParser.cs accepts both)
        return !t.empty();
    }
    bool f(float& out) {
        std::string t;
        if (!next(t) || !normalizeNumber(t)) return false;
        const char* s = t.c_str();
        char* end = nullptr;
        float v = std::strtof(s, &end);
        if (end == s) return false;                    // no digits at all
        if (!std::isfinite(v)) return false;
        out = v;
        return true;
    }
    bool i(int& out) {
        std::string t;
        if (!next(t) || !normalizeNumber(t)) return false;
        const char* s = t.c_str();
        char* end = nullptr;
        long v = std::strtol(s, &end, 10);
        if (end == s) return false;
        out = (int)v;
        return true;
    }
};
}  // namespace

int BvhClip::findJoint(const std::string& n) const {
    for (size_t i = 0; i < joints.size(); ++i) if (joints[i].name == n) return (int)i;
    return -1;
}

bool loadBvh(const std::string& path, BvhClip& out, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = std::filesystem::path(path).filename().string() + ": " + m; return false; };
    std::string text;
    if (!readTextFile(path, text)) return fail("cannot read file");
    Tok t{text.data(), text.data() + text.size()};
    std::string w;
    if (!t.next(w) || !Tok::kw(w, "HIERARCHY")) return fail("missing HIERARCHY");
    out = BvhClip{};
    out.name = std::filesystem::path(path).stem().string();

    std::vector<int> stack;                 // joint indices, or -1 for a block that is not a joint (End Site, unknown)
    int channelCount = 0;
    bool hierarchyEnded = false;
    while (t.next(w)) {
        if (Tok::kw(w, "ROOT") || Tok::kw(w, "JOINT")) {
            BvhJoint j;
            j.parent = stack.empty() ? -1 : stack.back();
            if (j.parent < 0 && !stack.empty()) return fail("joint declared inside an End Site");
            if (!t.nameUntilBrace(j.name)) return fail("joint '" + j.name + "' has no '{'");
            if (j.name.empty()) return fail("joint without a name");
            out.joints.push_back(j);
            stack.push_back((int)out.joints.size() - 1);
        } else if (Tok::kw(w, "End")) {
            std::string site;
            if (!t.next(site) || !Tok::kw(site, "Site")) return fail("expected 'End Site'");
            if (!t.nameUntilBrace(w)) return fail("End Site has no '{'");   // consumes the '{', the name is unused
            stack.push_back(-1);
        } else if (w == "{") {
            stack.push_back(-1);            // block we do not model: skip to its matching '}'
        } else if (w == "}") {
            if (stack.empty()) return fail("unbalanced '}' in the hierarchy");
            stack.pop_back();
            if (stack.empty()) { hierarchyEnded = true; break; }   // root closed: the hierarchy is complete
        } else if (Tok::kw(w, "OFFSET")) {
            float x, y, z;
            if (!t.f(x) || !t.f(y) || !t.f(z)) return fail("malformed OFFSET");
            if (stack.empty()) return fail("OFFSET outside of any joint");
            if (stack.back() >= 0) out.joints[stack.back()].offset = {x, y, z};
        } else if (Tok::kw(w, "CHANNELS")) {
            int n = 0;
            if (!t.i(n) || n < 1 || n > 6) return fail("bad CHANNELS count");
            if (stack.empty() || stack.back() < 0) return fail("CHANNELS outside of a joint");
            BvhJoint& j = out.joints[stack.back()];
            j.channelStart = channelCount;
            for (int k = 0; k < n; ++k) {
                std::string name;
                if (!t.next(name)) return fail("truncated CHANNELS list");
                int c = -1;
                if (Tok::kw(name, "Xposition")) c = 0; else if (Tok::kw(name, "Yposition")) c = 1;
                else if (Tok::kw(name, "Zposition")) c = 2;
                else if (Tok::kw(name, "Xrotation")) c = 3; else if (Tok::kw(name, "Yrotation")) c = 4;
                else if (Tok::kw(name, "Zrotation")) c = 5;
                if (c < 0) return fail("unknown channel '" + name + "'");
                j.channels.push_back(c);
            }
            channelCount += n;
        }
        // anything else is ignored, like the reference parser does for stray whitespace/unknown blocks
    }
    if (!hierarchyEnded) return fail("hierarchy is not closed");
    if (!t.next(w) || !Tok::kw(w, "MOTION")) return fail("missing MOTION");
    t.next(w);                                        // "Frames:"
    if (!t.i(out.frameCount) || out.frameCount <= 0) return fail("bad frame count");
    if (out.frameCount > 20000000) return fail("implausible frame count");
    t.next(w); t.next(w);                             // "Frame" "Time:"
    if (!t.f(out.frameTime)) return fail("bad frame time");
    // Frame times are seconds, but some exporters write milliseconds ("Frame Time: 33.3333").
    if (out.frameTime > 1.f) out.frameTime /= 1000.f;
    if (!(out.frameTime > 1e-4f) || out.frameTime > 0.5f) out.frameTime = 1.f / 30.f;   // implausible: assume 30 fps
    if (out.joints.empty() || channelCount <= 0) return fail("no joints or channels");

    out.channelCount = channelCount;
    const size_t values = (size_t)out.frameCount * (size_t)channelCount;
    // Every value needs at least one character (plus a separator), so a truncated file is rejected before allocating.
    if (values > (size_t)(t.e - t.p)) return fail("motion section is truncated (declared frames exceed the data)");
    out.data.resize(values);
    for (size_t i = 0; i < values; ++i)
        if (!t.f(out.data[i])) return fail("truncated or malformed motion data at value " + std::to_string(i));
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
