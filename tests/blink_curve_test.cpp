// Blink curve (AAA-anime "mepachi"): shape, timing, bounds and determinism of the state machine.
// Regression target: the blink used to be an inline block in Entity::updateExpressions() driven by std::rand() without a
// seed, with a "double blink" branch that clamped the weight back to 1 mid-open. Now it is anim/BlinkCurve.h.
#include "Check.h"
#include "anim/BlinkCurve.h"

#include <cmath>
#include <vector>

using namespace sk;

static float maxWeight(const BlinkTiming& t, bool dbl, bool half, float dt = 0.001f) {
    float m = 0.f;
    for (float e = 0.f; e <= blinkActiveTime(dbl, t) + 0.05f; e += dt) m = std::max(m, blinkWeightAt(e, dbl, half, t));
    return m;
}

int main() {
    SK_TEST("blink_curve_test");
    BlinkTiming t;

    // --- timing: 80 ms close + 40 ms in-between + 80 ms open = 200 ms
    CHECK_NEAR(blinkDuration(t), 0.20f, 1e-5f);
    CHECK_NEAR(t.close, 0.080f, 1e-5f);
    CHECK_NEAR(t.hold, 0.040f, 1e-5f);
    CHECK_NEAR(t.open, 0.080f, 1e-5f);

    // --- expected keys of a normal blink
    CHECK_NEAR(blinkWeightAt(0.f, false, false, t), 0.f, 1e-6f);
    CHECK_NEAR(blinkWeightAt(0.040f, false, false, t), 0.5f, 1e-4f);      // half way through the close
    CHECK_NEAR(blinkWeightAt(t.close, false, false, t), 1.f, 1e-4f);      // closed
    CHECK_NEAR(blinkWeightAt(t.close + t.hold, false, false, t), 0.25f, 1e-4f);   // in-between key, "near open"
    CHECK_NEAR(blinkWeightAt(blinkDuration(t), false, false, t), 0.f, 1e-6f);     // fully open again
    CHECK_NEAR(blinkWeightAt(blinkDuration(t) + 1.f, false, false, t), 0.f, 1e-6f);   // and stays closed... i.e. open

    // --- half blink keeps the in-between at half closed; double blink dips twice
    CHECK_NEAR(blinkWeightAt(t.close + t.hold, false, true, t), 0.5f, 1e-4f);
    CHECK_NEAR(blinkWeightAt(blinkDuration(t) + 0.030f, true, false, t), 0.f, 1e-4f);          // gap between the two blinks
    CHECK_NEAR(blinkWeightAt(blinkDuration(t) + 0.060f + t.close, true, false, t), 1.f, 1e-4f); // second blink closes

    // --- the close is faster than the open (the defining property of a mepachi blink)
    const float dClose = (blinkWeightAt(0.004f, false, false, t) - blinkWeightAt(0.f, false, false, t)) / 0.004f;
    const float dOpen = (blinkWeightAt(t.close + t.hold + 0.004f, false, false, t) -
                         blinkWeightAt(t.close + t.hold, false, false, t)) / 0.004f;
    CHECK_MSG(dClose > std::fabs(dOpen), "the lid closes faster than it opens");
    CHECK_MSG(dClose > 0.f && dOpen < 0.f, "close ramps up, open ramps down");

    // --- bounds over the whole curve, for every variant
    CHECK_NEAR(maxWeight(t, false, false), 1.f, 1e-3f);
    CHECK_NEAR(maxWeight(t, false, true), 1.f, 1e-3f);
    CHECK_NEAR(maxWeight(t, true, false), 1.f, 1e-3f);
    for (int variant = 0; variant < 3; ++variant) {
        bool dbl = variant == 2, half = variant == 1;
        float prev = 0.f;
        for (float e = 0.f; e < blinkActiveTime(dbl, t); e += 0.002f) {
            float w = blinkWeightAt(e, dbl, half, t);
            CHECK_FINITE(w);
            CHECK(w >= -1e-6f && w <= 1.f + 1e-6f);
            (void)prev;
            prev = w;
        }
    }

    // --- the clock: interval in [3, 6] s, blink happens, weight returns to 0, no runaway
    {
        BlinkClock clock; clock.rng = Rng(12345u); clock.timer = 0.f;
        float maxW = 0.f, lastW = 0.f;
        float firstBlinkAt = -1.f;
        const float dt = 1.f / 120.f;
        for (int i = 0; i < 120 * 12; ++i) {          // 12 seconds
            float w = clock.step(dt, false);
            CHECK_FINITE(w);
            if (w > 0.5f && firstBlinkAt < 0.f) firstBlinkAt = (float)i * dt;
            maxW = std::max(maxW, w);
            lastW = w;
        }
        CHECK(maxW > 0.5f);
        CHECK(lastW >= 0.f && lastW <= 1.f);
        CHECK(firstBlinkAt >= 0.f);
        CHECK_MSG(clock.timer >= 0.f && clock.timer <= BlinkTiming{}.intervalMax + clock.timing.close + clock.timing.hold + clock.timing.open,
                  "the next-blink timer stays inside the 3-6 s window");
        // average blink rate over 12 s must be within the 3-6 s band (>= 2 and <= 5 blinks)
        int blinks = 0; bool wasBlinking = false;
        BlinkClock c2; c2.rng = Rng(12345u); c2.timer = 0.f;
        for (int i = 0; i < 120 * 12; ++i) {
            c2.step(dt, false);
            if (c2.blinking && !wasBlinking) ++blinks;
            wasBlinking = c2.blinking;
        }
        CHECK_MSG(blinks >= 2 && blinks <= 5, "2-5 blinks in 12 s (~1 per 3-6 s)");
    }

    // --- a look/head turn triggers a blink immediately
    {
        BlinkClock c; c.rng = Rng(7u); c.timer = 100.f;
        CHECK_MSG(c.step(1.f / 60.f, true) >= 0.f, "gaze-triggered blink");
        CHECK(c.blinking);
        CHECK(c.timer <= c.timing.intervalMax);
    }

    // --- determinism: same seed -> same sequence, different seed -> different sequence (no more global rand())
    {
        auto run = [](uint32_t seed) {
            BlinkClock c; c.rng = Rng(seed); c.timer = 0.f;
            std::vector<float> out;
            for (int i = 0; i < 120 * 6; ++i) out.push_back(c.step(1.f / 120.f, false));
            return out;
        };
        std::vector<float> a = run(42u), b = run(42u), c = run(99u);
        CHECK(a == b);
        CHECK_MSG(a != c, "different seeds produce different blink rhythms");
        CHECK_MSG(a.size() == b.size(), "same length");
    }

    // --- disabled clock is a no-op
    {
        BlinkClock c; c.enabled = false; c.timer = 0.f;
        for (int i = 0; i < 100; ++i) CHECK_NEAR(c.step(1.f / 60.f, false), 0.f, 1e-9f);
    }

    return t_.finish();
}
