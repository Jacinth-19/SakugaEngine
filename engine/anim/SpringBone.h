#pragma once
// VRM spring bones: a verlet / "dynamic bone" style simulation of hair, skirts, ribbons and accessories, driven in world space so
// the characters' own motion (running, turning, stopping) throws them around. Supports VRMC_springBone (1.0) and secondaryAnimation (0.x),
// sphere + capsule colliders on body bones, per-joint stiffness / drag / gravity / hit radius.
#include "anim/Rig.h"
#include "scene/Model.h"

namespace sk {

struct SpringSettings {
    bool enabled = true;
    float stiffnessScale = 1.f, dragScale = 1.f, gravityScale = 1.f;
    float windStrength = 0.f; glm::vec3 windDir{1, 0, 0.3f};      // optional ambient breeze
    float time = 0.f;
};

struct SpringState {
    struct J { glm::vec3 cur{0}, prev{0}; glm::vec3 boneAxisLocal{0, 1, 0}; float boneLength = 0.05f; glm::quat initRot{1, 0, 0, 0}; bool init = false; };
    std::vector<std::vector<J>> chains;
    glm::vec3 lastPos{0}; bool inited = false;
    void reset() { inited = false; for (auto& c : chains) for (auto& j : c) j.init = false; }
};

// `pose` / `globals` hold the final animated pose in model space; the simulation rewrites the local rotations of spring joints and
// refreshes `globals` of their subtrees. `T` is the entity transform.
void stepSpringBones(const Model& model, const std::vector<glm::mat4>& restGlobals, Pose& pose, std::vector<glm::mat4>& globals,
                     const glm::mat4& T, float dt, const SpringSettings& s, SpringState& st);

}  // namespace sk
