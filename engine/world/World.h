#pragma once
// Collision world made of oriented boxes (stairs, ramps, walls, pillars ...) plus an optional infinite ground plane.
// Provides what a third-person character controller and the IK systems need: ray casts, ground height queries and
// sphere depenetration. The same boxes are exported as triangles for the Recast navmesh builder.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace sk {

struct Collider {
    std::string name;
    glm::vec3 center{0};
    glm::quat rot{1, 0, 0, 0};
    glm::vec3 half{0.5f};
    bool walkable = true;       // contributes to the navmesh as walkable geometry
    bool climbable = true;      // hand IK may touch it
    int navArea = -1;           // optional Recast area override
    glm::mat4 toWorld() const;
};

struct RayHit {
    bool hit = false;
    float t = 0.f;
    glm::vec3 point{0}, normal{0, 1, 0};
    int collider = -1;          // -1 = ground plane
};

class World {
public:
    float groundY = 0.f;
    bool hasGround = true;
    float groundExtent = 40.f;  // half size of the ground used for the navmesh

    int add(const Collider& c) { colliders_.push_back(c); return (int)colliders_.size() - 1; }
    const std::vector<Collider>& colliders() const { return colliders_; }
    std::vector<Collider>& colliders() { return colliders_; }
    void clear() { colliders_.clear(); }

    // Closest hit along a ray (dir need not be normalised).
    RayHit raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) const;
    // Highest walkable surface below `from` (searching down `maxDown` metres). Returns false if nothing is hit.
    bool groundHeight(glm::vec3 from, float maxDown, float& height, glm::vec3* normal = nullptr) const;
    // Pushes a sphere out of every box; returns the accumulated correction.
    glm::vec3 depenetrate(glm::vec3 center, float radius) const;
    // Nearest point on the box surface (for proximity queries)
    glm::vec3 closestPoint(const Collider& c, glm::vec3 p) const;

private:
    std::vector<Collider> colliders_;
};

// Triangle soup (12 triangles per box) for navmesh generation.
void appendBoxTriangles(const Collider& c, std::vector<float>& verts, std::vector<int>& tris);

}  // namespace sk
