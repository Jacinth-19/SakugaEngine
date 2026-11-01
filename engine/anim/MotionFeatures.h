#pragma once
// Motion-matching feature cost, extracted from MotionMatcher::search() so the weighting/early-out behaviour can be tested
// against hand-built feature vectors.
#include <algorithm>

namespace sk {

// Weighted squared distance between the query `q` and the database frame `x`, plus an optional per-frame bias (`base`,
// used by the idle-pose term). Stops early once the partial sum exceeds `limit`, which is what makes the brute-force
// search cheap (the running best is the limit).
template <int Dim>
inline float weightedFeatureCost(const float* q, const float* x, const float* weight, float limit, float base = 0.f) {
    float c = base;
    for (int d = 0; d < Dim; ++d) {
        float e = q[d] - x[d];
        c += weight[d] * e * e;
        if (c > limit) return c;
    }
    return c;
}

}  // namespace sk
