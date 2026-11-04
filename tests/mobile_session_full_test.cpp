#include "Check.h"
#include "MobileSession.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

int main() {
    SK_TEST("mobile_session_full_test");
    namespace fs = std::filesystem;
    const char* dir = std::getenv("SAKUGA_MOTION_DIR");
    std::string root = dir ? dir : std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.cache/lafan1";
    std::error_code ec; std::vector<std::string> paths;
    for (const auto& f : fs::directory_iterator(root, ec)) if (f.path().extension() == ".bvh") paths.push_back(f.path().string());
    if (paths.size() != 77) { std::printf("SKIP: complete 77-clip LAFAN1 data not present\n"); return 0; }
    std::sort(paths.begin(), paths.end());
    sk::MobileSession session; std::string error;
    CHECK_MSG(session.load(paths, error), error);
    if (session.clipCount() != 77) { CHECK(session.clipCount() == 77); return t_.finish(); }
    session.update(1.f / 30.f, 0, 0);
    int previous = session.clipIndex();
    CHECK(previous >= 0 && previous < 77);
    for (int i = 0; i < 77; ++i) {
        session.nextClip(); session.update(1.f / 30.f, 0, 0);
        CHECK(session.clipIndex() == (previous + 1) % 77);
        CHECK(!session.clipName().empty());
        for (float v : session.joints()) CHECK_FINITE(v);
        previous = session.clipIndex();
    }
    return t_.finish();
}
