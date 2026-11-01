#pragma once
#include "gfx/Context.h"
#include <glm/glm.hpp>
#include <array>
#include <string>

namespace sk {

struct Texture {
    Image image;
    VkSampler sampler = VK_NULL_HANDLE;
    std::string name;
};

enum TexSlot : int {
    TEX_BASE, TEX_NORMAL, TEX_PARAM, TEX_STYLE, TEX_RAMP, TEX_SPEC_RAMP, TEX_LUT, TEX_MATCAP, TEX_MASK,
    TEX_FACE_SDF, TEX_HIGHLIGHT, TEX_HAIR_SPEC, TEX_EMISSION, TEX_FLOW, TEX_COUNT
};
inline const char* texSlotName(int s) {
    static const char* n[] = {"Base (_D)", "Normal (_N)", "Param (_P)", "Style (_ST)", "Diffuse Ramp (_RD)", "Spec Ramp (_RS)",
                              "Color LUT", "MatCap", "Mask", "Face SDF", "Highlight Mask", "Hair Spec Style", "Emission", "Flow"};
    return n[s];
}
// Colour data is sRGB-encoded in files, everything else is linear data.
inline bool texSlotIsSRGB(int s) { return s == TEX_BASE || s == TEX_MATCAP || s == TEX_EMISSION || s == TEX_HIGHLIGHT; }

namespace matdetail {
// Property counts, derived from MaterialProps.inc so adding a property cannot desynchronise the CPU mirror, the std140
// padding or the generated shaders/material_block.glsl.
enum MatCounts {
    kVec4Count = 0
#define SK_MAT_VEC4(name, r, g, b, a, isColor) + 1
#define SK_MAT_FLOAT(name, d, mn, mx) + 0
#include "gfx/MaterialProps.inc"
#undef SK_MAT_VEC4
#undef SK_MAT_FLOAT
    ,
    kFloatCount = 0
#define SK_MAT_VEC4(name, r, g, b, a, isColor) + 0
#define SK_MAT_FLOAT(name, d, mn, mx) + 1
#include "gfx/MaterialProps.inc"
#undef SK_MAT_VEC4
#undef SK_MAT_FLOAT
};
constexpr int kPadFloats = (4 - (kFloatCount % 4)) % 4;   // std140 rounds the uniform block up to 16 bytes
}  // namespace matdetail

// CPU mirror of the std140 MaterialUBO. Generated layout: see MaterialProps.inc.
struct MaterialParams {
#define SK_MAT_VEC4(name, r, g, b, a, isColor) glm::vec4 name{r, g, b, a};
#define SK_MAT_FLOAT(name, d, mn, mx) float name = d;
#include "gfx/MaterialProps.inc"
#undef SK_MAT_VEC4
#undef SK_MAT_FLOAT
    std::array<float, (size_t)matdetail::kPadFloats> _pad{};
};
static_assert(sizeof(MaterialParams) == (size_t)matdetail::kVec4Count * 16 + (size_t)(matdetail::kFloatCount + matdetail::kPadFloats) * 4,
              "MaterialParams must match shaders/material_block.glsl (regenerate it with tools/gen_material_glsl.py)");

struct Material {
    std::string name;
    MaterialParams params;
    std::array<Texture*, TEX_COUNT> tex{};
    Buffer ubo;
    VkDescriptorSet set = VK_NULL_HANDLE;
    // state last pushed to the GPU (flushMaterial only touches the GPU when something changed)
    MaterialParams uploaded;
    std::array<Texture*, TEX_COUNT> bound{};
    bool everUploaded = false;
    bool castShadow = true;
    bool faceFrame = false;                 // the face SDF frame (forward/right) follows the head bone; set by the loader for VRM face skin
    bool transparent = false;               // alpha-blended: drawn after all opaque geometry, sorted back to front, no depth write, no outline
    bool outline() const { return params.outlineAlpha > 0.001f && params.outlineWidth > 0.f; }
};

}  // namespace sk
