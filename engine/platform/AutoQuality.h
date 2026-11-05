#pragma once
// Hardware-based starting points, not benchmark results. Kept GPU-free for tests.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
namespace sk {
enum class DeviceKind { Unknown, Integrated, Discrete, Virtual, Cpu };
enum class QualityTier { Performance, Balanced, High };
struct QualityDefaults { QualityTier tier; float renderScale; uint32_t maxSamples; bool bloom; bool pointShadows; bool integratedSky; };
inline QualityDefaults qualityDefaults(QualityTier tier) {
    switch (tier) {
        case QualityTier::Performance: return {tier, .75f, 1, false, false, false};
        case QualityTier::Balanced: return {tier, .85f, 2, true, false, true};
        default: return {tier, 1.f, 4, true, true, true};
    }
}
inline QualityTier autoQuality(DeviceKind device, bool android) {
    if (android || device == DeviceKind::Integrated || device == DeviceKind::Cpu || device == DeviceKind::Virtual) return QualityTier::Performance;
    if (device == DeviceKind::Discrete) return QualityTier::High;
    return QualityTier::Balanced;
}
inline const char* qualityName(QualityTier tier) {
    switch (tier) { case QualityTier::Performance: return "Performance"; case QualityTier::Balanced: return "Balanced"; default: return "High"; }
}
inline float safeRenderScale(float scale) { return std::isfinite(scale) ? std::clamp(scale, .5f, 1.f) : 1.f; }
struct RenderExtent { uint32_t width, height; };
inline RenderExtent scaledExtent(uint32_t width, uint32_t height, float scale) {
    scale = safeRenderScale(scale);
    if (!width || !height) return {0, 0};
    return {std::max(1u, uint32_t(std::lround(double(width)*scale))), std::max(1u, uint32_t(std::lround(double(height)*scale)))};
}
inline uint32_t cappedSampleCount(uint32_t supported, uint32_t maxSamples) {
    for (uint32_t n : {4u, 2u, 1u}) if (n <= maxSamples && (supported & n)) return n;
    return 0;
}
}
