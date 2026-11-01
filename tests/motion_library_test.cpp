// The real mocap library: builds a motion-matching database from the LAFAN1 clips on this machine and checks that every
// clip family is usable - i.e. that characters animate from the whole set and not from the two clips committed in
// assets/motion/.
//
// The clips are NOT in the repository (333 MB, CC BY-NC-ND): get them with
//     tools/fetch_assets.sh --motion-only
// which puts them in $SAKUGA_MOTION_DIR or ~/.cache/lafan1. Without them this test reports SKIP and passes, so ctest
// stays green on a fresh clone.
//
//   motion_library_test            # first 12 clips (fast, a few hundred ms)
//   motion_library_test --full     # the whole library; asserts all 9 clip families are searchable
#include "Check.h"
#include "SyntheticMotion.h"
#include "anim/MotionMatching.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace sk;
namespace fs = std::filesystem;

static std::vector<std::string> findClips() {
    std::vector<std::string> dirs;
    if (const char* e = std::getenv("SAKUGA_MOTION_DIR")) dirs.push_back(e);
    if (const char* h = std::getenv("HOME")) dirs.push_back(std::string(h) + "/.cache/lafan1");
    dirs.push_back(std::string(SAKUGA_ROOT) + "/assets/motion");
    std::vector<std::string> files;
    std::set<std::string> seen;
    for (const std::string& d : dirs) {
        std::error_code ec;
        if (!fs::exists(d, ec)) continue;
        for (const auto& f : fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec))
            if (f.path().extension() == ".bvh" && seen.insert(f.path().stem().string()).second) files.push_back(f.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

int main(int argc, char** argv) {
    SK_TEST("motion_library_test");
    const bool full = argc > 1 && std::strcmp(argv[1], "--full") == 0;

    std::vector<std::string> files = findClips();
    if (files.empty()) {
        std::cout << "motion_library_test: SKIP (no .bvh files found; run tools/fetch_assets.sh --motion-only)\n";
        return 0;
    }
    const int clips = full ? 0 : 12;                                   // 0 = everything
    if (!full && files.size() > (size_t)clips) {
        // Spread the subset over the clip families (round robin), so a small run still exercises walk/run/dance/fight...
        std::vector<std::string> spread, buckets[kMotionCategoryCount];
        for (const std::string& f : files) buckets[motionCategoryOf(fs::path(f).stem().string())].push_back(f);
        for (int round = 0; (int)spread.size() < clips; ++round) {
            bool added = false;
            for (int c = 0; c < kMotionCategoryCount && (int)spread.size() < clips; ++c) {
                if (round < (int)buckets[c].size()) { spread.push_back(buckets[c][round]); added = true; }
            }
            if (!added) break;
        }
        std::sort(spread.begin(), spread.end());          // deterministic
        files.swap(spread);
    }
    std::cout << "motion_library_test: " << files.size() << " clip(s)"
              << (full ? ", building the whole library\n" : ", building a family-spread subset\n");

    Rig rig = sktest::makeSyntheticRig();
    MotionDatabase db;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    const bool ok = db.buildFromFiles(rig, files, "", clips, &err);
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK_MSG(ok, "database built: " + err);
    if (!ok) return t_.finish();

    const size_t expectedClips = std::min<size_t>(files.size(), clips == 0 ? files.size() : (size_t)clips);
    std::cout << "  " << db.clipNames.size() << " clips, " << db.frameCount << " frames, built in " << (long)buildMs << " ms\n";

    // ------------------------------------------------------------------ every clip was parsed and retargeted
    CHECK_NEAR((double)db.clipNames.size(), (double)expectedClips, 0);   // no clip silently skipped
    CHECK(db.frameCount > 1000);
    CHECK_NEAR((double)db.valid.size(), (double)db.frameCount, 0);
    int searchable = 0;
    for (char v : db.valid) searchable += v != 0;
    CHECK(searchable > 0);
    CHECK_MSG(searchable > db.frameCount / 2, "most frames are searchable (T-pose lead-ins excluded)");

    // normalisation: the stored means are the raw means, and applying them centres the features (mean ~0, sd ~1)
    {
        double sum[kFeatureDim] = {};
        size_t n = 0;
        for (int f = 0; f < db.frameCount; ++f) {
            if (!db.valid[f]) continue;
            for (int d = 0; d < kFeatureDim; ++d) sum[d] += db.feat(f)[d];
            ++n;
        }
        CHECK(n > 0);
        for (int d = 0; d < kFeatureDim; ++d) CHECK_NEAR(sum[d] / (double)std::max<size_t>(n, 1), 0.0, 1e-3);
        CHECK_FINITE(db.featureMean[12]);
    }
    for (int g = 0; g < FG_Count; ++g) {
        CHECK_FINITE(db.featureScale[g]);
        CHECK(db.featureScale[g] > 0.f);
    }

    // pose data is complete and finite
    CHECK_NEAR((double)db.localRot.size(), (double)db.frameCount * kHumanBoneCount, 0);
    CHECK_NEAR((double)db.hipsLocalPos.size(), (double)db.frameCount, 0);
    CHECK_NEAR((double)db.rootPos.size(), (double)db.frameCount, 0);
    CHECK_NEAR((double)db.rootYaw.size(), (double)db.frameCount, 0);
    for (int b = 0; b < kHumanBoneCount; ++b) CHECK_MSG(db.mapped[b], std::string("bone ") + humanBoneName((HumanBone)b) + " is mapped");
    for (size_t i = 0; i < db.localRot.size(); i += 997) CHECK_NEAR(glm::length(db.localRot[i]), 1.f, 1e-2f);

    // clip ranges tile the frame array exactly
    int frames = 0;
    for (size_t c = 0; c < db.clipStart.size(); ++c) {
        CHECK_NEAR((double)db.clipStart[c], (double)frames, 0);
        CHECK(db.clipEnd[c] > db.clipStart[c]);
        frames = db.clipEnd[c];
        CHECK(db.clipCategory[c] >= 0 && db.clipCategory[c] < kMotionCategoryCount);
        for (int f = db.clipStart[c]; f < db.clipEnd[c]; ++f) {
            if (db.frameClip[f] != (int)c) { CHECK_MSG(false, "frameClip is consistent"); break; }
            if (db.frameCat[f] != (uint8_t)db.clipCategory[c]) { CHECK_MSG(false, "frameCat is consistent"); break; }
        }
    }
    CHECK_NEAR((double)frames, (double)db.frameCount, 0);

    // ------------------------------------------------------------------ every family is reachable
    int presentCategories = 0;
    for (int c = 0; c < kMotionCategoryCount; ++c) {
        int n = db.clipsInCategory(c);
        if (n) ++presentCategories;
        std::cout << "  " << kMotionCategoryNames[c] << ": " << n << " clip(s)\n";
    }
    CHECK_MSG(presentCategories >= 3, "the subset covers several families, not just walk + run");

    // the search finds a frame inside every family it is asked for
    MotionMatcher mm;
    mm.init(&rig, &db);
    float query[kFeatureDim] = {0};
    int reachable = 0;
    for (int c = 0; c < kMotionCategoryCount; ++c) {
        if (!db.clipsInCategory(c)) continue;
        int best = mm.nearestFrame(query, 1u << c);
        CHECK_MSG(best >= 0, std::string("category ") + kMotionCategoryNames[c] + " is searchable");
        if (best >= 0) {
            ++reachable;
            CHECK_NEAR((double)db.frameCat[best], (double)c, 0);
            CHECK(db.clipNames[db.frameClip[best]].size() > 0);
        }
    }
    CHECK_NEAR((double)reachable, (double)presentCategories, 0);

    // with the whole library: all nine LAFAN1 families, and the default mask admits every one of them
    if (full && files.size() >= 70) {
        CHECK_MSG(presentCategories == kMotionCategoryCount, "the full library covers all 9 clip families");
        MMSettings s;
        const uint32_t idleMask = resolveCategoryMask(s, false, 0.f, false);
        const uint32_t movingMask = resolveCategoryMask(s, true, s.runGateSpeed + 1.f, false);
        for (int c = 0; c < kMotionCategoryCount; ++c) {
            if (c == 3 || c == 8) {                                   // jumps / ground: triggered, not ambient
                CHECK_NEAR((double)((idleMask >> c) & 1u), 0.0, 0);
                continue;
            }
            CHECK_MSG(((idleMask >> c) & 1u) != 0, std::string("idle search may use ") + kMotionCategoryNames[c]);
        }
        CHECK((movingMask & (1u << 0)) && (movingMask & (1u << 1)));
    }

    // a full search over the library stays affordable for a crowd (measured, not asserted tightly)
    {
        auto t1 = std::chrono::steady_clock::now();
        const int n = 10;
        for (int i = 0; i < n; ++i) mm.nearestFrame(query, 0xFFFFFFFFu);
        double per = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / n;
        std::cout << "  one search over " << db.frameCount << " frames: " << per << " ms\n";
        CHECK_MSG(per < 100.0, "a search is fast enough to run per character (measured in this build)");
    }

    return t_.finish();
}
