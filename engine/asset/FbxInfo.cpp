#include "asset/FbxInfo.h"
#include "ufbx/ufbx.h"
#include <memory>
namespace sk {
bool inspectFbx(const std::string& path, FbxInfo& out, std::string& error) {
    out = {}; error.clear();
    ufbx_load_opts opts{};
    opts.load_external_files = false; // never implicitly open paths named by an untrusted FBX
    opts.node_depth_limit = 512;
    opts.temp_allocator.memory_limit = 512ull * 1024 * 1024;
    opts.result_allocator.memory_limit = 512ull * 1024 * 1024;
    ufbx_error err{};
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(ufbx_load_file(path.c_str(), &opts, &err), ufbx_free_scene);
    if (!scene) { char message[1024]{}; ufbx_format_error(message, sizeof message, &err); error = message; return false; }
    out.nodes = scene->nodes.count; out.meshes = scene->meshes.count; out.skins = scene->skin_deformers.count;
    out.materials = scene->materials.count; out.animations = scene->anim_stacks.count;
    for (size_t i = 0; i < scene->meshes.count; ++i) out.triangles += scene->meshes.data[i]->num_triangles;
    for (size_t i = 0; i < scene->textures.count; ++i) if (scene->textures.data[i]->content.size) ++out.embeddedTextures;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (n->bone) out.boneNames.emplace_back(n->name.data, n->name.length);
    }
    return true;
}
}
