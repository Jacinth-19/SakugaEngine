#include "Check.h"
#include "SyntheticMotion.h"
#include "anim/MotionMatching.h"
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <chrono>

int main() {
    SK_TEST("motion_cache_test");
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / ("sakuga-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    auto path = dir / "db.bin";
    sk::MotionDatabase db = sktest::makeSyntheticDb(), loaded;
    db.clipStart[0] = 0; // production caches tile every frame, including invalid T-pose lead-ins
    CHECK(db.saveCache(path.string(), 42));
    CHECK(!loaded.loadCache(path.string(), 43));
    CHECK(loaded.loadCache(path.string(), 42));
    CHECK(loaded.frameCount == db.frameCount);
    CHECK(loaded.clipNames == db.clipNames);
    CHECK(loaded.features == db.features);
    CHECK(loaded.localRot.size() == db.localRot.size());
    fs::resize_file(path, fs::file_size(path) / 2);
    CHECK(!loaded.loadCache(path.string(), 42));
    CHECK(!db.saveCache((dir / "missing" / "db.bin").string(), 42));
    fs::remove_all(dir);
    return t_.finish();
}
