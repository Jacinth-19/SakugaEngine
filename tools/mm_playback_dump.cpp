// mm_playback_dump - records what the motion matcher actually does, frame by frame, without a GPU.
//
// It links the same GPU-free engine subset the tests use (BVH loader, rig/FK, motion matching), builds the database from
// the clips on disk exactly like the sandbox does, and drives a MotionMatcher through two phases:
//
//   A. matching - a scripted route (walk, run, stop, jump, walk, turn, idle) where the matcher picks the frames;
//   B. sweep    - every clip in the database is played for a fraction of a second, so all of them are exercised.
//
// Each frame it writes the clip that is playing, its index against the total, how many clip switches have happened so far,
// the character's root position/yaw and the 22 humanoid joint positions (rig space, metres). tools/gen_motion_debug_video.py
// turns that into a video: the numbers are the real ones, not a mock-up.
//
//   mm_playback_dump                         # ~/.cache/lafan1 (or assets/motion), writes motion_debug.csv
//   mm_playback_dump --out /tmp/m.csv --fps 30 --sweep 0.35 --motion-dir DIR --clips 20
#include "SyntheticMotion.h"

#include "anim/MotionMatching.h"
#include "anim/Rig.h"
#include "core/FileUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace sk;
namespace fs = std::filesystem;

static std::vector<std::string> findClips(const std::vector<std::string>& extraDirs) {
    std::vector<std::string> dirs = extraDirs;
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
    std::string out = "motion_debug.csv";
    std::vector<std::string> motionDirs;
    float fps = 30.f, sweep = 0.35f, routeScale = 1.f;
    int maxClips = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--out") out = next();
        else if (a == "--motion-dir") motionDirs.push_back(next());
        else if (a == "--clips") maxClips = std::atoi(next().c_str());
        else if (a == "--fps") fps = (float)std::atof(next().c_str());
        else if (a == "--sweep") sweep = (float)std::atof(next().c_str());
        else if (a == "--route-scale") routeScale = (float)std::atof(next().c_str());
        else if (a == "-h" || a == "--help") {
            std::printf("mm_playback_dump [--out file.csv] [--motion-dir DIR]... [--clips N] [--fps 30] [--sweep 0.35]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
    }

    if (!std::isfinite(fps) || fps < 5.f || fps > 120.f || !std::isfinite(sweep) || sweep <= 0.f || sweep > 60.f || !std::isfinite(routeScale) || routeScale < 0.f || routeScale > 10.f) {
        std::fprintf(stderr, "invalid fps, sweep, or route scale\n"); return 2;
    }
    std::vector<std::string> files = findClips(motionDirs);
    if (files.empty()) { std::fprintf(stderr, "no .bvh files found (run tools/fetch_assets.sh --motion-only)\n"); return 1; }
    if (maxClips > 0 && (int)files.size() > maxClips) files.resize(maxClips);

    Rig rig = sktest::makeSyntheticRig();
    MotionDatabase db;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    if (!db.buildFromFiles(rig, files, "", 0, &err)) { std::fprintf(stderr, "database: %s\n", err.c_str()); return 1; }
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "database: " << db.clipNames.size() << " clips, " << db.frameCount << " frames, built in "
              << (int)buildMs << " ms\n";
    if (db.clipNames.empty()) return 1;

    MotionMatcher mm;
    mm.init(&rig, &db);
    Pose pose = rig.restPose();
    glm::vec3 pos(0.f);
    float yaw = 0.f;
    mm.reset(pos, yaw);

    std::ofstream f(out);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    f << "frame,t,phase,posx,posy,posz,yaw,clip,clip_count,clip_frames,clip_name,browsing,transitions";
    for (int b = 0; b < kHumanBoneCount; ++b) f << ",j" << b << "_x,j" << b << "_y,j" << b << "_z";
    f << "\n";

    const float dt = 1.f / std::max(5.f, fps);
    int frame = 0, switches = 0, lastClip = -1;
    std::vector<char> visited(db.clipNames.size(), 0), swept(db.clipNames.size(), 0);
    bool failed = false;
    std::vector<glm::mat4> globals;

    auto emit = [&](const char* phase) {
        rig.globals(pose, globals);           // same call the renderer makes: FK for every node
        const int clip = mm.debug.clip;
        if (clip < 0 || clip >= (int)db.clipNames.size()) { failed = true; return; }
        if (clip != lastClip) { ++switches; lastClip = clip; visited[clip] = 1; }
        f << frame << ',' << (frame * dt) << ',' << phase << ',' << pos.x << ',' << pos.y << ',' << pos.z << ','
          << glm::degrees(yaw) << ',' << clip << ',' << db.clipNames.size() << ','
          << (db.clipEnd[clip] - db.clipStart[clip]) << ',' << db.clipNames[clip] << ','
          << (mm.browsing() ? 1 : 0) << ',' << mm.debug.transitions;
        for (int b = 0; b < kHumanBoneCount; ++b) {
            const int node = rig.human[b];
            const glm::vec3 p = node >= 0 ? glm::vec3(globals[node][3]) : glm::vec3(0.f);
            f << ',' << p.x << ',' << p.y << ',' << p.z;
        }
        f << '\n';
        ++frame;
    };

    // ------------------------------------------------------------------ A. the scripted route
    struct Leg { float seconds; float speed; glm::vec2 dir; bool jump; const char* phase; };
    const Leg route[] = {
        {4.0f, 1.4f, {0.f, 1.f}, false, "walk"},
        {4.0f, 4.2f, {0.f, 1.f}, false, "run"},
        {1.2f, 0.0f, {0.f, 1.f}, false, "stop"},
        {1.0f, 1.6f, {0.f, 1.f}, true,  "jump"},
        {2.8f, 1.5f, {0.f, 1.f}, false, "walk"},
        {1.5f, 1.6f, {1.f, 0.f}, false, "turn"},
        {1.5f, 0.0f, {0.f, 1.f}, false, "idle"},
    };
    for (const Leg& leg : route) {
        const int steps = std::max(1, (int)std::lround(leg.seconds * routeScale * fps));
        if (leg.jump) mm.requestJump();
        for (int s = 0; s < steps; ++s) {
            const glm::vec3 want = glm::vec3(leg.dir.x, 0.f, leg.dir.y) * leg.speed;
            mm.update(dt, want, pos, yaw, pose);
            emit(leg.phase);
        }
    }

    // ------------------------------------------------------------------ B. every clip once
    for (int c = 0; c < (int)db.clipNames.size(); ++c) {
        mm.playClip(c, pos, yaw);
        const int steps = std::max(3, (int)std::lround(sweep * fps));
        for (int s = 0; s < steps; ++s) {
            mm.update(dt, glm::vec3(0.f), pos, yaw, pose);
            if (mm.debug.clip != c || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) || !std::isfinite(yaw)) failed = true;
            else swept[c] = 1;
            emit("sweep");
        }
    }
    f.close();

    int played = 0;
    for (char v : visited) played += v ? 1 : 0;
    std::cout << "wrote " << out << ": " << frame << " frames, " << switches << " clip switches, " << played << "/"
              << db.clipNames.size() << " clips played\n";
    int sweptCount = 0; for (char v : swept) sweptCount += v ? 1 : 0;
    std::cout << "sweep: " << sweptCount << "/" << db.clipNames.size() << " clips sampled\n";
    return !failed && f && sweptCount == (int)db.clipNames.size() ? 0 : 3;
}
