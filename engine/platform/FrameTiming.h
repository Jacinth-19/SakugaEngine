#pragma once
#include <algorithm>
#include <cmath>

namespace sk {
// Average actual wall-clock frame intervals, not clamped simulation steps. This remains meaningful
// when the simulation limits dt after a slow frame or a pause.
struct FrameTiming {
    float seconds = 0.f;
    float update(float wallSeconds) {
        if (std::isfinite(wallSeconds) && wallSeconds > 0.f) {
            seconds = seconds > 0.f ? seconds * .95f + wallSeconds * .05f : wallSeconds;
        }
        return seconds > 0.f ? 1.f / seconds : 0.f;
    }
};
}
