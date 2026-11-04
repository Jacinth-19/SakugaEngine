#pragma once
// CPU-precomputed transmittance through an Earth-like spherical atmosphere, in kilometres.
// This deliberately small 8-bit LUT is an input to the stylised sky, not a full sky-view
// or multiple-scattering solution. It is generated once per process and uploaded as linear RGBA.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sk {
constexpr int kAtmosphereLutWidth = 256, kAtmosphereLutHeight = 64;
constexpr double kPlanetRadiusKm = 6360.0, kAtmosphereHeightKm = 100.0;

// RGB = Rayleigh+Mie transmittance, A = Mie-only transmittance. Rays that hit
// the opaque ground before leaving the atmosphere have zero transmittance.
inline std::array<double, 4> atmosphereTransmittance(double altitudeKm, double mu, int steps = 48) {
    altitudeKm = std::clamp(altitudeKm, 0.0, kAtmosphereHeightKm);
    mu = std::clamp(mu, -1.0, 1.0);
    steps = std::max(steps, 1);
    const double r = kPlanetRadiusKm + altitudeKm;
    const double top = kPlanetRadiusKm + kAtmosphereHeightKm;
    const double rm = r * mu;
    const double topDistance = -rm + std::sqrt(std::max(0.0, rm * rm + top * top - r * r));
    const double groundDiscriminant = rm * rm + kPlanetRadiusKm * kPlanetRadiusKm - r * r;
    // At the ground, a downward ray intersects immediately. A tangent ray is not occluded.
    if (mu < 0.0 && groundDiscriminant >= 0.0) {
        const double groundDistance = -rm - std::sqrt(groundDiscriminant);
        if (groundDistance >= 0.0 && groundDistance < topDistance) return {0, 0, 0, 0};
    }
    const double ds = topDistance / steps;
    double rayleighDepth = 0.0, mieDepth = 0.0;
    for (int i = 0; i < steps; ++i) {
        const double t = (i + 0.5) * ds;
        const double height = std::max(0.0, std::sqrt(r * r + 2.0 * rm * t + t * t) - kPlanetRadiusKm);
        rayleighDepth += std::exp(-height / 8.0) * ds;
        mieDepth += std::exp(-height / 1.2) * ds;
    }
    constexpr double betaR[3] = {0.005802, 0.013558, 0.033100};
    constexpr double betaM = 0.003996;
    std::array<double, 4> result{};
    for (int c = 0; c < 3; ++c) result[c] = std::exp(-betaR[c] * rayleighDepth - betaM * mieDepth);
    result[3] = std::exp(-betaM * mieDepth);
    return result;
}

inline std::vector<uint8_t> buildAtmosphereLut() {
    std::vector<uint8_t> pixels(kAtmosphereLutWidth * kAtmosphereLutHeight * 4);
    for (int y = 0; y < kAtmosphereLutHeight; ++y) for (int x = 0; x < kAtmosphereLutWidth; ++x) {
        const double h = kAtmosphereHeightKm * y / (kAtmosphereLutHeight - 1);
        // The renderer samples only the upper hemisphere, giving the horizon more precision.
        const double mu = double(x) / (kAtmosphereLutWidth - 1);
        const auto t = atmosphereTransmittance(h, mu);
        for (int c = 0; c < 4; ++c) pixels[(y * kAtmosphereLutWidth + x) * 4 + c] =
            static_cast<uint8_t>(std::lround(std::clamp(t[c], 0.0, 1.0) * 255.0));
    }
    return pixels;
}
}
