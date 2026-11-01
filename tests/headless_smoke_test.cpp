// Headless smoke test: drives the GPU-free parts of the engine end to end - collision world, rig evaluation, and the full
// motion-matching update loop (search + transition + inertialization + root motion) for several seconds of simulated play.
// No Vulkan device, no window, no window server.
#include "Check.h"
#include "SyntheticMotion.h"
#include "anim/MotionMatching.h"
#include "anim/Rig.h"
#include "world/World.h"

#include <cmath>
#include <vector>

using namespace sk;

int main() {
    SK_TEST("headless_smoke_test");

    // ------------------------------------------------------------------ collision world
    {
        World w;
        w.groundY = 0.f;
        Collider box; box.name = "step"; box.center = glm::vec3(2.f, 0.25f, 0.f); box.half = glm::vec3(0.5f, 0.25f, 0.5f);
        w.add(box);

        float h = 0.f;
        CHECK(w.groundHeight(glm::vec3(0.f, 3.f, 0.f), 10.f, h));
        CHECK_NEAR(h, 0.f, 1e-4f);
        CHECK(w.groundHeight(glm::vec3(2.f, 3.f, 0.f), 10.f, h));
        CHECK_NEAR(h, 0.5f, 1e-4f);                        // top of the box

        RayHit hit = w.raycast(glm::vec3(0.f, 5.f, 0.f), glm::vec3(0.f, -1.f, 0.f), 20.f);
        CHECK(hit.hit);
        CHECK_NEAR(hit.point.y, 0.f, 1e-3f);

        RayHit miss = w.raycast(glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f), 5.f);
        CHECK(!miss.hit);

        // a sphere inside the box is pushed out, a distant one is untouched
        glm::vec3 push = w.depenetrate(glm::vec3(2.f, 0.5f, 0.f), 0.3f);
        CHECK(glm::length(push) > 0.01f);
        CHECK_FINITE(push.x);
        glm::vec3 none = w.depenetrate(glm::vec3(20.f, 0.5f, 0.f), 0.3f);
        CHECK_NEAR(glm::length(none), 0.f, 1e-5f);
    }

    // ------------------------------------------------------------------ rig evaluation
    Rig rig = sktest::makeSyntheticRig();
    {
        CHECK(rig.isHumanoid());
        CHECK(rig.order.size() == rig.nodes.size());
        Pose rest = rig.restPose();
        CHECK_NEAR((double)rest.r.size(), (double)rig.nodes.size(), 0);
        std::vector<glm::mat4> g, g2;
        rig.globals(rest, g);
        rig.globals(rest, g2);
        CHECK_NEAR((double)g.size(), (double)rig.nodes.size(), 0);
        int head = rig.bone(HumanBone::Head);
        CHECK(head >= 0);
        // the head sits above the hips in the rest pose
        int hips = rig.bone(HumanBone::Hips);
        CHECK(g[head][3].y > g[hips][3].y);
        CHECK_FINITE(g[head][3].y);

        // updateSubtree after a local change matches a full re-evaluation
        Pose moved = rest;
        moved.r[rig.bone(HumanBone::Spine)] = glm::angleAxis(0.4f, glm::vec3(1, 0, 0));
        std::vector<glm::mat4> inc = g;
        rig.updateSubtree(rig.bone(HumanBone::Spine), moved, inc);
        rig.globals(moved, g2);
        for (size_t i = 0; i < inc.size(); ++i)
            for (int c = 0; c < 4; ++c) CHECK_NEAR(inc[i][c][3], g2[i][c][3], 1e-4f);
    }

    // ------------------------------------------------------------------ motion matching, 10 seconds of play
    MotionDatabase db = sktest::makeSyntheticDb();
    {
        MotionMatcher mm;
        mm.init(&rig, &db);
        Pose pose = rig.restPose();
        glm::vec3 pos(0.f, 0.f, 0.f);
        float yaw = 0.f;
        const float dt = 1.f / 60.f;
        const glm::vec3 walkVel(0.f, 0.f, 1.4f);

        // Prime the matcher the way a player would: it starts on the walk clip, so its smoothed velocity is already
        // ~1.6 m/s when the movement input arrives (a cold start from a standstill legitimately prefers the idle clip,
        // because the search compares against the *current* pose features).
        mm.playClip(1, pos, yaw);
        for (int i = 0; i < 60; ++i) mm.update(dt, glm::vec3(0.f), pos, yaw, pose);
        CHECK_MSG(pos.z > 0.5f, "browsing a clip drives root motion");

        double travelled = 0.0;
        int searches = 0;
        for (int i = 0; i < 600; ++i) {
            mm.update(dt, walkVel, pos, yaw, pose);
            if (mm.debug.hasSearch) ++searches;
            if ((i % 60) == 0) {
                CHECK_FINITE(pos.x); CHECK_FINITE(pos.y); CHECK_FINITE(pos.z);
                CHECK_FINITE(yaw);
                for (int b = 0; b < kHumanBoneCount; ++b) {
                    int node = rig.human[b];
                    if (node < 0) continue;
                    CHECK_FINITE(pose.r[node].w);
                    CHECK_NEAR(glm::length(pose.r[node]), 1.f, 1e-3f);
                }
            }
            travelled = std::max(travelled, (double)pos.z);
        }
        CHECK_MSG(searches > 0, "the matcher ran searches");
        CHECK_MSG(mm.debug.transitions > 0, "the matcher transitioned between frames");
        CHECK_MSG(travelled > 6.0, "root motion advanced the character along +Z");
        CHECK_FINITE(mm.debug.cost);
        CHECK(mm.debug.clip >= 0);
        CHECK(mm.debug.frame >= 0);

        // stop moving: the matcher settles on the stand clip and stops translating
        glm::vec3 stopped = pos;
        for (int i = 0; i < 180; ++i) mm.update(dt, glm::vec3(0.f), pos, yaw, pose);
        CHECK_FINITE(pos.x); CHECK_FINITE(yaw);
        CHECK_MSG(glm::length(pos - stopped) < 1.0f, "standing still does not race forward");
        CHECK_MSG(db.frameClip[(int)mm.currentFrame()] == 0, "standing still selects the stand clip");
        CHECK(mm.debug.transitions >= 2);

        // a jump request is accepted and decays
        mm.requestJump(1.f);
        for (int i = 0; i < 30; ++i) mm.update(dt, walkVel, pos, yaw, pose);
        CHECK_FINITE(pos.z);

        // browsing a clip straight through keeps the pose finite
        mm.playClip(2, pos, yaw);
        for (int i = 0; i < 120; ++i) { mm.update(dt, glm::vec3(0.f), pos, yaw, pose); }
        CHECK_FINITE(pos.z);
        CHECK_FINITE(yaw);
        for (int b = 0; b < kHumanBoneCount; ++b) {
            int node = rig.human[b];
            if (node >= 0) CHECK_NEAR(glm::length(pose.r[node]), 1.f, 1e-3f);
        }
    }

    // ------------------------------------------------------------------ determinism of a scripted run
    {
        auto run = [&]() {
            MotionMatcher mm; mm.init(&rig, &db);
            Pose pose = rig.restPose();
            glm::vec3 pos(0.f, 0.f, 0.f); float yaw = 0.f;
            for (int i = 0; i < 240; ++i) mm.update(1.f / 60.f, glm::vec3(0.f, 0.f, 1.4f), pos, yaw, pose);
            return pos;
        };
        glm::vec3 a = run(), b = run();
        CHECK_NEAR(glm::length(a - b), 0.f, 1e-6f);
    }

    return t_.finish();
}
