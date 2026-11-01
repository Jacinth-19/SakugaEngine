#pragma once
#include "gfx/Renderer.h"
namespace sk {
// Stand-ins for the art-pipeline textures the Endfield-style shader expects (_RD, _RS, LUT, matcap, flow, face SDF ...).
// Swap any of these for authored textures through the material inspector / MaterialDesc.
struct StyleTextures {
    Texture *ramp = nullptr, *specRamp = nullptr, *lut = nullptr, *matcap = nullptr, *hairSpec = nullptr,
            *flow = nullptr, *faceSDF = nullptr, *checker = nullptr;
};
StyleTextures createStyleTextures(Renderer& r);
}
