#include "asset/FbxInfo.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: sakuga-fbx-info <file.fbx>\nInspects FBX only; it does not import or render it.\n"; return 2; }
    sk::FbxInfo info; std::string error;
    if (!sk::inspectFbx(argv[1], info, error)) { std::cerr << "FBX error: " << error << '\n'; return 1; }
    std::cout << "FBX inspection (not a renderer/importer)\nNodes: " << info.nodes << "\nMeshes: " << info.meshes
              << "\nTriangles: " << info.triangles << "\nSkins: " << info.skins << "\nMaterials: " << info.materials
              << "\nEmbedded textures: " << info.embeddedTextures << "\nAnimation stacks: " << info.animations
              << "\nBones: " << info.boneNames.size() << '\n';
    for (const auto& name : info.boneNames) std::cout << "  " << name << '\n';
    return 0;
}
