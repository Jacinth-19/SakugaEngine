#pragma once
// Approximate isotropic second atmospheric scattering order. Each texel is the
// angular mean of direct single-scattered radiance at one altitude and solar
// zenith. The angular and ray quadratures are intentionally coarse; no infinite
// scattering series or physically integrated ground bounce is implied.
#include "sky/SkyScatteringMath.h"
#include <cstdint>
#include <vector>
namespace sk {
constexpr int kMultipleWidth = 33, kMultipleHeight = 16;
constexpr double kMultipleRange = 16.0;
inline double multipleMu(double u) { double x = 2.0*u-1.0; return std::copysign(x*x, x); }
inline double multipleU(double mu) { mu = std::clamp(mu, -1.0, 1.0); return .5 + .5*std::copysign(std::sqrt(std::abs(mu)), mu); }
inline uint8_t encodeMultiple(double v) { return uint8_t(std::lround(std::sqrt(std::clamp(v/kMultipleRange, 0.0, 1.0))*255.0)); }
inline glm::dvec3 multipleFirstOrderRay(double altitude, glm::dvec3 view, glm::dvec3 sun, AtmosphereParameters p) {
    const double r = p.planetRadiusKm+altitude, top = p.planetRadiusKm+p.atmosphereHeightKm, rm = r*view.y;
    double distance = std::max(0.0, -rm + std::sqrt(std::max(0.0, rm*rm+(top-r)*(top+r))));
    double disc = rm*rm+(double(p.planetRadiusKm)-r)*(double(p.planetRadiusKm)+r);
    if (view.y < 0.0 && disc > 1e-7) distance = std::min(distance, std::max(0.0, -rm-std::sqrt(disc)));
    constexpr int steps = 8;
    const double ds=distance/steps, ct=glm::dot(view,sun), g=.76;
    const double pr=.75*(1+ct*ct), pm=std::min(12.0,(1-g*g)/std::pow(std::max(1+g*g-2*g*ct,.02),1.5));
    const glm::dvec3 br(.005802,.013558,.033100), bo(.000650,.001881,.000085);
    glm::dvec3 out(0), trans(1);
    for (int i=0;i<steps;++i) {
        const double t=(i+.5)*ds; glm::dvec3 pos(view.x*t,r+view.y*t,view.z*t);
        const double radius=glm::length(pos), h=std::max(0.0,radius-p.planetRadiusKm);
        const double dr=std::exp(-h/p.rayleighHeightKm), dm=std::exp(-h/p.mieHeightKm);
        const double ozone=std::max(0.0,1.0-std::abs(h-25.0)/15.0);
        auto solar=atmosphereTransmittance(h, glm::dot(pos/radius,sun), 16, p);
        for (int c=0;c<3;++c) {
            double attenuation=std::exp(-(br[c]*dr+.003996*dm+bo[c]*ozone*p.ozone)*ds);
            out[c]+=trans[c]*std::sqrt(attenuation)*solar[c]*(br[c]*dr*pr+.003996*dm*pm)*ds;
            trans[c]*=attenuation;
        }
    }
    return out;
}
inline std::vector<uint8_t> buildMultipleScatteringLut(AtmosphereParameters params = {}) {
    auto p=sanitizeAtmosphere(params); std::vector<uint8_t> pixels(kMultipleWidth*kMultipleHeight*4);
    constexpr int directions=16; constexpr double golden=2.39996322972865332;
    for(int y=0;y<kMultipleHeight;++y) for(int x=0;x<kMultipleWidth;++x) {
        double h=double(p.atmosphereHeightKm)*y/(kMultipleHeight-1), mu=multipleMu(double(x)/(kMultipleWidth-1));
        glm::dvec3 sun(std::sqrt(std::max(0.0,1-mu*mu)),mu,0), mean(0);
        for(int i=0;i<directions;++i) {
            double dy=1.0-2.0*(i+.5)/directions, phi=i*golden, dh=std::sqrt(1-dy*dy);
            mean+=multipleFirstOrderRay(h,{dh*std::cos(phi),dy,dh*std::sin(phi)},sun,p)/double(directions);
        }
        size_t idx=(y*kMultipleWidth+x)*4;
        for(int c=0;c<3;++c) pixels[idx+c]=encodeMultiple(mean[c]);
        pixels[idx+3]=255;
    }
    return pixels;
}
inline glm::dvec3 sampleMultipleScatteringLut(const std::vector<uint8_t>& px, double altitude, double sunMu, AtmosphereParameters params = {}) {
    if(px.size()!=size_t(kMultipleWidth*kMultipleHeight*4)) return glm::dvec3(0);
    auto p=sanitizeAtmosphere(params);
    double fx=multipleU(sunMu)*(kMultipleWidth-1), fy=std::clamp(altitude/double(p.atmosphereHeightKm),0.0,1.0)*(kMultipleHeight-1);
    int x=int(fx), y=int(fy), x1=std::min(x+1,kMultipleWidth-1), y1=std::min(y+1,kMultipleHeight-1);
    double tx=fx-x,ty=fy-y; glm::dvec3 out;
    for(int c=0;c<3;++c) {
        auto at=[&](int a,int b){return px[(b*kMultipleWidth+a)*4+c]/255.0;};
        double e=(at(x,y)*(1-tx)+at(x1,y)*tx)*(1-ty)+(at(x,y1)*(1-tx)+at(x1,y1)*tx)*ty;
        out[c]=e*e*kMultipleRange;
    }
    return out;
}
}
