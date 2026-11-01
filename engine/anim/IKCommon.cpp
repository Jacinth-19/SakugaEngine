#include "anim/IKCommon.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace sk {

glm::quat rotOf(const glm::mat4& m) {
    glm::mat3 r(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])));
    return glm::normalize(glm::quat_cast(r));
}
glm::quat fromTo(glm::vec3 a, glm::vec3 b) {
    float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return glm::quat(1, 0, 0, 0);
    a /= la; b /= lb;
    float d = glm::dot(a, b);
    if (d < -0.9999f) { glm::vec3 ax = glm::cross(a, std::fabs(a.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)); return glm::angleAxis(glm::pi<float>(), glm::normalize(ax)); }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}
glm::quat yawQ(float a) { return glm::angleAxis(a, glm::vec3(0, 1, 0)); }
float wrapPi(float a) { while (a > glm::pi<float>()) a -= glm::two_pi<float>(); while (a < -glm::pi<float>()) a += glm::two_pi<float>(); return a; }

bool twoBoneIK(const Rig& rig, Pose& pose, const std::vector<glm::mat4>& G, int nu, int nl, int nf, glm::vec3 target, glm::vec3 pole, float* deficit) {
    if (nu < 0 || nl < 0 || nf < 0) return false;
    glm::vec3 A = glm::vec3(G[nu][3]), B = glm::vec3(G[nl][3]), C = glm::vec3(G[nf][3]);
    float l1 = glm::length(B - A), l2 = glm::length(C - B);
    glm::vec3 to = target - A; float d = glm::length(to); if (d < 1e-5f || l1 < 1e-5f || l2 < 1e-5f) return false;
    if (deficit) *deficit = std::max(0.f, d - (l1 + l2) * 0.9995f);
    glm::vec3 dir = to / d;
    float dc = std::clamp(d, std::fabs(l1 - l2) + 1e-3f, (l1 + l2) * 0.9995f);
    glm::vec3 cur = glm::normalize(C - A), pv = (B - A) - cur * glm::dot(B - A, cur);
    if (glm::length(pole) > 1e-5f) pv = pole;
    else if (glm::length(pv) < 1e-4f) pv = glm::vec3(0, 0, 1);
    glm::vec3 pl = pv - dir * glm::dot(pv, dir); if (glm::length(pl) < 1e-5f) pl = glm::vec3(0, 0, 1);
    pl = glm::normalize(pl);
    float a = (l1 * l1 - l2 * l2 + dc * dc) / (2.f * dc), h = std::sqrt(std::max(l1 * l1 - a * a, 0.f));
    glm::vec3 B2 = A + dir * a + pl * h, C2 = A + dir * dc;
    glm::quat Ru = rotOf(G[nu]), Rl = rotOf(G[nl]);
    glm::quat s1 = fromTo(B - A, B2 - A);
    glm::quat s2 = fromTo(s1 * (C - B), C2 - B2);
    glm::quat Ru2 = s1 * Ru, Rl2 = s2 * s1 * Rl;
    int par = rig.nodes[nu].parent;
    glm::quat Rp = par >= 0 ? rotOf(G[par]) : glm::quat(1, 0, 0, 0);
    pose.r[nu] = glm::normalize(glm::inverse(Rp) * Ru2);
    pose.r[nl] = glm::normalize(glm::inverse(Ru2) * Rl2);
    return true;
}

}  // namespace sk
