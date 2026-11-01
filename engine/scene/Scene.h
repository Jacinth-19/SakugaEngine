#pragma once
#include "anim/BlinkCurve.h"
#include "anim/FootIK.h"
#include "anim/HandIK.h"
#include "anim/LookAt.h"
#include "anim/Posture.h"
#include "anim/SpringBone.h"
#include "anim/MotionMatching.h"
#include "gfx/Renderer.h"
#include "scene/Model.h"
#include <memory>
#include <string>
#include <vector>

namespace sk {

class World;

struct Entity {
    std::string name;
    std::shared_ptr<Model> model;
    glm::vec3 pos{0};
    float yaw = 0.f;                      // radians about +Y, 0 = facing +Z
    float scale = 1.f;
    bool visible = true;
    bool useCustomTransform = false; glm::mat4 customTransform{1.f};   // props with pitch / roll (ramps)

    // animation state
    Pose pose;
    std::vector<glm::mat4> globals, restGlobals;
    int clip = -1; float clipTime = 0.f, clipSpeed = 1.f; bool loop = true;
    std::shared_ptr<MotionDatabase> mmDb;
    std::unique_ptr<MotionMatcher> mm;    // when set, drives the humanoid bones every frame

    // NPC retargeting: an NPC shares the hero's database + matcher type; the matcher runs on the hero rig and is retargeted here
    std::shared_ptr<Model> mmModel;       // the model whose rig the matcher drives (hero model); null = same as `model`
    Pose mmPose; std::vector<glm::mat4> mmGlobals, mmRestGlobals;

    // world interaction
    const World* world = nullptr;
    bool collide = false;                 // push the root out of world colliders and follow the ground (player / non-navmesh use)
    float stepHeight = 0.35f, bodyRadius = 0.3f, vy = 0.f; bool grounded = true;
    float groundHeightUnder = 0.f;
    glm::vec3 velocity{0}; bool sprinting = false;
    glm::vec3 velocityRequest{0};         // convenience for steering code: the desired velocity to pass to update()

    // procedural layers (humanoid VRM)
    FootIKSettings ik; FootIKState ikState;
    HandIKSettings hands; HandIKState handState;
    LookAtSettings look; LookAtState lookState; glm::vec3 lookTarget{0}; bool hasLookTarget = false;
    SpringSettings spring; SpringState springState;
    // per-entity spring tuning (ImGui sliders / CLI). Resolved into a per-frame copy by sk::applyTuning(); the model's
    // own SpringSettings are never mutated (a previous version did `spring.stiffnessScale *= ...` every frame).
    SpringTuning springTuning;
    // expressions: targets are set by gameplay (setExpression), blink / lookAt-expressions are added automatically
    bool autoBlink = true;
    BlinkClock blink;                     // mepachi blink curve + its own seeded RNG (see anim/BlinkCurve.h)
    std::vector<float> exprTarget, exprCur, exprFinal;   // per-expression weights: requested, smoothed, after autoregressive layers
    // Posture controls (Unity Animation Rigging analogue): spine/chest/neck/head pitch offsets, lean/hunch correction
    PostureControl posture;
    void setExpression(const std::string& n, float w) { if (!model) return; int i = model->expression(n); if (i >= 0 && i < (int)exprTarget.size()) exprTarget[i] = w; }
    float expressionWeight(const std::string& n) const { int i = model ? model->expression(n) : -1; return i >= 0 && i < (int)exprFinal.size() ? exprFinal[i] : 0.f; }
    float time = 0.f;

    void init(std::shared_ptr<Model> m);
    void update(float dt, const glm::vec3& desiredVelocity);
    glm::mat4 transform() const;
    void collect(FrameInput& fi) const;                       // appends draws + skinning palette + morph weights
    void debugSkeleton(std::vector<LineVertex>& lines, uint32_t color) const;

private:
    void retargetFromMM();
    void updateExpressions(float dt);
    glm::vec3 lastPos_{0}; bool havePos_ = false;
};

struct OrbitCamera {
    glm::vec3 target{0, 1.0f, 0};
    float yaw = 0.f, pitch = 0.22f, distance = 3.4f;          // yaw: direction the camera LOOKS (0 = +Z)
    float fov = glm::radians(38.f), zNear = 0.05f, zFar = 120.f;
    float usedDistance = -1.f;                                 // distance after collision (negative: use `distance`)
    glm::vec3 position() const;
    glm::vec3 forwardFlat() const { return glm::vec3(std::sin(yaw), 0, std::cos(yaw)); }
    glm::vec3 rightFlat() const { return glm::vec3(-std::cos(yaw), 0, std::sin(yaw)); }
    glm::mat4 view() const;
};

// Builds a one-node model from a procedural mesh.
std::shared_ptr<Model> makePrimitiveModel(Renderer& r, const std::string& name, const MeshData& mesh, Material* material);

}  // namespace sk
