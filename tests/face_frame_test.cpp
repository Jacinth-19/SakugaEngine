#include "Check.h"
#include "gfx/FaceFrame.h"
#include <glm/gtc/constants.hpp>
int main() {
    SK_TEST("face_frame_test");
    const glm::quat identity(1,0,0,0);
    const auto a = sk::makeFaceFrame(identity, identity);
    const auto b = sk::makeFaceFrame(glm::angleAxis(glm::half_pi<float>(), glm::vec3(0,1,0)), identity);
    CHECK_NEAR(a.forward.z, 1, 1e-5); CHECK_NEAR(a.right.x, 1, 1e-5);
    CHECK_NEAR(b.forward.x, 1, 1e-5); CHECK_NEAR(b.right.z, -1, 1e-5);
    CHECK_NEAR(glm::dot(b.forward, b.right), 0, 1e-5);
    // Rotation is relative to the bind head frame, not absolute world rotation.
    const auto c = sk::makeFaceFrame(glm::angleAxis(.4f, glm::vec3(0,1,0)), glm::angleAxis(.4f, glm::vec3(0,1,0)));
    CHECK_NEAR(c.forward.z, 1, 1e-5); CHECK_NEAR(c.right.x, 1, 1e-5);
    return t_.finish();
}
