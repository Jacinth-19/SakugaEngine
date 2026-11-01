// The real mocap library: builds a motion-matching database from the LAFAN1 clips on this machine and checks that every
// clip family is usable - i.e. that characters animate from the whole set and not from the two clips committed in
// assets/motion/.
//
// The clips are NOT in the repository (333 MB, CC BY-NC-ND): get them with
//     tools/fetch_assets.sh --motion-only
// which puts them in $SAKUGA_MOTION_DIR or ~/.cache/lafan1. With fewer than 12 clips (a fresh clone only has the two
// vendored ones) the test reports SKIP and passes, so ctest stays green in CI.
//
//   motion_library_test            # 12 clips spread over the families (fast, a few hundred ms)
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
    // assets/motion/ always carries two vendored clips, so "not empty" is not "the library is here": the checks below are
    // about a library that spans the clip families. Below the threshold this is a SKIP, which keeps `ctest` green on a
    // fresh clone (and in CI) where tools/fetch_assets.sh has not run.
    const size_t needed = full ? 40 : 12;
    if (files.size() < needed) {
        std::cout << "motion_library_test: SKIP (" << files.size() << " clip(s) found, need " << needed
                  << "; run tools/fetch_assets.sh --motion-only for the 77-clip LAFAN1 set)\n";
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

    // Exercise actual clip browsing for every loaded clip on a synthetic humanoid rig, without a GPU.
    // This verifies sampled poses/root motion are finite, not visual retargeting quality on every model.
    {
        glm::vec3 pos(0.f); float yaw = 0.f;
        Pose pose = rig.restPose(); mm.reset(pos, yaw);
        for (size_t c = 0; c < db.clipNames.size(); ++c) {
            mm.playClip(static_cast<int>(c), pos, yaw);
            for (int i = 0; i < 12; ++i) {
                mm.update(1.f / 30.f, glm::vec3(0.f), pos, yaw, pose);
                CHECK_MSG(mm.debug.clip == static_cast<int>(c), db.clipNames[c]);
                CHECK_FINITE(pos.x); CHECK_FINITE(pos.y); CHECK_FINITE(pos.z); CHECK_FINITE(yaw);
                for (int b = 0; b < kHumanBoneCount; ++b) {
                    const auto &q = pose.r[rig.human[b]];
                    CHECK_FINITE(q.x); CHECK_FINITE(q.y); CHECK_FINITE(q.z); CHECK_FINITE(q.w);
                }
            }
        }
        std::cout << "  browsed " << db.clipNames.size() << " clips for 12 frames each\n";
    }

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
