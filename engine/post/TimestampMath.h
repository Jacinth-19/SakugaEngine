#pragma once
#include <cstdint>
namespace sk {
// Vulkan timestamp counters may wrap at timestampValidBits. The duration must fit within one wrap.
inline double timestampMilliseconds(uint64_t start, uint64_t end, uint32_t validBits, double periodNanoseconds) {
    if (validBits == 0 || validBits > 64 || periodNanoseconds <= 0.0) return -1.0;
    uint64_t delta = end - start;
    if (validBits < 64) delta &= (uint64_t(1) << validBits) - 1;
    return static_cast<double>(delta) * periodNanoseconds * 1e-6;
}
}
