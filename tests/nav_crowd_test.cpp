#include "Check.h"
#include "world/NavMesh.h"
#include "world/CrowdSteering.h"
int main() {
    SK_TEST("nav_crowd_test");
    sk::World w; w.groundExtent = 8.f;
    sk::NavMesh nav; CHECK(nav.build(w, {}));
    nav.initCrowd(4, .6f);
    const int id = nav.addAgent({0,0,0}, .3f, 1.7f, 2.f, 8.f);
    CHECK(id >= 0); CHECK(!nav.setTarget(-1, {3,0,0})); CHECK(nav.setTarget(id, {3,0,0}));
    for (int i=0;i<100;++i) nav.updateCrowd(.05f);
    auto info = nav.agent(id); CHECK(info.active); CHECK(info.pos.x > 1.f); CHECK(info.pos.x < 4.f);
    CHECK_FINITE(info.pos.x); CHECK_FINITE(info.pos.z);
    nav.clearTarget(id); nav.updateCrowd(.05f);
    CHECK(!nav.agent(id).hasTarget);
    return t_.finish();
}
