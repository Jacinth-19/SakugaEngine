#pragma once
// Four separately editable artistic sky slots. JSON is deliberately GPU-free.
#include "core/Json.h"
#include "sky/AtmosphereLut.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace sk {
struct EditableSkyProfile {
    float atmosphere = .4f, rayleigh = 1.f, mie = 1.f;
    float cloudCover = .55f, wind = 1.f;
    glm::vec3 zenithTint{1.f}, horizonTint{1.f};
    bool integratedScattering = true, volumetricClouds = false;
    AtmosphereParameters model{};
};
using EditableSkyProfiles = std::array<EditableSkyProfile, 4>;

inline std::string skyProfilesToJson(const EditableSkyProfiles& slots) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9);
    out << "{\n  \"version\": 1,\n  \"profiles\": [\n";
    for (size_t i = 0; i < slots.size(); ++i) {
        const auto& p = slots[i];
        out << "    {\"atmosphere\":" << p.atmosphere << ",\"rayleigh\":" << p.rayleigh << ",\"mie\":" << p.mie
            << ",\"cloudCover\":" << p.cloudCover << ",\"wind\":" << p.wind
            << ",\"zenithTint\":[" << p.zenithTint.x << ',' << p.zenithTint.y << ',' << p.zenithTint.z
            << "],\"horizonTint\":[" << p.horizonTint.x << ',' << p.horizonTint.y << ',' << p.horizonTint.z
            << "],\"integratedScattering\":" << (p.integratedScattering ? "true" : "false")
            << ",\"volumetricClouds\":" << (p.volumetricClouds ? "true" : "false")
            << ",\"planetRadiusKm\":" << p.model.planetRadiusKm << ",\"atmosphereHeightKm\":" << p.model.atmosphereHeightKm
            << ",\"rayleighHeightKm\":" << p.model.rayleighHeightKm << ",\"mieHeightKm\":" << p.model.mieHeightKm
            << ",\"ozone\":" << p.model.ozone << ",\"groundAlbedo\":" << p.model.groundAlbedo << "}" << (i + 1 == slots.size() ? "\n" : ",\n");
    }
    return out.str() + "  ]\n}\n";
}
// Parse into a temporary so corrupt files never modify the live profiles.
inline bool skyProfilesFromJson(const std::string& text, EditableSkyProfiles* out) {
    if (!out) return false;
    bool ok = false; const Json root = Json::parse(text, &ok);
    if (!ok || !root.isObj() || root["version"].type != Json::Num || root["version"].n != 1 ||
        !root["profiles"].isArr() || root["profiles"].size() != 4) return false;
    EditableSkyProfiles parsed{};
    auto number = [](const Json& v, float lo, float hi, float& dst) {
        if (v.type != Json::Num || !std::isfinite(v.n)) return false;
        dst = static_cast<float>(std::clamp(v.n, double(lo), double(hi))); return true;
    };
    auto color = [&](const Json& j, glm::vec3& dst) {
        if (!j.isArr() || j.size() != 3) return false;
        for (int k = 0; k < 3; ++k) if (!number(j[size_t(k)], 0.f, 2.f, dst[k])) return false;
        return true;
    };
    for (size_t i = 0; i < parsed.size(); ++i) {
        const Json& j = root["profiles"][i]; auto& p = parsed[i];
        if (!j.isObj() || !number(j["atmosphere"], 0, 2, p.atmosphere) || !number(j["rayleigh"], 0, 2, p.rayleigh) ||
            !number(j["mie"], 0, 2, p.mie) || !number(j["cloudCover"], 0, 1, p.cloudCover) || !number(j["wind"], 0, 5, p.wind) ||
            !color(j["zenithTint"], p.zenithTint) || !color(j["horizonTint"], p.horizonTint) ||
            j["integratedScattering"].type != Json::Bool || j["volumetricClouds"].type != Json::Bool ||
            !number(j["planetRadiusKm"],1000,10000,p.model.planetRadiusKm) || !number(j["atmosphereHeightKm"],20,150,p.model.atmosphereHeightKm) ||
            !number(j["rayleighHeightKm"],2,20,p.model.rayleighHeightKm) || !number(j["mieHeightKm"],.5f,4,p.model.mieHeightKm) ||
            !number(j["ozone"],0,4,p.model.ozone) || !number(j["groundAlbedo"],0,1,p.model.groundAlbedo)) return false;
        p.integratedScattering = j["integratedScattering"].b; p.volumetricClouds = j["volumetricClouds"].b;
    }
    *out = parsed; return true;
}
}
