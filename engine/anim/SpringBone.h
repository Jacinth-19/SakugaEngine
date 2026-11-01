#pragma once
// VRM spring bones: a verlet / "dynamic bone" style simulation of hair, skirts, ribbons and accessories, driven in world space so
// the characters' own motion (running, turning, stopping) throws them around. Supports VRMC_springBone (1.0) and secondaryAnimation (0.x),
// sphere + capsule colliders on body bones, per-joint stiffness / drag / gravity / hit radius.
// SpringSettings / SpringState / the verlet step itself live in anim/SpringStep.h (GPU-free, unit tested).
#include "anim/Rig.h"
#include "anim/SpringStep.h"
#include "scene/Model.h"

namespace sk {

// `pose` / `globals` hold the final animated pose in model space; the simulation rewrites the local rotations of spring joints and
// refreshes `globals` of their subtrees. `T` is the entity transform.
void stepSpringBones(const Model& model, const std::vector<glm::mat4>& restGlobals, Pose& pose, std::vector<glm::mat4>& globals,
                     const glm::mat4& T, float dt, const SpringSettings& s, SpringState& st);

}  // namespace sk
