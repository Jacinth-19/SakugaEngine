#pragma once
// Small deterministic PRNG (xorshift32). The engine used to call std::rand() without ever calling std::srand(), so every
// run produced the same "random" blink/interaction sequence. Entity code now owns an Rng with an explicit seed, which also
// makes the procedural animation layers reproducible in tests and in screenshot/recording runs.
#include <cstdint>

namespace sk {

struct Rng {
    uint32_t state = 0x9E3779B9u;

    explicit Rng(uint32_t seed = 0x9E3779B9u) : state(seed ? seed : 0x9E3779B9u) {}

    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }
    // uniform in [0, 1]
    float next01() { return (float)(next() >> 8) * (1.f / 16777216.f); }
    // uniform in [lo, hi]
    float range(float lo, float hi) { return lo + (hi - lo) * next01(); }
    int rangeInt(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
    // true with probability p
    bool chance(float p) { return p > 0.f && next01() < p; }
};

// Stable per-entity seed so two characters do not blink in lockstep.
inline uint32_t hashSeed(const char* s, uint32_t salt = 2166136261u) {
    uint32_t h = salt;
    if (s) for (; *s; ++s) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h ? h : 2166136261u;
}

}  // namespace sk
