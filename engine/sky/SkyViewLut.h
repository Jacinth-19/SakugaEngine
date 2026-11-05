#pragma once
// Low-resolution, CPU-generated sky-view LUT for the existing single-scattering
// approximation. Coordinates are relative solar azimuth and sqrt(view elevation).
// It is NOT Hillaire's complete sky-view/multiple-scattering implementation.
#include "sky/SkyScatteringMath.h"
#include "sky/MultipleScatteringLut.h"
#include <array>
#include <cstdint>
#include <vector>

namespace sk {
constexpr int kSkyViewWidth = 65, kSkyViewHeight = 33;
constexpr double kSkyViewRange = 16.0;
constexpr double kSkyViewPi = 3.14159265358979323846;
inline uint8_t encodeSkyView(double value) {
    return static_cast<uint8_t>(std::lround(std::sqrt(std::clamp(value / kSkyViewRange, 0.0, 1.0)) * 255.0));
}
inline double decodeSkyView(double encoded) { return encoded * encoded * kSkyViewRange; }
inline std::vector<uint8_t> buildSkyViewLut(double altitudeKm, double sunMu, double rayleighWeight,
                                             double mieWeight, AtmosphereParameters p = {},
                                             const std::vector<uint8_t>* multiple = nullptr) {
    sunMu = std::clamp(sunMu, -1.0, 1.0);
    const glm::dvec3 sun(std::sqrt(std::max(0.0, 1.0 - sunMu * sunMu)), sunMu, 0.0);
    std::vector<uint8_t> pixels(kSkyViewWidth * kSkyViewHeight * 4);
    for (int y = 0; y < kSkyViewHeight; ++y) {
        double v = double(y) / (kSkyViewHeight - 1), mu = v * v;
        double horizontal = std::sqrt(std::max(0.0, 1.0 - mu * mu));
        for (int x = 0; x < kSkyViewWidth; ++x) {
            double phi = 2.0 * kSkyViewPi * x / (kSkyViewWidth - 1) - kSkyViewPi;
            glm::dvec3 view(horizontal * std::cos(phi), mu, horizontal * std::sin(phi));
            auto radiance = integrateSkySingleScattering(altitudeKm, view, sun, rayleighWeight, mieWeight, 8, 24, p);
            if (multiple && multiple->size() == size_t(kMultipleWidth*kMultipleHeight*4)) {
                // Integrate the isotropic second-order source along the same view ray.
                auto model=sanitizeAtmosphere(p);
                double alt=std::clamp(altitudeKm,0.0,double(model.atmosphereHeightKm));
                double r=model.planetRadiusKm+alt, top=model.planetRadiusKm+model.atmosphereHeightKm, rm=r*view.y;
                double ds=(-rm+std::sqrt(std::max(0.0,rm*rm+(top-r)*(top+r))))/8.0;
                glm::dvec3 trans(1), br(.005802,.013558,.033100), bo(.000650,.001881,.000085);
                for(int j=0;j<8;++j) {
                    double t=(j+.5)*ds; glm::dvec3 pos(view.x*t,r+view.y*t,view.z*t);
                    double radius=glm::length(pos), h=std::max(0.0,radius-model.planetRadiusKm);
                    double dr=std::exp(-h/model.rayleighHeightKm), dm=std::exp(-h/model.mieHeightKm);
                    double ozone=std::max(0.0,1.0-std::abs(h-25.0)/15.0);
                    glm::dvec3 ms=sampleMultipleScatteringLut(*multiple,h,glm::dot(pos/radius,sun),model);
                    for(int c=0;c<3;++c) {
                        double attenuation=std::exp(-(br[c]*dr+.003996*dm+bo[c]*ozone*model.ozone)*ds);
                        radiance[c]+=trans[c]*std::sqrt(attenuation)*ms[c]*(br[c]*dr*rayleighWeight+.003996*dm*mieWeight)*ds;
                        trans[c]*=attenuation;
                    }
                }
            }
            size_t i = (y * kSkyViewWidth + x) * 4;
            for (int c = 0; c < 3; ++c) pixels[i+c] = encodeSkyView(radiance[c]);
            pixels[i+3] = 255;
        }
    }
    return pixels;
}
// Mirrors hardware bilinear interpolation in the square-root-encoded domain.
inline glm::vec3 sampleSkyViewLut(const std::vector<uint8_t>& pixels, glm::vec3 view, glm::vec3 sun) {
    if (pixels.size() != size_t(kSkyViewWidth * kSkyViewHeight * 4) || !(glm::length(view) > 1e-8f) || !(glm::length(sun) > 1e-8f)) return glm::vec3(0);
    view = glm::normalize(view); sun = glm::normalize(sun);
    if (view.y < 0.f) return glm::vec3(0);
    double phi = std::atan2(view.z, view.x) - std::atan2(sun.z, sun.x);
    phi = std::remainder(phi, 2.0 * kSkyViewPi);
    double fx = (phi / (2.0 * kSkyViewPi) + .5) * (kSkyViewWidth - 1);
    double fy = std::sqrt(std::clamp(double(view.y), 0.0, 1.0)) * (kSkyViewHeight - 1);
    int x0 = std::clamp(int(std::floor(fx)), 0, kSkyViewWidth-1), x1 = std::min(x0+1, kSkyViewWidth-1);
    int y0 = std::clamp(int(std::floor(fy)), 0, kSkyViewHeight-1), y1 = std::min(y0+1, kSkyViewHeight-1);
    double tx = fx-x0, ty = fy-y0; glm::vec3 result;
    for (int c=0; c<3; ++c) {
        auto at = [&](int x, int y) { return pixels[(y*kSkyViewWidth+x)*4+c] / 255.0; };
        double a = at(x0,y0)*(1-tx)+at(x1,y0)*tx, b = at(x0,y1)*(1-tx)+at(x1,y1)*tx;
        result[c] = float(decodeSkyView(a*(1-ty)+b*ty));
    }
    return result;
}
}
