#pragma once
// CPU reference for the bounded single-scattering quadrature used by the sky shader.
// This is not a multiple-scattering or sky-view LUT implementation.
#include "sky/AtmosphereLut.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace sk {
inline glm::dvec3 integrateSkySingleScattering(double altitudeKm, glm::dvec3 view, glm::dvec3 sun,
                                               double rayleighWeight = 1, double mieWeight = 1,
                                               int viewSteps = 8, int sunSteps = 24, AtmosphereParameters params = {}) {
    const auto p = sanitizeAtmosphere(params);
    const double vl = glm::length(view), sl = glm::length(sun);
    if (!(vl > 1e-12 && sl > 1e-12) || !std::isfinite(altitudeKm)) return glm::dvec3(0);
    view /= vl; sun /= sl;
    if (view.y < 0) return glm::dvec3(0);
    altitudeKm = std::clamp(altitudeKm, 0.0, double(p.atmosphereHeightKm));
    viewSteps = std::clamp(viewSteps, 1, 128); sunSteps = std::clamp(sunSteps, 1, 128);
    rayleighWeight = std::clamp(rayleighWeight, 0.0, 2.0); mieWeight = std::clamp(mieWeight, 0.0, 2.0);
    const double r = p.planetRadiusKm + altitudeKm, top = p.planetRadiusKm + p.atmosphereHeightKm;
    const double rm = r * view.y;
    const double ds = (-rm + std::sqrt(std::max(0.0, rm * rm + (top-r)*(top+r)))) / viewSteps;
    const double ct = std::clamp(glm::dot(view, sun), -1.0, 1.0), g = .76;
    const double pr = .75 * (1.0 + ct * ct);
    const double pm = std::min(12.0, (1-g*g) / std::pow(std::max(1+g*g-2*g*ct, .02), 1.5));
    const glm::dvec3 betaR(.005802,.013558,.033100), betaO(.000650,.001881,.000085);
    constexpr double betaM = .003996;
    glm::dvec3 sum(0), viewT(1);
    const auto groundSun = atmosphereTransmittance(0, sun.y, sunSteps, p);
    for (int i = 0; i < viewSteps; ++i) {
        const double t = (i+.5)*ds;
        const glm::dvec3 pos(view.x*t, r+view.y*t, view.z*t);
        const double radius = glm::length(pos), h = std::max(0.0, radius-p.planetRadiusKm);
        const double dr = std::exp(-h/p.rayleighHeightKm), dm = std::exp(-h/p.mieHeightKm);
        const double ozone = std::max(0.0, 1.0-std::abs(h-25.0)/15.0);
        const glm::dvec3 extinction = betaR*dr + glm::dvec3(betaM*dm) + betaO*ozone*double(p.ozone);
        const auto solar = atmosphereTransmittance(h, glm::dot(pos/radius, sun), sunSteps, p);
        for (int k = 0; k < 3; ++k) {
            const double attenuation = std::exp(-extinction[k]*ds);
            const double source = betaR[k]*dr*pr*rayleighWeight + betaM*dm*pm*mieWeight;
            const double bounce = p.groundAlbedo * std::max(sun.y, 0.0) * groundSun[k] *
                (betaR[k]*dr*rayleighWeight + betaM*dm*mieWeight) * 0.5;
            sum[k] += viewT[k]*std::sqrt(attenuation)*(solar[k]*source + bounce)*ds;
            viewT[k] *= attenuation;
        }
    }
    return sum;
}
}
