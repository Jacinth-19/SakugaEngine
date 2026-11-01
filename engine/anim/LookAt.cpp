#include "anim/LookAt.h"
#include "anim/IKCommon.h"
#include <algorithm>
#include <cmath>

namespace sk {

bool solveLookAt(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 glm::vec3 targetWorld, bool hasTarget, float dt, const LookAtSettings& s, LookAtState& st) {
    int head = rig.bone(HumanBone::Head), neck = rig.bone(HumanBone::Neck);
    int chest = rig.bone(HumanBone::UpperChest) >= 0 ? rig.bone(HumanBone::UpperChest) : rig.bone(HumanBone::Chest);
    if (head < 0 || dt <= 0.f) return false;
    st.clock += dt;
    float wTarget = (s.enabled && hasTarget) ? s.weight : 0.f;
    st.activeWeight += (wTarget - st.activeWeight) * expBlend(dt, 0.2f);

    const glm::mat4 Tinv = glm::inverse(T);
    const glm::mat3 Tr = glm::mat3(Tinv);
    // body frame: yaw of the spine (the head turns relative to the torso)
    int body = rig.bone(HumanBone::Spine) >= 0 ? rig.bone(HumanBone::Spine) : rig.bone(HumanBone::Hips);
    glm::quat bodyDelta = rotOf(G[body]) * glm::inverse(rotOf(restG[body]));
    glm::vec3 bf = bodyDelta * glm::vec3(0, 0, 1); float bodyYaw = std::atan2(bf.x, bf.z);
    // eye position (model space) and direction to the target
    glm::vec3 eyeM = glm::vec3(G[head] * glm::vec4(0.f, 0.07f, 0.08f, 1.f));
    int eL = rig.eyeL, eR = rig.eyeR;
    if (eL >= 0 && eR >= 0) eyeM = 0.5f * (glm::vec3(G[eL][3]) + glm::vec3(G[eR][3]));
    glm::vec3 d = glm::vec3(Tinv * glm::vec4(targetWorld, 1.f)) - eyeM;
    float tYaw = 0.f, tPitch = 0.f;
    if (hasTarget && glm::length(d) > 0.05f) {
        d = glm::normalize(d);
        tYaw = wrapPi(std::atan2(d.x, d.z) - bodyYaw); tPitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        if (std::fabs(tYaw) > 2.0f) { tYaw = 0.f; tPitch = 0.f; }               // target behind: look ahead instead of wringing the neck
    }
    // saccades: tiny random eye jumps
    if (s.saccades && st.clock >= st.nextSaccade) {
        auto rnd = [&](float a) { static unsigned x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return ((x & 0xFFFF) / 65535.f * 2.f - 1.f) * a; };
        st.saccadeYaw = rnd(0.045f); st.saccadePitch = rnd(0.03f);
        st.nextSaccade = st.clock + 0.4f + (rnd(1.f) * 0.5f + 0.5f) * 2.2f;
    }
    // clamp & smooth: the head takes part of the angle, the eyes take the rest
    float headYawT = std::clamp(tYaw, -s.yawLimit, s.yawLimit) * 0.78f, headPitchT = std::clamp(tPitch, -s.pitchDown, s.pitchUp) * 0.7f;
    headYawT *= st.activeWeight; headPitchT *= st.activeWeight;
    float kh = expBlend(dt, s.headHalflife), ke = expBlend(dt, s.eyeHalflife);
    st.yaw += (headYawT - st.yaw) * kh; st.pitch += (headPitchT - st.pitch) * kh;
    float eyeYawT = std::clamp(tYaw - st.yaw, -s.eyeYawLimit, s.eyeYawLimit) * st.activeWeight + st.saccadeYaw;
    float eyePitchT = std::clamp(tPitch - st.pitch, -s.eyePitchLimit, s.eyePitchLimit) * st.activeWeight + st.saccadePitch;
    st.eyeYaw += (eyeYawT - st.eyeYaw) * ke; st.eyePitch += (eyePitchT - st.eyePitch) * ke;
    st.exprLook = glm::vec2(std::clamp(st.eyeYaw / s.eyeYawLimit, -1.f, 1.f), std::clamp(st.eyePitch / s.eyePitchLimit, -1.f, 1.f));
    if (std::fabs(st.yaw) < 1e-4f && std::fabs(st.pitch) < 1e-4f && st.activeWeight < 1e-3f && eL < 0) return false;

    bool changed = false;
    // desired head forward in model space
    float cp = std::cos(st.pitch);
    glm::vec3 desired = glm::vec3(std::sin(bodyYaw + st.yaw) * cp, std::sin(st.pitch), std::cos(bodyYaw + st.yaw) * cp);
    glm::vec3 fwdLocal = glm::inverse(rotOf(restG[head])) * glm::vec3(0, 0, 1);
    auto applyWorldRot = [&](int node, glm::quat q) {
        glm::quat cur = rotOf(G[node]); int par = rig.nodes[node].parent;
        glm::quat Rp = par >= 0 ? rotOf(G[par]) : glm::quat(1, 0, 0, 0);
        pose.r[node] = glm::normalize(glm::inverse(Rp) * (q * cur));
        rig.updateSubtree(node, pose, G);
    };
    struct Part { int node; float share; } parts[3] = {{chest, s.chestShare}, {neck, s.neckShare}, {head, s.headShare}};
    float remaining = s.chestShare + s.neckShare + s.headShare;
    for (auto& p : parts) {
        if (p.node < 0) { remaining -= p.share; continue; }
        glm::vec3 cur = rotOf(G[head]) * fwdLocal;
        glm::quat Q = fromTo(cur, desired);
        float f = remaining > 1e-4f ? p.share / remaining : 1.f;
        glm::quat q = glm::slerp(glm::quat(1, 0, 0, 0), Q, std::clamp(f, 0.f, 1.f));
        if (glm::dot(q, glm::quat(1, 0, 0, 0)) < 0) q = -q;
        applyWorldRot(p.node, q);
        remaining -= p.share; changed = true;
    }
    // eyes (bones): rotate relative to the (now final) head
    if (eL >= 0 || eR >= 0) {
        glm::quat Dh = rotOf(G[head]) * glm::inverse(rotOf(restG[head]));
        glm::quat look = yawQ(st.eyeYaw) * glm::angleAxis(-st.eyePitch, glm::vec3(1, 0, 0));
        for (int e : {eL, eR}) {
            if (e < 0) continue;
            glm::quat world = Dh * look * rotOf(restG[e]);
            int par = rig.nodes[e].parent;
            pose.r[e] = glm::normalize(glm::inverse(par >= 0 ? rotOf(G[par]) : glm::quat(1, 0, 0, 0)) * world);
            rig.updateSubtree(e, pose, G); changed = true;
        }
    }
    return changed;
}

}  // namespace sk
