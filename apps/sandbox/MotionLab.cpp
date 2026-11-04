#include "MotionLab.h"
#include "core/Log.h"
#include "world/CourseLayout.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace sk {

void MotionLab::build(Renderer& r, const StyleTextures& style, bool outlines, const AddEntity& add) {
    world.groundExtent = 30.f; world.groundY = 0.f;
    auto material = [&](const char* name, glm::vec4 base) {
        Material* m = r.createMaterial(name);
        m->params.baseColor = base; m->params.outlineAlpha = outlines ? 1.f : 0.f; m->params.outlineWidth = 0.010f;
        m->tex[TEX_RAMP] = style.ramp; m->tex[TEX_SPEC_RAMP] = style.specRamp; m->tex[TEX_LUT] = style.lut;
        m->params.shadowColor = glm::vec4(0.62f, 0.58f, 0.80f, 1.f); m->params.rampBlend = 0.9f; m->params.realtimeShadowStrength = 1.f;
        m->params.pbrBaseBlend = 0.4f; m->params.rampThreshold = 0.56f; m->params.rampFeather = 0.05f; m->params.doubleSided = 1.f;
        m->params.matCapStrength = 0.f; m->params.specRampStrength = 0.f; m->params.occlusionStrength = 0.f; m->params.lutStrength = 0.12f;
        r.flushMaterial(m); return m;
    };
    Material* stone = material("Lab/Stone", {0.86f, 0.86f, 0.92f, 1}), *stair = material("Lab/Stairs", {0.95f, 0.80f, 0.72f, 1}),
              *wallM = material("Lab/Wall", {0.74f, 0.82f, 0.96f, 1}), *crate = material("Lab/Crate", {0.94f, 0.72f, 0.52f, 1}),
              *pillar = material("Lab/Pillar", {0.80f, 0.92f, 0.82f, 1}), *beam = material("Lab/Beam", {0.98f, 0.88f, 0.55f, 1}),
              *ramp = material("Lab/Ramp", {0.78f, 0.74f, 0.96f, 1}), *mud = material("Lab/Mud", {0.38f, 0.28f, 0.22f, 1});
    mud->params.outlineAlpha = 0.f; r.flushMaterial(mud);

    auto prop = [&](const char* name, glm::vec3 center, glm::vec3 half, glm::quat rot, Material* m, bool collider = true) {
        Collider c; c.name = name; c.center = center; c.half = half; c.rot = rot;
        if (collider) world.add(c);
        auto e = add(makePrimitiveModel(r, name, MeshData::box(half), m), name);
        e->useCustomTransform = true; e->customTransform = glm::translate(glm::mat4(1.f), center) * glm::mat4_cast(rot);
        visuals.push_back(e);
        return e;
    };
    const glm::quat I(1, 0, 0, 0);
    // --- stairs (8 x 0.16 m) up to a platform, a 20 degree ramp down the other side
    for (int k = 0; k < 8; ++k) {
        StairBox b = motionLabStair(k);
        prop("Stair", b.center, b.half, I, stair);
    }
    prop("Platform", {0, 0.64f, 14.f}, {2.f, 0.64f, 2.f}, I, stone);
    {
        float th = glm::radians(20.f), len = 1.28f / std::sin(th);
        float zTop = 16.f + (len * std::cos(th)) * 0.5f, yTop = 0.64f;
        glm::vec3 n(0, std::cos(th), std::sin(th));
        glm::vec3 c = glm::vec3(0, yTop, zTop) - n * 0.15f;
        prop("Ramp 20deg", c, {2.f, 0.15f, len * 0.5f}, glm::angleAxis(th, glm::vec3(1, 0, 0)), ramp);
    }
    // --- tall wall for the hand IK, corridor, slalom, beam, crates, hurdles
    prop("Tall wall", {9.f, 1.5f, 0.f}, {0.15f, 1.5f, 8.f}, I, wallM);
    prop("Corridor A", {-9.5f, 1.1f, -4.f}, {0.15f, 1.1f, 4.f}, I, wallM);
    prop("Corridor B", {-11.0f, 1.1f, -4.f}, {0.15f, 1.1f, 4.f}, I, wallM);
    for (int i = 0; i < 5; ++i) prop("Pillar", {(i % 2 ? 1.5f : -1.5f), 1.1f, -6.f - 3.f * i}, {0.3f, 1.1f, 0.3f}, I, pillar);
    prop("Beam", {-18.f, 0.15f, 4.f}, {0.25f, 0.15f, 4.f}, I, beam);
    prop("Crate", {-5.f, 0.45f, 5.f}, {0.45f, 0.45f, 0.45f}, glm::angleAxis(0.4f, glm::vec3(0, 1, 0)), crate);
    prop("Crate small", {-6.1f, 0.3f, 5.5f}, {0.3f, 0.3f, 0.3f}, glm::angleAxis(-0.3f, glm::vec3(0, 1, 0)), crate);
    prop("Crate big", {6.f, 0.5f, 5.f}, {0.5f, 0.5f, 0.5f}, I, crate);
    for (int i = 0; i < 3; ++i) prop("Hurdle", {-14.f, 0.15f, -4.f - 3.f * i}, {1.4f, 0.15f, 0.12f}, I, beam);
    prop("Low wall", {4.f, 0.3f, 2.f}, {1.6f, 0.3f, 0.15f}, I, wallM);
    // --- mud: walkable but 6x as expensive for the navmesh (visual only)
    slowZones.push_back({{5.5f, 0.f, -10.f}, {2.5f, 4.f}});
    prop("Mud", {5.5f, 0.012f, -10.f}, {2.5f, 0.012f, 4.f}, I, mud, false);

    navReady = nav.build(world, slowZones);
    if (navReady) {
        nav.initCrowd(8, 0.6f);
        SK_LOG("navmesh: %d polys, %d verts, %d detail tris, built in %.0f ms", nav.polyCount(), nav.vertexCount(), nav.detailTriCount(), nav.buildMs());
        playerAgent = nav.addAgent(spawn, 0.3f, 1.7f, 4.f, 10.f);
    } else SK_WARN("navmesh build failed; NPCs will stand still");
}

