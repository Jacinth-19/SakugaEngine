#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
namespace sk {
struct FaceFrame { glm::vec3 forward{0,0,1}, right{1,0,0}; };
inline FaceFrame makeFaceFrame(glm::quat currentHead, glm::quat restHead) {
    glm::quat delta = glm::normalize(currentHead * glm::inverse(restHead));
    return {delta * glm::vec3(0, 0, 1), delta * glm::vec3(1, 0, 0)};
}
}
