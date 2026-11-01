#include <cstdio>
#include "scene/Scene.h"
#include "anim/IKCommon.h"
#include "world/World.h"
#include <array>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace sk {

void Entity::init(std::shared_ptr<Model> m) {
    model = std::move(m);
    pose = model->rig.restPose();
    model->rig.globals(pose, restGlobals);
    globals = restGlobals;
    exprTarget.assign(model->expressions.size(), 0.f); exprCur = exprTarget; exprFinal = exprTarget;
    blink = BlinkClock{};                                  // deterministic default timing
    blink.rng = Rng(hashSeed(name.c_str()));                // same-name entities blink in the same rhythm, others differ
    blink.timer = blink.rng.range(0.8f, 2.8f);             // first blink shortly after spawn
}

glm::mat4 Entity::transform() const {
    if (useCustomTransform) return customTransform;
    return glm::translate(glm::mat4(1.f), pos) * glm::rotate(glm::mat4(1.f), yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.f), glm::vec3(scale));
}

// Retarget the pose that the matcher wrote on the hero rig onto this model. World-space rotation deltas from the rest pose are transferred bone by bone;
// limb bones additionally get an aim correction so A-pose characters follow T-pose motion without folded arms.
void Entity::retargetFromMM() {
    const Rig& hr = mmModel->rig; const Rig& nr = model->rig;
    hr.globals(mmPose, mmGlobals);
    std::vector<glm::quat> Rw(nr.nodes.size());
    std::vector<char> mapped(nr.nodes.size(), 0);
    std::array<glm::quat, kHumanBoneCount> corr; corr.fill(glm::quat(1, 0, 0, 0));
    std::array<glm::quat, kHumanBoneCount> want; std::array<bool, kHumanBoneCount> have; have.fill(false);
    auto Dh = [&](int b) { return rotOf(mmGlobals[hr.human[b]]) * glm::inverse(rotOf(mmRestGlobals[hr.human[b]])); };
    // child bone used as the aim target of limb bones
    const int childOf[kHumanBoneCount] = {
        /*Hips*/ -1, -1, -1, -1, -1, -1,
        /*LShoulder*/ (int)HumanBone::LUpperArm, (int)HumanBone::LLowerArm, (int)HumanBone::LHand, -1,
        /*RShoulder*/ (int)HumanBone::RUpperArm, (int)HumanBone::RLowerArm, (int)HumanBone::RHand, -1,
        /*LUpperLeg*/ (int)HumanBone::LLowerLeg, (int)HumanBone::LFoot, (int)HumanBone::LToes, -1,
        /*RUpperLeg*/ (int)HumanBone::RLowerLeg, (int)HumanBone::RFoot, (int)HumanBone::RToes, -1};
    for (int b = 0; b < kHumanBoneCount; ++b) {
        int hn = hr.human[b], nn = nr.human[b];
        if (hn < 0 || nn < 0) continue;
        glm::quat d = Dh(b);
        glm::quat w = d * rotOf(restGlobals[nn]);
        int c = childOf[b];
        if (c >= 0 && hr.human[c] >= 0 && nr.human[c] >= 0) {
            glm::vec3 hd = glm::vec3(mmGlobals[hr.human[c]][3]) - glm::vec3(mmGlobals[hn][3]);
            glm::vec3 nd = glm::vec3(restGlobals[nr.human[c]][3]) - glm::vec3(restGlobals[nn][3]);
            if (glm::length(hd) > 1e-4f && glm::length(nd) > 1e-4f) { glm::quat sw = fromTo(d * glm::normalize(nd), glm::normalize(hd)); corr[b] = sw; w = sw * w; }
        } else {
            // end bones (hand): follow the correction of the parent limb bone so the hand keeps its relation to the forearm
            int pb = b == (int)HumanBone::LHand ? (int)HumanBone::LLowerArm : b == (int)HumanBone::RHand ? (int)HumanBone::RLowerArm : -1;
            if (pb >= 0) w = corr[pb] * w;
        }
        want[b] = w; have[b] = true; mapped[nn] = 1;
    }
    std::vector<int> bone(nr.nodes.size(), -1);
    for (int b = 0; b < kHumanBoneCount; ++b) if (nr.human[b] >= 0 && have[b]) bone[nr.human[b]] = b;
    for (int n : nr.order) {
        int p = nr.nodes[n].parent;
        glm::quat Rp = p >= 0 ? Rw[p] : glm::quat(1, 0, 0, 0);
        if (bone[n] >= 0) { Rw[n] = want[bone[n]]; pose.r[n] = glm::normalize(glm::inverse(Rp) * Rw[n]); }
        else Rw[n] = Rp * pose.r[n];
    }
    // hips position: the hero's model-space hips scaled by the leg length ratio
    int hh = hr.human[(int)HumanBone::Hips], nh = nr.human[(int)HumanBone::Hips];
    if (hh >= 0 && nh >= 0) {
        float ratio = restGlobals[nh][3].y / std::max(mmRestGlobals[hh][3].y, 1e-3f);
        glm::vec3 pm = glm::vec3(mmGlobals[hh][3]) * ratio;
        int p = nr.nodes[nh].parent;
        glm::mat4 Gp(1.f);
        if (p >= 0) { std::vector<glm::mat4> g; nr.globals(pose, g); Gp = g[p]; }
        pose.t[nh] = glm::vec3(glm::inverse(Gp) * glm::vec4(pm, 1.f));
    }
}