Npc& MotionLab::addNpc(Entity* e, Npc::Mode mode, glm::vec3 pos, float side) {
    Npc n; n.e = e; n.mode = mode; n.side = side; n.home = pos;
    glm::vec3 p = pos; if (navReady) nav.nearestPoint(pos, p);
    e->pos = p; n.home = p;
    if (navReady) n.agent = nav.addAgent(p, 0.3f, 1.7f, 3.5f, 8.f);
    n.timer = 1.f + rnd() * 3.f;
    npcs.push_back(n);
    return npcs.back();
}

void MotionLab::updateNpcs(float dt, const Entity& player, float walkSpeed, float runSpeed) {
    if (!navReady) return;
    nav.syncAgent(playerAgent, player.pos, player.velocity);
    float cy = std::cos(player.yaw), sy = std::sin(player.yaw);
    for (Npc& n : npcs) {
        Entity& e = *n.e;
        nav.syncAgent(n.agent, e.pos, e.velocity);
        float dp = glm::length(glm::vec2(player.pos.x - e.pos.x, player.pos.z - e.pos.z));
        glm::vec3 goal = e.pos; bool go = false; float speed = walkSpeed;
        if (n.mode == Npc::Follow) {
            // formation slot behind the player on the left (+) or right (-) side
            glm::vec3 local(n.side * 1.0f, 0, -1.5f);
            glm::vec3 slot = player.pos + glm::vec3(local.x * cy + local.z * sy, 0, -local.x * sy + local.z * cy);
            glm::vec3 snapped = slot; nav.nearestPoint(slot, snapped);
            float d = glm::length(glm::vec2(snapped.x - e.pos.x, snapped.z - e.pos.z));
            bool wasMoving = !n.holding;
            n.holding = wasMoving ? d < 0.55f : d < 1.25f;       // hysteresis
            if (!n.holding) {
                goal = snapped; go = true;
                speed = glm::clamp(glm::mix(walkSpeed * 0.7f, runSpeed, (d - 1.5f) / 4.5f), walkSpeed * 0.6f, runSpeed);
                n.status = d > 5.f ? "follow (run)" : "follow";
            } else n.status = "follow (hold)";
        } else {
            n.timer -= dt;
            bool near = dp < 2.8f;
            if (near) { n.holding = true; n.hasGoal = false; n.status = "greeting"; if (n.timer < 0) n.timer = 1.f; }
            else if (n.hasGoal) {
                float d = glm::length(glm::vec2(n.goal.x - e.pos.x, n.goal.z - e.pos.z));
                if (d < 0.6f || n.timer < -25.f) { n.hasGoal = false; n.holding = true; n.timer = 2.f + rnd() * 4.f; }
                else { goal = n.goal; go = true; n.holding = false; speed = walkSpeed; n.status = "wander"; }
            } else {
                n.holding = true; n.status = "idle";
                if (n.timer <= 0.f) {
                    glm::vec3 p;
                    if (nav.randomPointAround(n.home, 11.f, p)) { n.goal = p; n.hasGoal = true; n.timer = 0.f; }
                    else n.timer = 2.f;
                }
            }
        }
        n.targetGate.tick(dt);
        if (go) {
            if (n.targetGate.shouldRequest(goal) && nav.setTarget(n.agent, goal)) n.targetGate.requested(goal);
            nav.setMaxSpeed(n.agent, speed); n.speed = speed;
        } else {
            if (n.targetGate.active) nav.clearTarget(n.agent);
            n.targetGate.clear(); n.speed = 0.f;
        }
    }
    nav.updateCrowd(dt);
    for (Npc& n : npcs) {
        Entity& e = *n.e;
        float dp = glm::length(glm::vec2(player.pos.x - e.pos.x, player.pos.z - e.pos.z));
        CrowdAgentInfo a = nav.agent(n.agent);
        glm::vec3 v = a.newVel; v.y = 0;
        float len = glm::length(v);
        bool moving = !n.holding && a.hasTarget && len > 0.05f;
        // Respect Detour's slowdown near obstacles and destinations instead of forcing 60% speed.
        glm::vec3 desired = moving ? crowdRequestedVelocity(v, n.speed) : glm::vec3(0);
        e.sprinting = moving && n.speed > (walkSpeed + runSpeed) * 0.5f;
        n.e->hasLookTarget = false;
        // pending desired velocity is consumed by Entity::update (stored in the Npc for the caller)
        e.lookTarget = player.pos + glm::vec3(0, 1.55f * player.scale, 0);
        e.hasLookTarget = (glm::length(glm::vec2(player.pos.x - e.pos.x, player.pos.z - e.pos.z)) < 6.5f);
        // greeting: turn on the spot toward the player and smile
        if (!moving && e.hasLookTarget && dp < 3.2f) {
            float want = std::atan2(player.pos.x - e.pos.x, player.pos.z - e.pos.z);
            float d = want - e.yaw; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f;
            e.yaw += d * (1.f - std::exp(-2.5f * dt));
        }
        e.setExpression("happy", dp < 3.5f ? 0.45f : 0.f);
        e.velocityRequest = desired;
    }
    // Hard personal space: the crowd only steers (root motion drives the bodies), so characters could still end up overlapping.
    // Push NPCs out of the player and out of each other (the player is authoritative and never pushed).
    const float minD = 0.8f; const float k = 1.f - std::exp(-12.f * dt);
    for (size_t i = 0; i < npcs.size(); ++i) {
        Entity& a = *npcs[i].e; glm::vec3 push(0);
        auto repel = [&](const glm::vec3& o, float w) {
            glm::vec2 d(a.pos.x - o.x, a.pos.z - o.z); float l = glm::length(d);
            if (l >= minD) return;
            glm::vec2 dir = l > 1e-4f ? d / l : glm::vec2(std::cos(i * 2.1f), std::sin(i * 2.1f));
            push += glm::vec3(dir.x, 0, dir.y) * (minD - l) * w;
        };
        repel(player.pos, 1.f);
        for (size_t j = 0; j < npcs.size(); ++j) if (j != i) repel(npcs[j].e->pos, 0.5f);
        glm::vec3 np = a.pos + push * k; glm::vec3 snapped = np;
        if (glm::length(push) > 1e-4f && nav.nearestPoint(np, snapped) && nav.walkable(a.pos, snapped)) {
            a.pos.x = snapped.x; a.pos.z = snapped.z;
        }
    }
}

