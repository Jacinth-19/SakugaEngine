#include "Check.h"
#include "platform/MobilePointerRouter.h"
#include "platform/SurfaceStateStore.h"
#include <string>
#include <thread>
#include <vector>

struct Snapshot { bool bloom = false; int scene = 0; std::vector<int> materials; };
int main() {
    SK_TEST("mobile_lifecycle_test");
    using Router = sk::MobilePointerRouter;
    Router r;
    CHECK(r.down(1, true) == Router::Route::Ui);
    CHECK(r.down(2, true) == Router::Route::Ignore); // no second ImGui mouse
    CHECK(r.down(3, false) == Router::Route::Game);
    CHECK(r.get(1) == Router::Route::Ui); // retains route outside window
    CHECK(r.down(1, false) == Router::Route::Ignore);
    CHECK(r.up(1) == Router::Route::Ui);
    CHECK(r.get(2) == Router::Route::Ignore);
    CHECK(r.down(4, true) == Router::Route::Ui);
    r.cancel();
    CHECK(r.get(3) == Router::Route::Ignore);
    CHECK(r.down(5, true) == Router::Route::Ui);
    CHECK(r.down(-1, false) == Router::Route::Ignore);

    sk::SurfaceStateStore<Snapshot> store;
    CHECK(!store.load("app"));
    Snapshot s; s.bloom = true; s.scene = 3; s.materials = {2, 4};
    store.save("app", s);
    s.materials[0] = 99;
    auto saved = store.load("app");
    CHECK(saved.has_value()); CHECK(saved->bloom); CHECK(saved->scene == 3); CHECK(saved->materials[0] == 2);
    CHECK(!store.load("other"));
    saved->materials[0] = 17;
    CHECK(store.load("app")->materials[0] == 2); // unsuccessful recreate doesn't consume or alter last snapshot
    std::thread writer([&] { for (int i = 0; i < 1000; ++i) store.save("app", Snapshot{true, i, {i, i}}); });
    for (int i = 0; i < 1000; ++i) {
        auto copy = store.load("app");
        CHECK(copy && copy->bloom);
        if (copy && copy->materials.size() == 2 && copy->scene != 3) CHECK(copy->materials[0] == copy->materials[1]);
    }
    writer.join();
    return t_.finish();
}
