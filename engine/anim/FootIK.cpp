#include "anim/FootIK.h"
#include "anim/IKCommon.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace sk {

bool solveFootIK(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 float dt, const FootIKSettings& s, FootIKState& st, const World* world, float rootGroundY) {
    if (!s.enabled || s.weight <= 0.f || dt <= 1e-5f || !rig.isHumanoid()) return false;
    const HumanBone legs[2][3] = {{HumanBone::LUpperLeg, HumanBone::LLowerLeg, HumanBone::LFoot}, {HumanBone::RUpperLeg, HumanBone::RLowerLeg, HumanBone::RFoot}};
    const HumanBone toesB[2] = {HumanBone::LToes, HumanBone::RToes};
    const glm::mat4 Tinv = glm::inverse(T);
    const float scale = glm::length(glm::vec3(T[0]));
    if (!st.inited) {
        for (int i = 0; i < 2; ++i) {
            int f = rig.bone(legs[i][2]), t = rig.bone(toesB[i]);
            glm::vec3 fp = glm::vec3(restG[f][3]);
            st.ankleRest[i] = fp.y;
            // heel: straight below the ankle, slightly behind; toe: below the toe joint (ball of the foot), slightly ahead. The floor of the rest pose is y = 0.
            glm::vec3 heelRest(fp.x, 0.f, fp.z - 0.045f);
            st.heelLocal[i] = glm::vec3(glm::inverse(restG[f]) * glm::vec4(heelRest, 1.f));
            if (t >= 0) { glm::vec3 tp = glm::vec3(restG[t][3]); st.toeLocal[i] = glm::vec3(glm::inverse(restG[t]) * glm::vec4(glm::vec3(tp.x, 0.f, tp.z + 0.035f), 1.f)); }
            else st.toeLocal[i] = glm::vec3(glm::inverse(restG[f]) * glm::vec4(glm::vec3(fp.x, 0.f, fp.z + 0.14f), 1.f));
            st.foot[i].lockAnkleAboveSole = fp.y;
        }
        st.inited = true;
    }
    auto groundAt = [&](glm::vec3 p, float& g, glm::vec3& n) {
        n = glm::vec3(0, 1, 0); g = s.groundY;
        if (!world) return;
        float h; glm::vec3 nn;
        if (world->groundHeight(glm::vec3(p.x, std::max(rootGroundY, p.y) + 0.55f, p.z), 2.5f, h, &nn)) { g = h; n = nn; }
    };

    // ------------------------------------------------------------ pelvis
    int hips = rig.bone(HumanBone::Hips);
    glm::vec3 P0[2]; float g0[2]; glm::vec3 n0[2];
    for (int i = 0; i < 2; ++i) { P0[i] = glm::vec3(T * G[rig.bone(legs[i][2])][3]); groundAt(P0[i], g0[i], n0[i]); }
    if (s.pelvis && hips >= 0) {
        float drop = std::min(0.f, std::min(g0[0], g0[1]) - rootGroundY);
        // hip-to-ankle overextension: if the animated hips are too high to plant a foot on the ground, lower the pelvis a little
        float over = 0.f;
        for (int i = 0; i < 2; ++i) {
            float l1 = glm::length(glm::vec3(G[rig.bone(legs[i][1])][3]) - glm::vec3(G[rig.bone(legs[i][0])][3])) * scale;
            float l2 = glm::length(glm::vec3(G[rig.bone(legs[i][2])][3]) - glm::vec3(G[rig.bone(legs[i][1])][3])) * scale;
            glm::vec3 A = glm::vec3(T * G[rig.bone(legs[i][0])][3]);
            glm::vec3 want(P0[i].x, g0[i] + st.ankleRest[i] * scale, P0[i].z);
            if (st.foot[i].w > 0.3f) over = std::max(over, glm::length(want - A) - (l1 + l2) * 0.985f);
        }
        st.pelvisTarget = std::clamp(drop - over, -s.maxPelvisDrop, 0.f);
        st.pelvisOffset += (st.pelvisTarget - st.pelvisOffset) * expBlend(dt, s.pelvisHalflife);
        if (std::fabs(st.pelvisOffset) > 1e-4f) {
            int par = rig.nodes[hips].parent;
            glm::mat4 Pm = par >= 0 ? G[par] : glm::mat4(1.f);
            glm::vec3 local = glm::vec3(glm::inverse(Pm) * glm::vec4(glm::vec3(Tinv * glm::vec4(glm::vec3(T * G[hips][3]) + glm::vec3(0, st.pelvisOffset, 0), 1.f)), 1.f));
            pose.t[hips] = local;
            rig.updateSubtree(hips, pose, G);
        }
    }

    // ------------------------------------------------------------ feet
    bool changed = st.pelvisOffset != 0.f;
    struct Solve { bool ok = false; glm::quat footWorld; };
    Solve sv[2];
    for (int i = 0; i < 2; ++i) {
        int nu = rig.bone(legs[i][0]), nl = rig.bone(legs[i][1]), nf = rig.bone(legs[i][2]), nt = rig.bone(toesB[i]);
        if (nu < 0 || nl < 0 || nf < 0) continue;
        auto& ft = st.foot[i];
        glm::vec3 P = glm::vec3(T * G[nf][3]);
        glm::vec3 heel = glm::vec3(T * G[nf] * glm::vec4(st.heelLocal[i], 1.f));
        glm::vec3 toe = nt >= 0 ? glm::vec3(T * G[nt] * glm::vec4(st.toeLocal[i], 1.f)) : glm::vec3(T * G[nf] * glm::vec4(st.toeLocal[i], 1.f));
        float soleY = std::min(heel.y, toe.y);
        float g; glm::vec3 gn; groundAt(P, g, gn);
        float speed = ft.hasPrev ? glm::length(P - ft.prev) / dt : 1e3f;
        ft.prev = P; ft.hasPrev = true;
        float h = soleY - g;
        st.soleHeight[i] = h; st.groundAtFoot[i] = g;
        if (!ft.locked) {
            if (h < s.contactHeight && speed < s.contactSpeed) { ft.locked = true; ft.lockPos = P; ft.lockAnkleAboveSole = std::max(P.y - soleY, 0.02f); }
        } else {
            glm::vec2 dxz(P.x - ft.lockPos.x, P.z - ft.lockPos.z);
            if (h > s.contactHeight * 1.5f || speed > s.releaseSpeed || glm::length(dxz) > s.releaseDistance) ft.locked = false;
        }
        float k = 1.f - std::exp(-(ft.locked ? s.blendIn : s.blendOut) * dt);
        ft.w += ((ft.locked ? 1.f : 0.f) - ft.w) * k;
        glm::vec3 freeT = P; if (soleY < g) freeT.y += g - soleY;           // never below the ground
        glm::vec3 lockT = ft.lockPos; { float gl; glm::vec3 nl_; groundAt(ft.lockPos, gl, nl_); lockT.y = gl + ft.lockAnkleAboveSole; }
        glm::vec3 target = glm::mix(freeT, lockT, ft.w * s.weight);
        target.y = std::max(target.y, g + ft.lockAnkleAboveSole * 0.25f);
        st.debugLift[i] = glm::length(target - P);
        glm::quat Rf = rotOf(G[nf]);
        glm::quat footW = Rf;
        if (s.alignFoot) footW = glm::slerp(Rf, fromTo(glm::vec3(0, 1, 0), glm::normalize(glm::mat3(Tinv) * gn)) * Rf, 0.85f * ft.w * s.weight);
        bool needFoot = glm::length(footW * glm::vec3(0, 1, 0) - Rf * glm::vec3(0, 1, 0)) > 1e-3f;
        if (st.debugLift[i] < 1e-4f && !needFoot) continue;
        glm::vec3 Tm = glm::vec3(Tinv * glm::vec4(target, 1.f));
        if (st.debugLift[i] >= 1e-4f) twoBoneIK(rig, pose, G, nu, nl, nf, Tm, glm::vec3(0));
        sv[i].ok = true; sv[i].footWorld = footW;
    }
    for (int i = 0; i < 2; ++i) if (sv[i].ok) {
        int nu = rig.bone(legs[i][0]), nl = rig.bone(legs[i][1]), nf = rig.bone(legs[i][2]);
        rig.updateSubtree(nu, pose, G);
        pose.r[nf] = glm::normalize(glm::inverse(rotOf(G[nl])) * sv[i].footWorld);
        rig.updateSubtree(nf, pose, G);
        changed = true;
    }
    return changed;
}

}  // namespace sk
