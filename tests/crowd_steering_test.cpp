#include "Check.h"
#include "world/CrowdSteering.h"
#include <limits>
int main() {
    SK_TEST("crowd_steering_test");
    sk::CrowdTargetGate gate; glm::vec3 a(1,0,2), b(2,0,2);
    CHECK(gate.shouldRequest(a)); gate.requested(a);
    CHECK(!gate.shouldRequest(a)); CHECK(!gate.shouldRequest(b));
    gate.tick(.35f); CHECK(!gate.shouldRequest(a)); CHECK(gate.shouldRequest(b));
    gate.requested(b); gate.tick(3.f); CHECK(gate.shouldRequest(b));
    gate.clear(); CHECK(gate.shouldRequest(b));
    auto slow = sk::crowdRequestedVelocity(glm::vec3(.2f, 1.f, 0.f), 2.f);
    CHECK_NEAR(slow.x, .2f, 1e-5); CHECK_NEAR(slow.y, 0, 1e-5);
    auto capped = sk::crowdRequestedVelocity(glm::vec3(3.f, 0, 4.f), 2.f);
    CHECK_NEAR(glm::length(capped), 2.f, 1e-5);
    CHECK_NEAR(glm::length(sk::crowdRequestedVelocity(glm::vec3(.02f,0,0), 1.f)), 0, 1e-5);
    CHECK_NEAR(sk::locomotionInputScale(0), 0, 1e-5);
    CHECK_NEAR(sk::locomotionInputScale(.08f), 0, 1e-5);
    CHECK_NEAR(sk::locomotionInputScale(1.f), 1, 1e-5);
    CHECK_NEAR(sk::locomotionInputScale(.54f), .5f, 1e-5);
    CHECK_NEAR(sk::locomotionInputScale(std::numeric_limits<float>::quiet_NaN()), 0, 1e-5);
    return t_.finish();
}
