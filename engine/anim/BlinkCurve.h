#pragma once
// AAA-anime eye blink ("mepachi"): 3 keys on 2s/3s timing.
//   key 0  open    -> close fast (linear ramp up)
//   key 1  closed  -> in-between snaps back to "near open" (small weight), the upper lid leads
//   key 2  open    -> eased out (slower than the close, so the close reads as a snap)
// Total duration is 80 + 40 + 80 ms. Intervals are random in [3, 6] s; 5% double blinks, 10% half blinks; a look/head
// turn can trigger an extra blink. Previously this lived inline in Entity::updateExpressions() (unreachable from tests and
// unseeded); the curve is now a pure function here and Entity only feeds it time and randomness.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/Rand.h"

namespace sk {

struct BlinkTiming {
    float close = 0.080f;      // open -> closed
    float hold = 0.040f;       // closed -> in-between ("near open")
    float open = 0.080f;       // in-between -> open
    float intervalMin = 3.0f;  // seconds between blinks
    float intervalMax = 6.0f;
    float responseHalflife = 0.020f;   // smoothing of the expression weight
};

inline float blinkDuration(const BlinkTiming& t) { return t.close + t.hold + t.open; }

// Weight in [0, 1] for a single blink, `elapsed` seconds after it started. 0 before/after, 1 at the closed key.
inline float blinkWeightAt(float elapsed, bool doubleBlink, bool halfBlink, const BlinkTiming& t) {
    if (elapsed <= 0.f) return 0.f;
    const float total = blinkDuration(t);
    // a double blink re-runs the whole curve once more after a short downtime
    const float gap = 0.060f;
    if (doubleBlink && elapsed >= total + gap) elapsed = elapsed - total - gap;
    if (elapsed <= 0.f || elapsed >= total) return 0.f;

    const float inBetween = halfBlink ? 0.50f : 0.25f;   // the "near open" in-between key
    if (elapsed < t.close) {                             // close: linear, fast snap
        return elapsed / t.close;
    }
    if (elapsed < t.close + t.hold) {                    // closed -> in-between
        float u = (elapsed - t.close) / std::max(t.hold, 1e-6f);
        return 1.f + (inBetween - 1.f) * u;
    }
    float u = (elapsed - t.close - t.hold) / std::max(t.open, 1e-6f);   // in-between -> open, eased out
    return inBetween * (1.f - u) * (1.f - u);
}

// Total time the curve is non-zero (including the gap of a double blink).
inline float blinkActiveTime(bool doubleBlink, const BlinkTiming& t) {
    return blinkDuration(t) * (doubleBlink ? 2.f : 1.f) + (doubleBlink ? 0.060f : 0.f);
}

struct BlinkClock {
    BlinkTiming timing;
    bool enabled = true;
    bool blinking = false, doubleBlink = false, halfBlink = false;
    float timer = 4.5f;      // seconds until the next blink
    float clock = 0.f;       // seconds since the current blink started
    float weight = 0.f;      // smoothed output weight
    Rng rng{0x5A17EDu};

    // Schedules the next blink: random interval in [intervalMin, intervalMax], 5% double, 10% half.
    void schedule(float extraDelay = 0.f) {
        timer = rng.range(timing.intervalMin, timing.intervalMax) + extraDelay;
        doubleBlink = rng.chance(0.05f);
        halfBlink = !doubleBlink && rng.chance(0.111f);   // 0.111 of the remaining 95% ~= 10% overall
    }

    // Starts a blink now (gaze-triggered blinks use this).
    void trigger() {
        if (blinking) return;
        blinking = true; clock = 0.f;
        doubleBlink = rng.chance(0.05f);
        halfBlink = !doubleBlink && rng.chance(0.111f);
        schedule();
    }

    // Advances the clock; `lookTriggered` requests an extra blink (head/eye turn).
    // Returns the smoothed weight for the "blink" expression in [0, 1].
    float step(float dt, bool lookTriggered) {
        if (!enabled) { weight = 0.f; return 0.f; }
        clock += dt;
        if (!blinking) {
            timer -= dt;
            if (timer <= 0.f || lookTriggered) trigger();
        }
        float target = blinking ? blinkWeightAt(clock, doubleBlink, halfBlink, timing) : 0.f;
        if (blinking && clock >= blinkActiveTime(doubleBlink, timing)) {
            blinking = false; doubleBlink = false; halfBlink = false; clock = 0.f;
            target = 0.f;
        }
        float k = 1.f - std::exp(-dt / std::max(timing.responseHalflife, 1e-5f));
        weight += (target - weight) * k;
        weight = std::clamp(weight, 0.f, 1.f);
        return weight;
    }
};

}  // namespace sk
