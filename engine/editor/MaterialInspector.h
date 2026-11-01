#pragma once
#include "gfx/Renderer.h"
#include <vector>
namespace sk {
// ImGui inspector for every property of the hybrid toon material (generated from MaterialProps.inc).
// Returns true if anything changed (materials are re-flushed to the GPU automatically).
bool drawMaterialInspector(Renderer& r, const std::vector<Material*>& mats, int& selected, bool& syncAll);
const char* debugModeName(int mode);
}
