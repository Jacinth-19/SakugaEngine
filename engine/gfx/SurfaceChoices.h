#pragma once
#include <cstdint>
#include <initializer_list>

namespace sk {
// Pure policy over Vulkan-compatible bit masks: 1, 2 and 4 samples; composite alpha bits 1/2/4/8.
// Kept independent of Vulkan so selection can be tested without a GPU or loader.
inline uint32_t preferredSampleCount(uint32_t colorCounts, uint32_t depthCounts) {
    const uint32_t both = colorCounts & depthCounts;
    if (both & 4u) return 4u;
    if (both & 2u) return 2u;
    if (both & 1u) return 1u;
    return 0;
}
inline uint32_t preferredSurfaceTransform(uint32_t supported, uint32_t current) {
    // Identity lets the Android compositor perform its orientation transform; otherwise use the required transform.
    return (supported & 1u) ? 1u : current;
}
inline uint32_t preferredCompositeAlpha(uint32_t supported) {
    for (uint32_t mode : {1u, 2u, 4u, 8u}) if (supported & mode) return mode;
    return 0;
}
}
