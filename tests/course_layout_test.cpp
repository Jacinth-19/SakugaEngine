#include "Check.h"
#include "world/CourseLayout.h"
#include "world/World.h"
int main() {
    SK_TEST("course_layout_test"); sk::World world;
    for (int i = 0; i < 8; ++i) {
        auto b = sk::motionLabStair(i);
        sk::Collider c; c.center = b.center; c.half = b.half; world.add(c);
        CHECK_NEAR(b.center.y + b.half.y, (i+1)*.16f, 1e-5);
        if (i) CHECK_NEAR((b.center.z - b.half.z) - (sk::motionLabStair(i-1).center.z + .15f), 0, 1e-5);
    }
    for (int i = 0; i < 8; ++i) {
        auto b = sk::motionLabStair(i); float height = -1;
        CHECK(world.groundHeight(glm::vec3(0, 3, b.center.z), 4, height));
        CHECK_NEAR(height, (i+1)*.16f, 1e-4);
    }
    CHECK_NEAR(sk::motionLabStair(7).center.z + .15f, 12.f, 1e-5);
    return t_.finish();
}