void Entity::updateExpressions(float dt) {
    if (!model || model->expressions.empty()) return;
    size_t n = model->expressions.size();
    if (exprCur.size() != n) { exprTarget.assign(n, 0.f); exprCur = exprTarget; exprFinal = exprTarget; }
    float k = 1.f - std::exp(-dt / 0.06f);
    for (size_t i = 0; i < n; ++i) exprCur[i] += (exprTarget[i] - exprCur[i]) * k;
    exprFinal = exprCur;
    // AAA-anime blink (mepachi): the curve lives in anim/BlinkCurve.h (tested by tests/blink_curve_test.cpp).
    // A look/head turn triggers an extra blink.
    blink.enabled = autoBlink;
    bool lookTriggered = look.enabled && (std::abs(lookState.exprLook.x) > 0.6f || std::abs(lookState.exprLook.y) > 0.6f);
    float blinkWeight = blink.step(dt, lookTriggered);
    int blinkExpr = model->expression("blink");
    if (blinkExpr >= 0) exprFinal[blinkExpr] = std::max(exprFinal[blinkExpr], blinkWeight * (1.f - 0.7f * std::max(expressionWeight("happy"), 0.f)));
    // eye-bone-less rigs: drive the look expressions from the gaze
    if (model->rig.eyeL < 0 && look.enabled) {
        glm::vec2 e = lookState.exprLook;
        auto put = [&](const char* nm, float w) { int i = model->expression(nm); if (i >= 0) exprFinal[i] = std::max(exprFinal[i], std::clamp(w, 0.f, 1.f)); };
        put("lookLeft", e.x); put("lookRight", -e.x); put("lookUp", e.y); put("lookDown", -e.y);
    }
}

