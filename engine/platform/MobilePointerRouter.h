#pragma once
#include <unordered_map>
namespace sk {
// One ImGui mouse pointer plus arbitrary independent gameplay touches. The hit test is
// performed only on DOWN; a drag retains its owner even when it leaves the window.
class MobilePointerRouter {
public:
    enum class Route { Ignore, Ui, Game };
    Route down(int id, bool overUi) {
        auto existing = routes_.find(id);
        if (id < 0 || existing != routes_.end()) return Route::Ignore;
        Route route = overUi ? (uiPointer_ < 0 ? Route::Ui : Route::Ignore) : Route::Game;
        routes_[id] = route;
        if (route == Route::Ui) uiPointer_ = id;
        return route;
    }
    Route get(int id) const { auto it = routes_.find(id); return it == routes_.end() ? Route::Ignore : it->second; }
    Route up(int id) {
        Route r = get(id); routes_.erase(id);
        if (uiPointer_ == id) uiPointer_ = -1;
        return r;
    }
    void cancel() { routes_.clear(); uiPointer_ = -1; }
private:
    int uiPointer_ = -1;
    std::unordered_map<int, Route> routes_;
};
}
