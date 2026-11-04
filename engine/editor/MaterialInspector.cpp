#include "editor/MaterialInspector.h"
#include <imgui.h>
#include <cstring>
#include <string>

namespace sk {
namespace {
struct FloatProp { const char* name; float MaterialParams::*ptr; float def, mn, mx; };
struct VecProp { const char* name; glm::vec4 MaterialParams::*ptr; bool color; };
const FloatProp kFloats[] = {
#define SK_MAT_VEC4(name, r, g, b, a, c)
#define SK_MAT_FLOAT(name, d, mn, mx) {#name, &MaterialParams::name, d, mn, mx},
#include "gfx/MaterialProps.inc"
#undef SK_MAT_VEC4
#undef SK_MAT_FLOAT
};
const VecProp kVecs[] = {
#define SK_MAT_VEC4(name, r, g, b, a, c) {#name, &MaterialParams::name, c != 0},
#define SK_MAT_FLOAT(name, d, mn, mx)
#include "gfx/MaterialProps.inc"
#undef SK_MAT_VEC4
#undef SK_MAT_FLOAT
};

const char* groupOf(const std::string& n) {
    auto sw = [&](const char* p) { return n.rfind(p, 0) == 0; };
    if (sw("wet")) return "Wet / Rain";
    if (sw("sdf") || sw("face")) return "Face SDF";
    if (sw("hair")) return "Hair";
    if (sw("outline")) return "Outline";
    if (sw("debug")) return "Debug";
    if (sw("rim") || sw("emission") || sw("highlight") || sw("bloom")) return "Rim / Emission / Highlight";
    if (sw("alpha") || sw("doubleSided")) return "Alpha";
    if (sw("metallic") || sw("smooth") || sw("occlusion") || sw("specRamp") || sw("matCap") || sw("normal") ) return "PBR / Specular";
    return "Stylised Shading";
}
const char* kGroups[] = {"Stylised Shading", "PBR / Specular", "Face SDF", "Hair", "Wet / Rain", "Rim / Emission / Highlight", "Alpha", "Outline", "Debug"};
const char* kDebug[] = {"0 Final", "1 Base", "2 Param (_P)", "3 Style (_ST)", "4 Mask", "5 Face SDF RGB", "6 SDF channel", "7 SDF shadow",
                        "8 Realtime shadow", "9 Ramp shadow", "10 Normal", "11 N.L", "12 Shadow atten", "13 UV"};
}

const char* debugModeName(int m) { return kDebug[std::max(0, std::min(m, 13))]; }

bool drawMaterialInspector(Renderer& r, const std::vector<Material*>& mats, int& selected, bool& syncAll) {
    if (mats.empty()) { ImGui::TextDisabled("no materials"); return false; }
    selected = std::max(0, std::min(selected, (int)mats.size() - 1));
    Material* m = mats[selected];
    // Lightweight colour icons are derived from the actual material base colour; no extra GPU textures required.
    auto swatch = [](Material* mat) {
        glm::vec4 c = glm::clamp(mat->params.baseColor, glm::vec4(0.f), glm::vec4(1.f));
        return ImGui::ColorButton("##material-swatch", ImVec4(c.r, c.g, c.b, c.a),
                                  ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoPicker,
                                  ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    };
    swatch(m); ImGui::SameLine();
    if (ImGui::BeginCombo("Material", m->name.c_str())) {
        for (int i = 0; i < (int)mats.size(); ++i) {
            ImGui::PushID(i);
            bool picked = swatch(mats[i]); ImGui::SameLine();
            if (ImGui::Selectable(mats[i]->name.c_str(), i == selected) || picked) {
                selected = i;
                if (picked) ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    m = mats[selected];
    ImGui::Checkbox("Apply edits to all materials of this model", &syncAll);
    ImGui::SameLine(); if (ImGui::SmallButton("reset##mat")) { m->params = MaterialParams{}; r.flushMaterial(m); }
    bool changed = false;
    auto propagate = [&](auto&& fn) { if (syncAll) for (Material* o : mats) if (o != m) fn(o); };

    for (const char* g : kGroups) {
        if (!ImGui::CollapsingHeader(g, std::string(g) == "Stylised Shading" ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        ImGui::PushID(g);
        for (const auto& v : kVecs) {
            if (std::strcmp(groupOf(v.name), g) != 0) continue;
            glm::vec4& val = m->params.*(v.ptr);
            bool c = v.color ? ImGui::ColorEdit4(v.name, &val.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)
                             : ImGui::DragFloat4(v.name, &val.x, 0.01f);
            if (c) { changed = true; glm::vec4 nv = val; propagate([&](Material* o) { o->params.*(v.ptr) = nv; r.flushMaterial(o); }); }
        }
        for (const auto& f : kFloats) {
            if (std::strcmp(groupOf(f.name), g) != 0) continue;
            float& val = m->params.*(f.ptr);
            bool c;
            if (std::strcmp(f.name, "debugMode") == 0) {
                int dm = (int)(val + 0.5f);
                c = ImGui::Combo("debugMode", &dm, kDebug, 14); if (c) val = (float)dm;
            } else if (std::strcmp(f.name, "sdfChannel") == 0) {
                int ch = (int)(val + 0.5f); const char* names[] = {"R", "G", "B", "A"};
                c = ImGui::Combo("sdfChannel", &ch, names, 4); if (c) val = (float)ch;
            } else c = ImGui::SliderFloat(f.name, &val, f.mn, f.mx, "%.3f");
            if (c) { changed = true; float nv = val; propagate([&](Material* o) { o->params.*(f.ptr) = nv; r.flushMaterial(o); }); }
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Textures")) {
        for (int i = 0; i < TEX_COUNT; ++i) ImGui::Text("%-18s %s", texSlotName(i), m->tex[i] ? m->tex[i]->name.c_str() : "-");
    }
    if (changed) r.flushMaterial(m);
    return changed;
}
}  // namespace sk
