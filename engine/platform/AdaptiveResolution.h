#pragma once
// Conservative wall-clock resolution controller. No renderer or GPU dependency.
#include "platform/AutoQuality.h"
#include <algorithm>
#include <cmath>
namespace sk {
class AdaptiveResolution {
public:
    // Pause/suspension, manual changes and benchmark starts discard old samples.
    void reset() { elapsed_ = 0; frames_ = 0; slowWindows_ = fastWindows_ = 0; }
    // Evaluate non-overlapping 5-second windows. Downscale after three slow
    // windows (<25 FPS); upscale after four fast windows (>40 FPS). These are
    // frame-loop rates, not GPU measurements and cannot diagnose a CPU bound app.
    float update(float dt, float current, float ceiling, bool enabled) {
        current = safeRenderScale(current); ceiling = safeRenderScale(ceiling);
        if (!enabled || !std::isfinite(dt) || dt <= 0.f || dt > .5f) { reset(); return current; }
        elapsed_ += dt; ++frames_;
        if (elapsed_ < 5.f) return current;
        const float mean = elapsed_ / frames_;
        elapsed_ = 0; frames_ = 0;
        if (mean > .040f) { ++slowWindows_; fastWindows_ = 0; }
        else if (mean < .025f) { ++fastWindows_; slowWindows_ = 0; }
        else { slowWindows_ = fastWindows_ = 0; }
        if (slowWindows_ >= 3) {
            reset(); return std::max(.5f, current - .05f);
        }
        if (fastWindows_ >= 4) {
            reset(); return current < ceiling ? std::min(ceiling, current + .05f) : current;
        }
        return current;
    }
private:
    float elapsed_ = 0.f;
    int frames_ = 0, slowWindows_ = 0, fastWindows_ = 0;
};
}
