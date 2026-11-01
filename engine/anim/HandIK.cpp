#include "anim/HandIK.h"
#include "anim/IKCommon.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace sk {

bool solveHandIK(const Rig& rig, Pose& pose, std::vector<glm::mat4>& G, const std::vector<glm::mat4>& restG, const glm::mat4& T,
                 float dt, float speedNorm, bool sprinting, const HandIKSettings& s, HandIKState& st, const World* world) {
    if (!rig.isHumanoid() || dt <= 0.f) return false;
    const HumanBone arms[2][3] = {{HumanBone::LUpperArm, HumanBone::LLowerArm, HumanBone::LHand}, {HumanBone::RUpperArm, HumanBone::RLowerArm, HumanBone::RHand}};
    const float scale = glm::length(glm::vec3(T[0]));
    const glm::mat4 Tinv = glm::inverse(T);
    const glm::mat3 R3 = glm::mat3(T) / scale;
    bool changed = false;
    float suppress = 1.f - (sprinting ? s.sprintFade : 0.f) * std::clamp(speedNorm, 0.f, 1.f);
    for (int i = 0; i < 2; ++i) {
        int nu = rig.bone(arms[i][0]), nl = rig.bone(arms[i][1]), nh = rig.bone(arms[i][2]);
        if (nu < 0 || nl < 0 || nh < 0) continue;
        auto& H = st.hand[i];
        float side = i == 0 ? 1.f : -1.f;                  // +X = character's left (model space)
        glm::vec3 A = glm::vec3(T * G[nu][3]), B = glm::vec3(T * G[nl][3]), C = glm::vec3(T * G[nh][3]);
        float armLen = (glm::length(B - A) + glm::length(C - B));
        float reach = armLen * s.reachScale;
        // probe the surroundings (lateral, forward-lateral, forward) from the shoulder
        bool found = false; float bestD = 1e9f; glm::vec3 bestP{0}, bestN{0, 0, 1};
        if (world && s.enabled && s.weight > 0.f) {
            const glm::vec3 dirs[3] = {{side, 0, 0}, {side * 0.6f, 0, 0.8f}, {0, 0, 1}};
            for (auto& dm : dirs) {
                glm::vec3 d = glm::normalize(R3 * dm);
                RayHit h = world->raycast(A, d, reach * 1.15f);
                if (!h.hit) continue;
                if (h.collider >= 0 && !world->colliders()[h.collider].climbable) continue;
                if (std::fabs(h.normal.y) > s.maxWallSlope) continue;
                if (h.t < bestD) { bestD = h.t; bestP = h.point; bestN = h.normal; found = true; }
            }
        }
        float want = 0.f;
        if (found) want = 1.f - std::clamp((bestD - 0.92f * reach) / (0.18f * reach), 0.f, 1.f);
        want *= suppress * s.weight;
        if (found) {
            glm::vec3 tgt = bestP + bestN * s.palmOffset;
            if (!H.had) { H.target = C; H.normal = bestN; }
            float k = 1.f - std::exp(-14.f * dt);
            H.target += (tgt - H.target) * k; H.normal = glm::normalize(H.normal + (bestN - H.normal) * k);
            H.had = true;
        }
        float rate = want > H.w ? 9.f : 6.f;
        H.w += (want - H.w) * (1.f - std::exp(-rate * dt));
        H.touching = H.w > 0.02f;
        if (!H.touching) { if (!found && H.w < 0.005f) H.had = false; continue; }
        // target blended from the animated hand
        glm::vec3 tw = glm::mix(C, H.target, H.w);
        glm::vec3 Tm = glm::vec3(Tinv * glm::vec4(tw, 1.f));
        glm::vec3 pole = glm::normalize(glm::vec3(side * 0.5f, -0.7f, -0.4f));
        glm::quat Rh0 = rotOf(G[nh]);
        if (!twoBoneIK(rig, pose, G, nu, nl, nh, Tm, pole)) continue;
        rig.updateSubtree(nu, pose, G);
        // palm orientation: rest palm (0,-1,0), rest fingers (+X for the left hand) -> palm = -wall normal, fingers along the wall
        glm::vec3 n = glm::normalize(glm::transpose(R3) * H.normal);                      // wall normal in model space
        glm::vec3 up(0, 1, 0), fwd(0, 0, 1);
        glm::vec3 upP = up - n * glm::dot(up, n), fwdP = fwd - n * glm::dot(fwd, n);
        glm::vec3 tdir = (glm::length(upP) > 1e-3f ? glm::normalize(upP) * 0.7f : glm::vec3(0)) + (glm::length(fwdP) > 1e-3f ? glm::normalize(fwdP) * 0.5f : glm::vec3(0));
        if (glm::length(tdir) < 1e-3f) tdir = glm::cross(n, glm::vec3(1, 0, 0));
        tdir = glm::normalize(tdir - n * glm::dot(tdir, n));
        glm::vec3 palm = -n, fing = tdir;
        glm::mat3 want3(fing, palm, glm::cross(fing, palm));
        glm::mat3 rest3(glm::vec3(side, 0, 0), glm::vec3(0, -1, 0), glm::cross(glm::vec3(side, 0, 0), glm::vec3(0, -1, 0)));
        glm::quat Q = glm::quat_cast(want3 * glm::transpose(rest3));
        glm::quat target = glm::normalize(Q * rotOf(restG[nh]));
        glm::quat finalR = glm::slerp(Rh0, target, std::clamp(H.w * 1.1f, 0.f, 1.f));
        pose.r[nh] = glm::normalize(glm::inverse(rotOf(G[nl])) * finalR);
        rig.updateSubtree(nh, pose, G);
        changed = true;
    }
    return changed;
}

}  // namespace sk
