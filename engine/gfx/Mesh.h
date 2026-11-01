#pragma once
#include "gfx/Context.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace sk {

struct Vertex {
    glm::vec3 pos{0};
    glm::vec3 normal{0, 1, 0};
    glm::vec4 tangent{1, 0, 0, 1};
    glm::vec2 uv{0};
    uint8_t joints[4] = {0, 0, 0, 0};
    glm::vec4 weights{1, 0, 0, 0};
};
static_assert(sizeof(Vertex) == 68, "vertex layout is mirrored in Renderer pipeline setup");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    void computeTangents();                    // per-triangle UV derivative, orthonormalised
    void computeNormals();
    static MeshData sphere(float radius, int segments = 48, int rings = 32, float uShift = 0.f);
    static MeshData plane(float size, float uvRepeat = 1.f);
    static MeshData box(glm::vec3 half);
};

struct GpuMesh {
    Buffer vertex, index;
    uint32_t indexCount = 0;
    glm::vec3 boundsMin{0}, boundsMax{0};
    // morph targets (position deltas) live in the renderer's morph buffer: deltas of slot k, vertex v are at morphBase + k * vertexCount + v
    uint32_t morphBase = 0, morphSlots = 0, vertexCount = 0;
    std::vector<int> morphTargetIndex;            // glTF morph target index of each slot
};

}  // namespace sk
