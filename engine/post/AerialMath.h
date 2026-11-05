#pragma once
// CPU reference for bounded, single-scattering aerial perspective over an opaque segment.
#include "sky/AtmosphereLut.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
namespace sk {
struct AerialSegment { glm::dvec3 transmittance{1.0}, inscattering{0.0}; };
inline AerialSegment integrateAerialSegment(double altitudeKm, glm::dvec3 view, glm::dvec3 sun, double distanceKm,
                                             AtmosphereParameters params = {}, int steps = 8, int sunSteps = 24) {
    AerialSegment result;
    const double vl = glm::length(view), sl = glm::length(sun);
    if (!(vl > 1e-12 && sl > 1e-12) || !std::isfinite(altitudeKm) || !std::isfinite(distanceKm) || distanceKm <= 0) return result;
    const auto p = sanitizeAtmosphere(params); view /= vl; sun /= sl;
    altitudeKm = std::clamp(altitudeKm, 0.0, double(p.atmosphereHeightKm));
    distanceKm = std::clamp(distanceKm, 0.0, 150.0); steps = std::clamp(steps, 1, 128);
    const double ds = distanceKm / steps, r = p.planetRadiusKm + altitudeKm;
    const double ct = std::clamp(glm::dot(view, sun), -1.0, 1.0), g = .76;
    const double phaseR = .75 * (1 + ct * ct);
    const double phaseM = std::min(12.0, (1-g*g) / std::pow(std::max(1+g*g-2*g*ct,.02), 1.5));
    const glm::dvec3 betaR(.005802,.013558,.033100), betaO(.000650,.001881,.000085);
    for (int i = 0; i < steps; ++i) {
        const double t = (i + .5)*ds;
        glm::dvec3 pos(view.x*t, r + view.y*t, view.z*t);
        const double radius = glm::length(pos), h = std::max(0.0, radius-p.planetRadiusKm);
        const double dr = std::exp(-h/p.rayleighHeightKm), dm = std::exp(-h/p.mieHeightKm);
        const double ozone = std::max(0.0, 1.0-std::abs(h-25.0)/15.0);
        const auto solar = atmosphereTransmittance(h, glm::dot(pos/radius, sun), sunSteps, p);
        for (int k = 0; k < 3; ++k) {
            double extinction = betaR[k]*dr + .003996*dm + betaO[k]*ozone*p.ozone;
            double segment = std::exp(-extinction*ds);
            double source = betaR[k]*dr*phaseR + .003996*dm*phaseM;
            result.inscattering[k] += result.transmittance[k] * std::sqrt(segment) * solar[k] * source * ds;
            result.transmittance[k] *= segment;
        }
    }
    return result;
}
}
