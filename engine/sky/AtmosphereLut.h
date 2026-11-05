#pragma once
// CPU-precomputed transmittance through an Earth-like spherical atmosphere, in kilometres.
// This deliberately small 8-bit LUT is an input to the stylised sky, not a full sky-view
// or multiple-scattering solution. It is generated for the active model and uploaded as linear RGBA.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sk {
constexpr int kAtmosphereLutWidth = 256, kAtmosphereLutHeight = 64;
constexpr double kPlanetRadiusKm = 6360.0, kAtmosphereHeightKm = 100.0;
struct AtmosphereParameters {
    float planetRadiusKm = 6360.f, atmosphereHeightKm = 100.f;
    float rayleighHeightKm = 8.f, mieHeightKm = 1.2f, ozone = 1.f, groundAlbedo = .1f;
    bool operator==(const AtmosphereParameters&) const = default;
};
inline AtmosphereParameters sanitizeAtmosphere(AtmosphereParameters p) {
    auto limit = [](float v, float fallback, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
    p.planetRadiusKm = limit(p.planetRadiusKm, 6360, 1000, 10000);
    p.atmosphereHeightKm = limit(p.atmosphereHeightKm, 100, 20, 150);
    p.rayleighHeightKm = limit(p.rayleighHeightKm, 8, 2, 20);
    p.mieHeightKm = limit(p.mieHeightKm, 1.2f, .5f, 4);
    p.ozone = limit(p.ozone, 1, 0, 4); p.groundAlbedo = limit(p.groundAlbedo, .1f, 0, 1);
    return p;
}

// RGB = Rayleigh+Mie+ozone transmittance, A = Mie-only transmittance. Rays that hit
// the opaque ground before leaving the atmosphere have zero transmittance.
inline std::array<double, 4> atmosphereTransmittance(double altitudeKm, double mu, int steps = 48, AtmosphereParameters params = {}) {
    const auto p = sanitizeAtmosphere(params);
    const double planetRadius = p.planetRadiusKm, atmosphereHeight = p.atmosphereHeightKm;
    altitudeKm = std::clamp(altitudeKm, 0.0, atmosphereHeight);
    mu = std::clamp(mu, -1.0, 1.0);
    steps = std::max(steps, 1);
    const double r = planetRadius + altitudeKm;
    const double top = planetRadius + atmosphereHeight;
    const double rm = r * mu;
    const double topDistance = -rm + std::sqrt(std::max(0.0, rm * rm + top * top - r * r));
    const double groundDiscriminant = rm * rm + planetRadius * planetRadius - r * r;
    // At the ground, a downward ray intersects immediately. A tangent ray is not occluded.
    if (mu < 0.0 && groundDiscriminant > 1e-7) {
        const double groundDistance = -rm - std::sqrt(groundDiscriminant);
        if (groundDistance >= 0.0 && groundDistance < topDistance) return {0, 0, 0, 0};
    }
    const double ds = topDistance / steps;
    double rayleighDepth = 0.0, mieDepth = 0.0, ozoneDepth = 0.0;
    for (int i = 0; i < steps; ++i) {
        const double t = (i + 0.5) * ds;
        const double height = std::max(0.0, std::sqrt(r * r + 2.0 * rm * t + t * t) - planetRadius);
        rayleighDepth += std::exp(-height / p.rayleighHeightKm) * ds;
        mieDepth += std::exp(-height / p.mieHeightKm) * ds;
        ozoneDepth += std::max(0.0, 1.0 - std::abs(height - 25.0) / 15.0) * ds;
    }
    constexpr double betaR[3] = {0.005802, 0.013558, 0.033100};
    constexpr double betaM = 0.003996;
    constexpr double betaO[3] = {0.000650, 0.001881, 0.000085};
    std::array<double, 4> result{};
    for (int c = 0; c < 3; ++c) result[c] = std::exp(-betaR[c] * rayleighDepth - betaM * mieDepth - betaO[c] * ozoneDepth * p.ozone);
    result[3] = std::exp(-betaM * mieDepth);
    return result;
}

// The local horizon dips below zero at altitude. Map only directions that can escape
// without ground intersection, preserving resolution near the horizon.
inline double atmosphereHorizonMu(double altitudeKm, AtmosphereParameters params = {}) {
    const auto p = sanitizeAtmosphere(params);
    const double r = p.planetRadiusKm + std::clamp(altitudeKm, 0.0, double(p.atmosphereHeightKm));
    return -std::sqrt(std::max(0.0, 1.0 - (p.planetRadiusKm / r) * (p.planetRadiusKm / r)));
}
// Non-linear angular coordinates concentrate samples near the horizon; sqrt encoding
// gives dim transmittance values more precision in the existing portable RGBA8 texture.
inline double atmosphereLutMu(double u, double h, AtmosphereParameters p = {}) {
    const double horizon = atmosphereHorizonMu(h, p); u = std::clamp(u, 0.0, 1.0);
    return horizon + (1.0 - horizon) * u * u;
}
inline double atmosphereLutCoordinate(double mu, double h, AtmosphereParameters p = {}) {
    const double horizon = atmosphereHorizonMu(h, p);
    return std::sqrt(std::clamp((mu - horizon) / (1.0 - horizon), 0.0, 1.0));
}
inline uint8_t encodeAtmosphereTransmittance(double t) {
    return static_cast<uint8_t>(std::lround(std::sqrt(std::clamp(t, 0.0, 1.0)) * 255.0));
}
inline double decodeAtmosphereTransmittance(uint8_t v) { const double t = v / 255.0; return t * t; }
inline std::vector<uint8_t> buildAtmosphereLut(AtmosphereParameters params = {}) {
    const auto p = sanitizeAtmosphere(params);
    std::vector<uint8_t> pixels(kAtmosphereLutWidth * kAtmosphereLutHeight * 4);
    for (int y = 0; y < kAtmosphereLutHeight; ++y) for (int x = 0; x < kAtmosphereLutWidth; ++x) {
        const double h = double(p.atmosphereHeightKm) * y / (kAtmosphereLutHeight - 1);
        const double mu = atmosphereLutMu(double(x) / (kAtmosphereLutWidth - 1), h, p);
        const auto t = atmosphereTransmittance(h, mu, 48, p);
        for (int c = 0; c < 4; ++c) pixels[(y * kAtmosphereLutWidth + x) * 4 + c] =
            encodeAtmosphereTransmittance(t[c]);
    }
    return pixels;
}
}
