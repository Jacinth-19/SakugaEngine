#include "anim/SpringBone.h"
#include "anim/IKCommon.h"
#include <algorithm>
#include <cmath>

namespace sk {

void stepSpringBones(const Model& model, const std::vector<glm::mat4>& restG, Pose& pose, std::vector<glm::mat4>& G,
                     const glm::mat4& T, float dt, const SpringSettings& set, SpringState& st) {
    const SpringData& sd = model.springs; const Rig& rig = model.rig;
    if (sd.chains.empty() || !set.enabled || dt <= 0.f) return;
    const float scale = glm::length(glm::vec3(T[0]));
    if (st.chains.size() != sd.chains.size()) { st.chains.assign(sd.chains.size(), {}); for (size_t c = 0; c < sd.chains.size(); ++c) st.chains[c].resize(sd.chains[c].joints.size()); st.inited = false; }
    glm::vec3 rootPos = glm::vec3(T * G[std::max(0, rig.bone(HumanBone::Hips))][3]);
    if (st.inited && glm::length(rootPos - st.lastPos) > 2.5f) st.reset();        // teleport
    st.lastPos = rootPos; st.inited = true;

    // static data per joint (computed once)
    for (size_t c = 0; c < sd.chains.size(); ++c) for (size_t k = 0; k < sd.chains[c].joints.size(); ++k) {
        auto& j = st.chains[c][k]; const SpringJoint& sj = sd.chains[c].joints[k];
        if (!j.init) {
            glm::vec3 localTail;
            if (sj.child >= 0) localTail = glm::vec3(glm::inverse(restG[sj.node]) * restG[sj.child][3]);
            else { glm::vec3 d = rig.nodes[sj.node].t; localTail = glm::length(d) > 1e-5f ? glm::normalize(d) * 0.07f : glm::vec3(0, -0.07f, 0); }
            j.boneLength = std::max(glm::length(localTail), 1e-4f); j.boneAxisLocal = localTail / j.boneLength;
            j.initRot = rig.nodes[sj.node].r;
            glm::vec3 tailW = sj.child >= 0 ? glm::vec3(T * G[sj.child][3]) : glm::vec3(T * G[sj.node] * glm::vec4(localTail, 1.f));
            j.cur = j.prev = tailW; j.init = true;
        }
    }
    int steps = std::clamp((int)std::ceil(dt * 60.f), 1, 4); float h = dt / steps;
    glm::vec3 wind = set.windStrength > 0 ? glm::normalize(set.windDir) * set.windStrength * (0.6f + 0.4f * std::sin(set.time * 1.7f) + 0.25f * std::sin(set.time * 4.1f)) : glm::vec3(0);

    for (int step = 0; step < steps; ++step) {
        for (size_t c = 0; c < sd.chains.size(); ++c) {
            const SpringChain& ch = sd.chains[c];
            for (size_t k = 0; k < ch.joints.size(); ++k) {
                const SpringJoint& sj = ch.joints[k]; auto& j = st.chains[c][k];
                int node = sj.node, par = rig.nodes[node].parent;
                if (node < 0 || par < 0) continue;
                glm::mat4 PW = T * G[par];
                glm::vec3 head = glm::vec3(T * G[node][3]);
                glm::quat parentRot = rotOf(PW);
                glm::quat restWorld = parentRot * j.initRot;
                float len = j.boneLength * scale;
                glm::vec3 restDir = restWorld * j.boneAxisLocal;
                // verlet integration
                glm::vec3 inertia = (j.cur - j.prev) * (1.f - std::clamp(sj.drag * set.dragScale, 0.f, 1.f));
                glm::vec3 stiff = restDir * (sj.stiffness * set.stiffnessScale * h) * scale;
                glm::vec3 ext = (glm::normalize(sj.gravityDir) * sj.gravityPower * set.gravityScale + wind) * h * scale;
                glm::vec3 next = j.cur + inertia + stiff + ext;
                next = head + glm::normalize(next - head + glm::vec3(1e-7f)) * len;
                // colliders (sphere / capsule) of every group attached to this chain
                for (int g : ch.colliderGroups) {
                    if (g < 0 || g >= (int)sd.groups.size()) continue;
                    for (int ci : sd.groups[g]) {
                        if (ci < 0 || ci >= (int)sd.colliders.size()) continue;
                        const SpringCollider& col = sd.colliders[ci]; if (col.node < 0) continue;
                        glm::mat4 CW = T * G[col.node];
                        glm::vec3 a = glm::vec3(CW * glm::vec4(col.offset, 1.f)), closest = a;
                        if (col.capsule) {
                            glm::vec3 b = glm::vec3(CW * glm::vec4(col.tail, 1.f)), ab = b - a; float l2 = glm::dot(ab, ab);
                            float t = l2 > 1e-10f ? std::clamp(glm::dot(next - a, ab) / l2, 0.f, 1.f) : 0.f; closest = a + ab * t;
                        }
                        float r = (col.radius * scale + entity->springTuningColliderRadius) * scale + sj.hitRadius * scale;
                        glm::vec3 d = next - closest; float dist = glm::length(d);
                        if (dist < r) {
                            glm::vec3 n = dist > 1e-6f ? d / dist : glm::vec3(0, 0, 1);
                            next = closest + n * r;
                            next = head + glm::normalize(next - head) * len;
                        }
                    }
                }
                // keep hair above the ground: simple floor (feet level of the entity)
                j.prev = j.cur; j.cur = next;
                // rotate the joint so its bone axis points at the simulated tail
                glm::quat world = fromTo(restDir, next - head) * restWorld;
                glm::quat local = glm::normalize(glm::inverse(parentRot) * world);
                pose.r[node] = local;
                rig.updateSubtree(node, pose, G);
            }
        }
    }
}

}  // namespace sk
