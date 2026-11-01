#include "core/FileUtil.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;
namespace sk {
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    size_t n = (size_t)f.tellg(); f.seekg(0);
    out.resize(n);
    f.read((char*)out.data(), (std::streamsize)n);
    return true;
}
bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
std::string exeDir(const char* argv0) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(argv0), ec);
    if (ec || p.empty()) return fs::current_path().string();
    return p.parent_path().string();
}
std::string findRoot(const char* argv0) {
    if (const char* e = std::getenv("SAKUGA_ROOT")) return e;
    for (fs::path start : { fs::path(exeDir(argv0)), fs::current_path() }) {
        for (fs::path p = start; !p.empty(); p = p.parent_path()) {
            if (fs::exists(p / "assets") && fs::exists(p / "shaders")) return p.string();
            if (p == p.root_path()) break;
        }
    }
    return fs::current_path().string();
}
}
