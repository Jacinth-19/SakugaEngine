#pragma once
// Navigation: Recast builds a navmesh from the World colliders, Detour answers path / height / raycast queries and
// DetourCrowd steers agents (the NPCs and the player as an avoidance obstacle). Animation-driven characters use the
// crowd as a "steering oracle": their transform comes from root motion and is written back into the crowd every frame.
#include "world/World.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class dtNavMesh; class dtNavMeshQuery; class dtCrowd; class dtQueryFilter;

namespace sk {

enum NavArea : unsigned char { NAV_AREA_NONE = 0, NAV_AREA_GROUND = 1, NAV_AREA_SLOW = 2 };
enum NavFlag : unsigned short { NAV_FLAG_WALK = 1, NAV_FLAG_SLOW = 2 };

struct NavBuildSettings {
    float cellSize = 0.15f, cellHeight = 0.1f;
    float agentHeight = 1.7f, agentRadius = 0.32f, agentMaxClimb = 0.42f, agentMaxSlope = 47.f;
    float regionMinSize = 8.f, regionMergeSize = 20.f, edgeMaxLen = 12.f, edgeMaxError = 1.3f;
    float detailSampleDist = 6.f, detailSampleMaxError = 1.f;
    int vertsPerPoly = 6;
};

struct NavSlowZone { glm::vec3 center; glm::vec2 half; };    // axis aligned region NPCs prefer to avoid (Detour area cost)

struct CrowdAgentInfo {
    glm::vec3 pos{0}, vel{0}, desiredVel{0}, newVel{0}, target{0};
    bool hasTarget = false, active = false;
    int state = 0, targetState = 0;
    std::vector<glm::vec3> corners;       // path "straight path" corners ahead of the agent
};

class NavMesh {
public:
    ~NavMesh();
    bool build(const World& world, const std::vector<NavSlowZone>& slowZones, const NavBuildSettings& s = {});
    bool valid() const { return nav_ != nullptr; }

    // --- Detour queries
    bool nearestPoint(glm::vec3 p, glm::vec3& out, glm::vec3 extents = {2.f, 3.f, 2.f}) const;
    bool findPath(glm::vec3 a, glm::vec3 b, std::vector<glm::vec3>& straight) const;       // findPath + findStraightPath
    bool randomPointAround(glm::vec3 center, float radius, glm::vec3& out) const;          // findRandomPointAroundCircle
    bool walkable(glm::vec3 a, glm::vec3 b, float* hitFraction = nullptr) const;           // navmesh raycast (line of walk)
    float distanceToWall(glm::vec3 p, glm::vec3* wallPoint = nullptr) const;               // findDistanceToWall

    // --- crowd
    void initCrowd(int maxAgents, float maxRadius);
    int addAgent(glm::vec3 pos, float radius, float height, float maxSpeed, float maxAccel);
    void removeAgent(int id);
    void setTarget(int id, glm::vec3 p);
    void clearTarget(int id);
    void syncAgent(int id, glm::vec3 pos, glm::vec3 vel);   // overwrite the crowd's state with the animation-driven transform
    void setMaxSpeed(int id, float v);
    void updateCrowd(float dt);
    CrowdAgentInfo agent(int id) const;

    // --- statistics / debug
    int polyCount() const { return polyCount_; }
    int vertexCount() const { return vertCount_; }
    int detailTriCount() const { return detailTris_; }
    float buildMs() const { return buildMs_; }
    struct Edge { glm::vec3 a, b; bool slow; };
    void debugEdges(std::vector<Edge>& out, float lift = 0.03f) const;   // polygon outlines (cached by the caller)

private:
    dtNavMesh* nav_ = nullptr; dtNavMeshQuery* query_ = nullptr; dtCrowd* crowd_ = nullptr; dtQueryFilter* filter_ = nullptr;
    int polyCount_ = 0, vertCount_ = 0, detailTris_ = 0; float buildMs_ = 0;
    NavBuildSettings settings_;
};

}  // namespace sk