void Entity::update(float dt, const glm::vec3& desiredVelocity) {
    if (!model) return;
    const Rig& rig = model->rig;
    time += dt;
    glm::vec3 before = pos;
    if (clip >= 0 && clip < (int)model->clips.size()) {
        clipTime += dt * clipSpeed;
        model->clips[clip].sample(clipTime, loop, pose);
    }
    if (mm) {
        if (mmModel && mmModel.get() != model.get()) {
            if (mmPose.r.size() != mmModel->rig.nodes.size()) { mmPose = mmModel->rig.restPose(); mmModel->rig.globals(mmPose, mmRestGlobals); }
            mm->update(dt, desiredVelocity, pos, yaw, mmPose);
            retargetFromMM();
        } else mm->update(dt, desiredVelocity, pos, yaw, pose);
    }
    // world: collision against boxes + ground following
    if (world && collide) {
        for (int it = 0; it < 2; ++it)
            for (float hgt : {0.55f, 1.0f, 1.5f}) {
                glm::vec3 c = pos + glm::vec3(0, hgt * scale, 0);
                glm::vec3 push = world->depenetrate(c, bodyRadius * scale);
                pos.x += push.x; pos.z += push.z;
            }
        float gh; if (world->groundHeight(pos + glm::vec3(0, stepHeight * scale, 0), 4.f, gh)) {
            if (gh >= pos.y - 0.002f) { pos.y += std::min(gh - pos.y, 7.f * dt + 0.05f); vy = 0.f; grounded = true; }
            else { vy -= 18.f * dt; pos.y += std::max(vy * dt, gh - pos.y); if (pos.y <= gh + 1e-3f) { pos.y = gh; vy = 0.f; } grounded = pos.y - gh < 0.05f; }
        }
    }
    {   float gh = 0.f; if (world && !world->groundHeight(pos + glm::vec3(0, 0.5f, 0), 3.f, gh)) gh = pos.y; groundHeightUnder = world ? gh : pos.y; }
    if (havePos_ && dt > 0) velocity = glm::mix(velocity, (pos - lastPos_) / dt, 0.4f); lastPos_ = pos; havePos_ = true;
    (void)before;
    // --- Posture correction (Unity Animation Rigging analogue) ---------------------------------------------
    // Clamping and the chain distribution live in anim/Posture.h (tested by tests/posture_clamp_test.cpp); this only
    // turns the (already clamped) pitch offsets into quaternions. Skipped entirely when the layer is a no-op.
    if (postureActive(posture)) {
        const PostureControl pc = clampPosture(posture);
        const int boneOf[4] = {rig.human[(int)HumanBone::Spine], rig.human[(int)HumanBone::Chest],
                               rig.human[(int)HumanBone::Neck], rig.human[(int)HumanBone::Head]};
        const float pitchDeg[4] = {pc.spine, pc.chest, pc.neck, pc.head};
        // 1. explicit per-bone pitch offsets (blended in by the layer weight)
        for (int i = 0; i < 4; ++i) {
            int b = boneOf[i];
            if (b < 0 || pitchDeg[i] == 0.f) continue;
            glm::quat q = glm::angleAxis(pitchDeg[i] * 0.017453293f, glm::vec3(1, 0, 0));
            pose.r[b] = glm::normalize(glm::slerp(pose.r[b], q * pose.r[b], pc.weight));
        }
        // 2. hunch / lean correction: a counter-rotation distributed over spine, chest and neck
        if (pc.hunchCorrection > 0.f) {
            float rad[3]; hunchRadiansPerBone(pc, rad);
            for (int i = 0; i < 3; ++i) {
                int b = boneOf[i];
                if (b < 0 || rad[i] == 0.f) continue;
                glm::quat corr = glm::angleAxis(rad[i], glm::vec3(1, 0, 0));
                pose.r[b] = glm::normalize(corr * pose.r[b]);
            }
        }
    }
    rig.applyConstraints(pose);
    rig.globals(pose, globals);
    glm::mat4 T = transform();
    bool human = rig.isHumanoid() && model->isVRM;
    if (human) {
        // order: feet (+pelvis) -> hands -> head/eyes -> expressions -> springs
        if (ik.enabled) solveFootIK(rig, pose, globals, restGlobals, T, dt, ik, ikState, world, groundHeightUnder);
        float speed = glm::length(glm::vec2(velocity.x, velocity.z));
        if (hands.enabled && world) solveHandIK(rig, pose, globals, restGlobals, T, dt, std::min(speed / 4.f, 1.f), sprinting, hands, handState, world);
        if (look.enabled) solveLookAt(rig, pose, globals, restGlobals, T, lookTarget, hasLookTarget, dt, look, lookState);
        updateExpressions(dt);
        spring.time = time;
        // Spring tuning: resolved into a fresh copy every frame (applyTuning is pure). The old code multiplied the
        // entity's persistent `spring` scales in place, so any slider != 1 compounded exponentially per frame.
        if (spring.enabled) {
            const SpringSettings tuned = applyTuning(spring, springTuning);
            stepSpringBones(*model, restGlobals, pose, globals, T, dt, tuned, springState);
        }
    } else if (model->isVRM) updateExpressions(dt);
}

