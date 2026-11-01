#pragma once
#include "scene/Model.h"
#include "asset/ProceduralTextures.h"
#include <memory>
#include <string>
namespace sk {
struct GltfOptions {
    int maxTextureSize = 2048;
    bool outlines = true;
    float outlineWidthScale = 1.f;
    float bodyUnderMask = 0.006f;      // metres: body-skin vertices that lie under tops/bottoms are pushed inward by this much (procedural "body under clothes" corrective shape); 0 = off
    float minOutlineWidth = 0.0024f;   // metres; VRM files often specify very thin lines
};
// Loads .glb/.gltf/.vrm (VRM 1.0 humanoid map + MToon -> Sakuga/Hybrid material mapping).
bool loadHdrImage(const std::string& path, std::vector<float>& rgb, int& w, int& h);   // Radiance .hdr -> linear RGB floats
std::unique_ptr<Model> loadGltfModel(Renderer& renderer, const StyleTextures& style, const std::string& path, const GltfOptions& opt = {});
}
