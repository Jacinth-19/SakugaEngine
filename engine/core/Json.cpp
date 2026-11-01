#include "core/Json.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>
namespace sk {
namespace {
const Json kNull;
struct P {
    const char* p; const char* e; bool err = false;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool lit(const char* w) { size_t n = std::strlen(w); if ((size_t)(e - p) >= n && !std::strncmp(p, w, n)) { p += n; return true; } return false; }
    std::string str() {
        std::string r; ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                switch (*p) {
                    case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
                    case 'b': r += '\b'; break; case 'f': r += '\f'; break;
                    case 'u': {
                        unsigned cp = 0; for (int i = 1; i <= 4 && p + i < e; ++i) { char c = p[i]; cp = cp * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10); }
                        p += 4;
                        if (cp < 0x80) r += (char)cp;
                        else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 63)); }
                        else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 63)); r += (char)(0x80 | (cp & 63)); }
                        break; }
                    default: r += *p;
                }
                ++p;
            } else r += *p++;
        }
        if (p < e) ++p; else err = true;
        return r;
    }
    Json val() {
        ws(); Json j;
        if (p >= e) { err = true; return j; }
        if (*p == '{') {
            ++p; j.type = Json::Obj; ws();
            if (p < e && *p == '}') { ++p; return j; }
            while (p < e) {
                ws(); if (*p != '"') { err = true; return j; }
                std::string k = str(); ws();
                if (p >= e || *p != ':') { err = true; return j; }
                ++p; Json v = val(); j.o.emplace_back(std::move(k), std::move(v)); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                err = true; return j;
            }
        } else if (*p == '[') {
            ++p; j.type = Json::Arr; ws();
            if (p < e && *p == ']') { ++p; return j; }
            while (p < e) {
                j.a.push_back(val()); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                err = true; return j;
            }
        } else if (*p == '"') { j.type = Json::Str; j.s = str(); }
        else if (lit("true")) { j.type = Json::Bool; j.b = true; }
        else if (lit("false")) { j.type = Json::Bool; }
        else if (lit("null")) {}
        else { char* end; j.n = std::strtod(p, &end); if (end == p) { err = true; return j; } j.type = Json::Num; p = end; }
        return j;
    }
};
}
Json Json::parse(const std::string& text, bool* ok) {
    P p{text.data(), text.data() + text.size()};
    // skip UTF-8 BOM
    if (text.size() >= 3 && (uint8_t)text[0] == 0xEF) p.p += 3;
    Json j = p.val();
    if (ok) *ok = !p.err;
    return j;
}
const Json& Json::operator[](const char* key) const {
    if (type == Obj) for (auto& kv : o) if (kv.first == key) return kv.second;
    return kNull;
}
const Json& Json::operator[](size_t i) const { return (type == Arr && i < a.size()) ? a[i] : kNull; }
bool Json::has(const char* key) const { if (type == Obj) for (auto& kv : o) if (kv.first == key) return true; return false; }
}