void MotionLab::debugDraw(std::vector<LineVertex>& lines, bool navmesh, bool agents) const {
    if (navmesh && navReady) {
        if (!edgesBuilt_) { nav.debugEdges(edgeCache_, 0.05f); edgesBuilt_ = true; }
        uint32_t cn = packColor(0.25f, 0.8f, 1.f), cs = packColor(1.f, 0.6f, 0.2f);
        for (auto& e : edgeCache_) { lines.push_back({e.a, e.slow ? cs : cn}); lines.push_back({e.b, e.slow ? cs : cn}); }
    }
    if (agents && navReady) {
        uint32_t cg = packColor(0.3f, 1.f, 0.4f), cy = packColor(1.f, 0.9f, 0.2f), cr = packColor(1.f, 0.35f, 0.45f);
        for (const Npc& n : npcs) {
            CrowdAgentInfo a = nav.agent(n.agent);
            glm::vec3 p = n.e->pos + glm::vec3(0, 0.08f, 0);
            glm::vec3 prev = p;
            for (auto& c : a.corners) { glm::vec3 q = c + glm::vec3(0, 0.08f, 0); lines.push_back({prev, cg}); lines.push_back({q, cg}); prev = q; }
            lines.push_back({p, cy}); lines.push_back({p + a.desiredVel * 0.5f, cy});
            lines.push_back({p + glm::vec3(0, 0.02f, 0), cr}); lines.push_back({p + a.newVel * 0.5f + glm::vec3(0, 0.02f, 0), cr});
            const int seg = 16; float r0 = 0.3f;     // agent radius circle
            for (int i = 0; i < seg; ++i) {
                float t0 = i * 6.2832f / seg, t1 = (i + 1) * 6.2832f / seg;
                lines.push_back({p + glm::vec3(std::cos(t0), 0, std::sin(t0)) * r0, cg}); lines.push_back({p + glm::vec3(std::cos(t1), 0, std::sin(t1)) * r0, cg});
            }
        }
    }
}

}  // namespace sk
