#pragma once
// CPU-only, process-local surface state. A failed initialization must not consume the last good state.
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace sk {
template<class T> class SurfaceStateStore {
public:
    void save(const std::string& key, T state) {
        std::lock_guard<std::mutex> lock(mutex_);
        key_ = key; state_ = std::move(state);
    }
    std::optional<T> load(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return key == key_ ? state_ : std::nullopt;
    }
private:
    mutable std::mutex mutex_;
    std::string key_;
    std::optional<T> state_;
};
}
