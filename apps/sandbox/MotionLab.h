#pragma once
// The "Motion lab": a test course (stairs, ramp, walls, corridor, pillars, beam, mud) built from World colliders, the Recast/Detour
// navmesh generated from the very same boxes, and a small crowd of NPCs (retargeted motion matching) that follow the player or wander.
#include "asset/ProceduralTextures.h"
#include "gfx/Renderer.h"
#include "scene/Scene.h"
#include "world/NavMesh.h"
#include "world/World.h"
#include <functional>
#include <memory>
#include <vector>

namespace sk {

struct Npc {
    Entity* e = nullptr;
    int agent = -1;
    enum Mode { Follow, Wander } mode = Follow;
    glm::vec3 home{0}, goal{0};
    bool hasGoal = false, holding = true;
    float timer = 0.f, side = 1.f, speed = 0.f;
    std::string status = "idle";
};

class MotionLab {
public:
    World world;
    NavMesh nav;
    std::vector<NavSlowZone> slowZones;
    std::vector<Entity*> visuals;          // prop entities (owned by the sandbox)
    std::vector<Npc> npcs;
    int playerAgent = -1;
    bool navReady = false;
    glm::vec3 spawn{0, 0, 0};

    using AddEntity = std::function<Entity*(std::shared_ptr<Model>, const char*)>;
    void build(Renderer& r, const StyleTextures& style, bool outlines, const AddEntity& add);
    Npc& addNpc(Entity* e, Npc::Mode mode, glm::vec3 pos, float side);
    // Steers the NPCs (call before the entities' own update). `walkSpeed`/`runSpeed` come from the matcher settings.
    void updateNpcs(float dt, const Entity& player, float walkSpeed, float runSpeed);
    void debugDraw(std::vector<LineVertex>& lines, bool navmesh, bool agents) const;

private:
    mutable std::vector<NavMesh::Edge> edgeCache_;
    mutable bool edgesBuilt_ = false;
    uint32_t seed_ = 12345u;
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) / 16777216.f; }
};

}  // namespace sk
