#include "Check.h"
#include "MobileSession.h"
#include <cmath>
#include <string>
#include <vector>

int main() {
    SK_TEST("mobile_session_test");
    sk::MobileSession bad; std::string error;
    CHECK(!bad.load({std::string(SAKUGA_ROOT) + "/missing.bvh"}, error));
    sk::MobileSession session;
    std::vector<std::string> paths = {std::string(SAKUGA_ROOT) + "/assets/motion/walk1_subject1.bvh",
                                      std::string(SAKUGA_ROOT) + "/assets/motion/run1_subject2.bvh"};
    CHECK_MSG(session.load(paths, error), error);
    if (session.clipCount() != 2) { CHECK(session.clipCount() == 2); return t_.finish(); }
    CHECK(session.clipCount() == 2);
    for (int i = 0; i < 15; ++i) session.update(1.f/30.f, 0.f, 0.f);
    CHECK(session.clipIndex() >= 0 && session.clipIndex() < session.clipCount());
    CHECK(!session.clipName().empty());
    auto a = session.joints();
    for (float v : a) CHECK_FINITE(v);
    const int first = session.clipIndex();
    session.nextClip();
    session.update(1.f/30.f, 0.f, 0.f);
    CHECK(session.clipIndex() != first);
    for (int i = 0; i < 15; ++i) session.update(1.f/30.f, 0.5f, 0.8f);
    CHECK(!session.browsing());
    auto b = session.joints();
    float diff = 0;
    for (size_t i = 0; i < b.size(); ++i) { CHECK_FINITE(b[i]); diff += std::fabs(b[i]-a[i]); }
    CHECK(diff > 0.01f);
    return t_.finish();
}
