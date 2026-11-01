// Posture controls (Unity Animation Rigging analogue): clamping, no-op detection and the chain distribution of the
// hunch correction. Regression target: the old inline code in Entity::update() accepted any value (no clamp, NaN passed
// straight through), always "applied" even when every offset was 0, and its hunch pass recomputed an identity slerp.
#include "Check.h"
#include "anim/Posture.h"

#include <cmath>
#include <limits>

using namespace sk;

int main() {
    SK_TEST("posture_clamp_test");
    PostureLimits limits;

    // --- defaults are a no-op
    {
        PostureControl p;
        CHECK_NEAR(p.weight, 1.f, 1e-6f);
        CHECK_NEAR(p.spine, 0.f, 1e-6f);
    }

    // --- per-bone clamps
    {
        PostureControl p;
        p.spine = 1000.f; p.chest = -1000.f; p.neck = limits.boneLimitDeg + 1.f; p.head = -limits.boneLimitDeg - 1.f;
        PostureControl c = clampPosture(p, limits);
        CHECK_NEAR(c.spine, limits.boneLimitDeg, 1e-6f);
        CHECK_NEAR(c.chest, -limits.boneLimitDeg, 1e-6f);
        CHECK_NEAR(c.neck, limits.boneLimitDeg, 1e-6f);
        CHECK_NEAR(c.head, -limits.boneLimitDeg, 1e-6f);

        p.spine = 10.f; p.chest = -20.f; p.neck = 0.f; p.head = 0.f;    // in range: untouched
        c = clampPosture(p, limits);
        CHECK_NEAR(c.spine, 10.f, 1e-6f);
        CHECK_NEAR(c.chest, -20.f, 1e-6f);
    }

    // --- weight and hunch clamps
    {
        PostureControl p;
        p.weight = 5.f; p.hunchCorrection = 3.f;
        PostureControl c = clampPosture(p, limits);
        CHECK_NEAR(c.weight, 1.f, 1e-6f);
        CHECK_NEAR(c.hunchCorrection, 1.f, 1e-6f);
        p.weight = -2.f; p.hunchCorrection = -1.f;
        c = clampPosture(p, limits);
        CHECK_NEAR(c.weight, 0.f, 1e-6f);
        CHECK_NEAR(c.hunchCorrection, 0.f, 1e-6f);
    }

    // --- NaN / inf are rejected instead of poisoning the pose
    {
        PostureControl p;
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        p.spine = nan; p.chest = inf; p.neck = -inf; p.head = nan; p.weight = nan; p.hunchCorrection = nan;
        PostureControl c = clampPosture(p, limits);
        CHECK_FINITE(c.spine); CHECK_FINITE(c.chest); CHECK_FINITE(c.neck); CHECK_FINITE(c.head);
        CHECK_FINITE(c.weight); CHECK_FINITE(c.hunchCorrection);
        CHECK_NEAR(c.spine, 0.f, 1e-6f);
        CHECK_NEAR(c.weight, 0.f, 1e-6f);
    }

    // --- the layer is skipped when it cannot change anything
    {
        PostureControl p;
        CHECK_MSG(!postureActive(p), "all-zero control is a no-op");
        p.spine = 0.001f;
        CHECK(postureActive(p));
        p.spine = 0.f; p.weight = 0.f; p.head = 20.f;
        CHECK_MSG(!postureActive(p), "weight 0 disables the layer");
        p.weight = 1.f; p.hunchCorrection = 0.5f;
        CHECK(postureActive(p));
    }

    // --- hunch correction: bounded, distributed over the chain, scaled by weight, zero when unset
    {
        PostureControl p; p.hunchCorrection = 1.f; p.weight = 1.f;
        CHECK_NEAR(hunchTotalRadians(p, limits), -limits.hunchLimitDeg * 0.01745329252f, 1e-6f);
        float rad[3];
        hunchRadiansPerBone(p, rad, limits);
        CHECK_NEAR(rad[0] + rad[1] + rad[2], hunchTotalRadians(p, limits), 1e-6f);      // shares are normalised
        CHECK_NEAR(rad[0], hunchTotalRadians(p, limits) * 0.4f, 1e-5f);                 // spine takes the most
        CHECK(rad[0] <= rad[1] && rad[1] >= rad[2]);

        p.weight = 0.5f;
        hunchRadiansPerBone(p, rad, limits);
        CHECK_NEAR(rad[0] + rad[1] + rad[2], hunchTotalRadians(p, limits), 1e-6f);
        CHECK_NEAR(hunchTotalRadians(p, limits), -limits.hunchLimitDeg * 0.5f * 0.01745329252f, 1e-6f);

        p.hunchCorrection = 0.f;
        hunchRadiansPerBone(p, rad, limits);
        CHECK_NEAR(rad[0], 0.f, 1e-9f);
        CHECK_NEAR(rad[1], 0.f, 1e-9f);
        CHECK_NEAR(rad[2], 0.f, 1e-9f);

        // all-zero shares fall back to "no rotation" instead of dividing by zero
        PostureLimits zeroShares; zeroShares.spineShare = zeroShares.chestShare = zeroShares.neckShare = 0.f;
        p.hunchCorrection = 1.f; p.weight = 1.f;
        hunchRadiansPerBone(p, rad, zeroShares);
        CHECK_FINITE(rad[0]);
        CHECK_NEAR(rad[0] + rad[1] + rad[2], hunchTotalRadians(p, zeroShares), 1e-6f);

        // never exceeds the budget, whatever the input
        for (float h = -2.f; h <= 2.f; h += 0.25f) {
            PostureControl q; q.hunchCorrection = h; q.weight = 1.f;
            CHECK(std::fabs(hunchTotalRadians(q, limits)) <= limits.hunchLimitDeg * 0.01745329252f + 1e-6f);
        }
    }

    return t_.finish();
}
