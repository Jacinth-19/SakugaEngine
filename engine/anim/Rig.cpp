#include "anim/Rig.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace sk {

static const char* kNames[] = {"hips", "spine", "chest", "upperChest", "neck", "head",
    "leftShoulder", "leftUpperArm", "leftLowerArm", "leftHand", "rightShoulder", "rightUpperArm", "rightLowerArm", "rightHand",
    "leftUpperLeg", "leftLowerLeg", "leftFoot", "leftToes", "rightUpperLeg", "rightLowerLeg", "rightFoot", "rightToes"};
const char* humanBoneName(HumanBone b) { return kNames[(int)b]; }
int humanBoneFromVrmName(const std::string& n) {
    for (int i = 0; i < kHumanBoneCount; ++i) if (n == kNames[i]) return i;
    return -1;
}

void Rig::buildOrder() {
    children.assign(nodes.size(), {});
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].parent >= 0) children[nodes[i].parent].push_back((int)i);
    order.clear();
    std::vector<char> done(nodes.size(), 0);
    // iterative: repeat until all placed (node counts are small)
    while (order.size() < nodes.size()) {
        size_t before = order.size();
        for (size_t i = 0; i < nodes.size(); ++i)
            if (!done[i] && (nodes[i].parent < 0 || done[nodes[i].parent])) { done[i] = 1; order.push_back((int)i); }
        if (order.size() == before) break;
    }
}
void Rig::updateSubtree(int node, const Pose& p, std::vector<glm::mat4>& out) const {
    glm::mat4 local = glm::translate(glm::mat4(1.f), p.t[node]) * glm::mat4_cast(p.r[node]) * glm::scale(glm::mat4(1.f), p.s[node]);
    out[node] = nodes[node].parent >= 0 ? out[nodes[node].parent] * local : local;
    for (int c : children[node]) updateSubtree(c, p, out);
}
Pose Rig::restPose() const {
    Pose p; p.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) { p.t[i] = nodes[i].t; p.r[i] = nodes[i].r; p.s[i] = nodes[i].s; }
    return p;
}
void Rig::globals(const Pose& p, std::vector<glm::mat4>& out) const {
    out.resize(nodes.size());
    for (int i : order) {
        glm::mat4 local = glm::translate(glm::mat4(1.f), p.t[i]) * glm::mat4_cast(p.r[i]) * glm::scale(glm::mat4(1.f), p.s[i]);
        out[i] = nodes[i].parent >= 0 ? out[nodes[i].parent] * local : local;
    }
}
void Rig::applyConstraints(Pose& p) const {
    if (constraints.empty()) return;
    std::vector<glm::mat4> g; globals(p, g);
    for (const NodeConstraint& c : constraints) {
        if (c.node < 0 || c.source < 0) continue;
        const glm::quat rest = nodes[c.node].r;
        glm::quat out = rest;
        if (c.type == NodeConstraint::Aim) {
            static const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            glm::vec3 d = glm::vec3(g[c.source][3]) - glm::vec3(g[c.node][3]);
            int par = nodes[c.node].parent;
            glm::mat3 pm = par >= 0 ? glm::mat3(g[par]) : glm::mat3(1.f);
            for (int k = 0; k < 3; ++k) pm[k] = glm::normalize(pm[k]);
            if (glm::length(d) < 1e-7f) continue;
            glm::vec3 to = glm::normalize(glm::transpose(pm) * d);
            glm::vec3 from = rest * axes[c.axis];
            glm::quat ft = glm::rotation(glm::normalize(from), to);
            out = glm::slerp(rest, glm::normalize(ft * rest), c.weight);
        } else {
            glm::quat delta = glm::inverse(nodes[c.source].r) * p.r[c.source];
            if (c.type == NodeConstraint::Roll) {
                glm::vec3 ax(0); ax[c.axis] = 1.f;
                glm::vec3 v(delta.x, delta.y, delta.z);
                glm::quat tw(delta.w, ax * glm::dot(v, ax));
                float l = std::sqrt(tw.w * tw.w + tw.x * tw.x + tw.y * tw.y + tw.z * tw.z);
                delta = l > 1e-8f ? tw * (1.f / l) : glm::quat(1, 0, 0, 0);
            }
            out = glm::normalize(rest * glm::slerp(glm::quat(1, 0, 0, 0), delta, c.weight));
        }
        p.r[c.node] = out;
        globals(p, g);
    }
}

int Rig::find(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return (int)i;
    return -1;
}

void AnimClip::sample(float time, bool loop, Pose& pose) const {
    if (loop && duration > 0) time = std::fmod(time, duration);
    time = std::clamp(time, 0.f, duration);
    for (const auto& c : channels) {
        if (c.times.empty() || c.node < 0) continue;
        size_t i1 = std::upper_bound(c.times.begin(), c.times.end(), time) - c.times.begin();
        size_t i0 = i1 == 0 ? 0 : i1 - 1; if (i1 >= c.times.size()) i1 = c.times.size() - 1;
        float span = c.times[i1] - c.times[i0];
        float a = (c.step || span <= 1e-8f) ? 0.f : (time - c.times[i0]) / span;
        const glm::vec4 &v0 = c.values[i0], &v1 = c.values[i1];
        switch (c.path) {
            case AnimChannel::Translation: pose.t[c.node] = glm::mix(glm::vec3(v0), glm::vec3(v1), a); break;
            case AnimChannel::Scale:       pose.s[c.node] = glm::mix(glm::vec3(v0), glm::vec3(v1), a); break;
            case AnimChannel::Rotation: {
                glm::quat q0(v0.w, v0.x, v0.y, v0.z), q1(v1.w, v1.x, v1.y, v1.z);
                pose.r[c.node] = glm::normalize(glm::slerp(q0, q1, a)); break; }
        }
    }
}

}  // namespace sk
