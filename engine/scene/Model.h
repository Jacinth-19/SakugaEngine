#pragma once
#include "anim/Rig.h"
#include "gfx/Material.h"
#include "gfx/Mesh.h"
#include <memory>
#include <string>
#include <vector>

namespace sk {

struct Primitive {
    const GpuMesh* mesh = nullptr;
    int material = 0;
    int node = -1;       // owning node (for rigid parts)
    int skin = -1;       // >=0: GPU-skinned with rig.skins[skin]
    int meshIndex = -1;  // glTF mesh index (VRM expression morph binds refer to it)
    std::string name;
};

// VRM spring bones (hair / clothes / accessories): verlet chains simulated every frame (see anim/SpringBone.*)
struct SpringJoint { int node = -1, child = -1; float stiffness = 1.f, drag = 0.4f, gravityPower = 0.f, hitRadius = 0.02f; glm::vec3 gravityDir{0, -1, 0}; };
struct SpringChain { std::string name; std::vector<SpringJoint> joints; std::vector<int> colliderGroups; };
struct SpringCollider { int node = -1; bool capsule = false; glm::vec3 offset{0}, tail{0}; float radius = 0.05f; };
struct SpringData { std::vector<SpringChain> chains; std::vector<SpringCollider> colliders; std::vector<std::vector<int>> groups; bool empty() const { return chains.empty(); } };

struct MorphBind { int mesh = -1, index = -1; float weight = 1.f; };
struct Expression { std::string name; std::vector<MorphBind> binds; bool binary = false; };

// A loaded character / prop: rig + meshes + materials + clips. Instances are made of a Model + per-instance state (see Entity).
struct Model {
    std::string name, path;
    Rig rig;
    std::vector<Primitive> prims;
    std::vector<Material*> materials;
    std::vector<AnimClip> clips;
    bool isVRM = false;
    int vrmVersion = 0;            // 0 = not VRM, 0 -> 0.x, 1 -> 1.0
    SpringData springs;
    std::vector<Expression> expressions;
    int expression(const std::string& n) const { for (size_t i = 0; i < expressions.size(); ++i) if (expressions[i].name == n) return (int)i; return -1; }
    glm::vec3 lookAtOffset{0, 0.07f, 0.08f};   // eye position in head-bone space (VRM lookAt.offsetFromHeadBone)
    std::string license;
    float height = 1.7f;           // metres, measured from the bind pose
    glm::vec3 boundsMin{0}, boundsMax{0};
};

}  // namespace sk
