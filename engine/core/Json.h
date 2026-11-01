#pragma once
#include <string>
#include <vector>
#include <utility>
namespace sk {
// Minimal JSON DOM. Enough for .inputactions, VRM extension blobs, engine config.
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    static Json parse(const std::string& text, bool* ok = nullptr);
    bool isNull() const { return type == Null; }
    bool isObj() const { return type == Obj; }
    bool isArr() const { return type == Arr; }
    size_t size() const { return type == Arr ? a.size() : type == Obj ? o.size() : 0; }
    const Json& operator[](const char* key) const;
    const Json& operator[](size_t i) const;
    bool has(const char* key) const;
    double num(double d = 0) const { return type == Num ? n : d; }
    int    integer(int d = 0) const { return type == Num ? (int)n : d; }
    bool   boolean(bool d = false) const { return type == Bool ? b : d; }
    std::string str(const std::string& d = "") const { return type == Str ? s : d; }
};
}