void Entity::collect(FrameInput& fi) const {
    if (!visible || !model) return;
    glm::mat4 T = transform();
    {   // head-bone driven frame for the face SDF
        int h = model->rig.bone(HumanBone::Head);
        if (h >= 0 && !globals.empty() && !restGlobals.empty()) {
            glm::quat d = rotOf(globals[h]) * glm::inverse(rotOf(restGlobals[h]));
            fi.faceForward = d * glm::vec3(0, 0, 1); fi.faceRight = d * glm::vec3(1, 0, 0);
        }
    }
    std::vector<int> skinBase(model->rig.skins.size(), -1);
    for (const Primitive& p : model->prims) {
        DrawItem d; d.mesh = p.mesh; d.material = model->materials[std::min<size_t>(p.material, model->materials.size() - 1)];
        if (p.skin >= 0) {
            if (skinBase[p.skin] < 0) {
                skinBase[p.skin] = (int)fi.bones.size();
                const Skin& s = model->rig.skins[p.skin];
                for (size_t j = 0; j < s.joints.size(); ++j) fi.bones.push_back(globals[s.joints[j]] * s.inverseBind[j]);
            }
            d.skinned = true; d.boneOffset = (uint32_t)skinBase[p.skin]; d.model = T;
        } else {
            // vertices were baked with the node's rest transform; apply only the animated delta
            d.model = T * globals[p.node] * glm::inverse(restGlobals[p.node]);
        }
        d.faceFrame = d.material->faceFrame;
        // morph targets: sum the VRM expression binds that refer to this mesh's slots
        if (p.mesh && p.mesh->morphSlots > 0 && p.meshIndex >= 0 && !exprFinal.empty()) {
            std::vector<float> w(p.mesh->morphSlots, 0.f); bool any = false;
            for (size_t e = 0; e < model->expressions.size() && e < exprFinal.size(); ++e) {
                float ew = exprFinal[e]; if (ew <= 1e-3f) continue;
                const Expression& ex = model->expressions[e]; if (ex.binary) ew = ew > 0.5f ? 1.f : 0.f;
                for (const MorphBind& b : ex.binds) {
                    if (b.mesh != p.meshIndex) continue;
                    for (uint32_t k = 0; k < p.mesh->morphSlots && k < p.mesh->morphTargetIndex.size(); ++k)
                        if (p.mesh->morphTargetIndex[k] == b.index) { w[k] += ew * b.weight; any = true; }
                }
            }
            if (any) {
                d.morphWeightOffset = (uint32_t)fi.morphWeights.size(); d.morphCount = p.mesh->morphSlots;
                for (float x : w) fi.morphWeights.push_back(std::clamp(x, 0.f, 1.f));
            }
        }
        fi.draws.push_back(d);
    }
}

void Entity::debugSkeleton(std::vector<LineVertex>& lines, uint32_t color) const {
    if (!model) return;
    glm::mat4 T = transform();
    const Rig& rig = model->rig;
    std::vector<char> isJoint(rig.nodes.size(), 0);
    for (auto& s : rig.skins) for (int j : s.joints) isJoint[j] = 1;
    for (int b = 0; b < kHumanBoneCount; ++b) if (rig.human[b] >= 0) isJoint[rig.human[b]] = 1;
    std::vector<int> parent(rig.nodes.size(), -1);
    for (size_t i = 0; i < rig.nodes.size(); ++i) parent[i] = rig.nodes[i].parent;
    appendSkeletonSegments(lines, parent, globals, isJoint, T, color);
}

glm::vec3 OrbitCamera::position() const {
    glm::vec3 f(std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch));   // look direction (pitch>0 looks down)
    return target - f * (usedDistance >= 0.f ? usedDistance : distance);
}
glm::mat4 OrbitCamera::view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }

std::shared_ptr<Model> makePrimitiveModel(Renderer& r, const std::string& name, const MeshData& mesh, Material* material) {
    auto m = std::make_shared<Model>();
    m->name = name;
    Node n; n.name = name; m->rig.nodes.push_back(n); m->rig.buildOrder();
    Primitive p; p.mesh = r.createMesh(mesh); p.node = 0; p.material = 0; p.name = name;
    m->prims.push_back(p); m->materials.push_back(material);
    m->boundsMin = p.mesh->boundsMin; m->boundsMax = p.mesh->boundsMax; m->height = m->boundsMax.y - m->boundsMin.y;
    return m;
}
}  // namespace sk
