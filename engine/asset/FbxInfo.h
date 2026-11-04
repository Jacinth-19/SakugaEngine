#pragma once
// CPU-only FBX inspection, not yet a renderable asset importer.
#include <cstddef>
#include <string>
#include <vector>
namespace sk {
struct FbxInfo {
    size_t nodes = 0, meshes = 0, triangles = 0, skins = 0, materials = 0, embeddedTextures = 0, animations = 0;
    std::vector<std::string> boneNames;
};
bool inspectFbx(const std::string& path, FbxInfo& out, std::string& error);
}
