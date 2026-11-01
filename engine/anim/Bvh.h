#pragma once
// BVH motion-capture loader (hierarchy + channel layout + frames) and forward kinematics.
// C++ counterpart of Assets/Scripts/BVHImporter/BVHParser.cs in the original Unity project.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace sk {

struct BvhJoint {
    std::string name;
    int parent = -1;
    glm::vec3 offset{0};
    int channelStart = 0;
    std::vector<int> channels;     // 0..2 = X/Y/Z position, 3..5 = X/Y/Z rotation (in file order)
};

struct BvhClip {
    std::string name;
    std::vector<BvhJoint> joints;  // End Sites are dropped
    int frameCount = 0, channelCount = 0;
    float frameTime = 1.f / 30.f;
    std::vector<float> data;       // frameCount * channelCount

    int findJoint(const std::string& n) const;
    // World-space joint transforms for a frame. `scale` converts file units (LAFAN: cm) to metres.
    void fk(int frame, float scale, std::vector<glm::vec3>& pos, std::vector<glm::quat>& rot) const;
};

bool loadBvh(const std::string& path, BvhClip& out, std::string* error = nullptr);

}  // namespace sk
