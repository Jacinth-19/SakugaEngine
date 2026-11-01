#pragma once
// GPU-free pointer routing for the Android animation preview. Coordinates and dimensions are in pixels.
// This does not implement the desktop sandbox's ImGui or camera input.
#include <algorithm>
#include <cmath>

namespace sk {
class MobileTouch {
public:
    enum class Action { Down, Move, Up, Cancel };
    struct Result { float stickX = 0.f, stickY = 0.f; bool nextClip = false; };

    Result event(Action action, int id, float x, float y, float width, float height, float density) {
        if (action == Action::Cancel) { reset(); return result(); }
        if (action == Action::Up) {
            if (id == stickPointer_) { stickPointer_ = -1; stickX_ = stickY_ = 0.f; }
            if (id == buttonPointer_) buttonPointer_ = -1;
            return result();
        }
        // Ignore malformed dimensions and positions rather than allowing NaNs into the motion matcher.
        if (id < 0 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height) || !std::isfinite(density) || width <= 0 || height <= 0 || density <= 0) return result();
        const double r = std::min(width, height) * .13, margin = 18.0 * density;
        if (action == Action::Down) {
            // An already-owned pointer cannot acquire a second control on a duplicate down.
            if (id == stickPointer_ || id == buttonPointer_) return result();
            if (buttonPointer_ == -1 && x >= width-margin-r*2 && x <= width-margin &&
                y >= height-margin-r*1.4f && y <= height-margin) {
                buttonPointer_ = id; return {stickX_, stickY_, true};
            }
            if (stickPointer_ == -1 && x < width*.45f && y > height*.45f) {
                stickPointer_ = id; moveStick(x,y,height,margin,r);
            }
        } else if (action == Action::Move && id == stickPointer_) {
            moveStick(x,y,height,margin,r);
        }
        return result();
    }
    void reset() { stickPointer_ = buttonPointer_ = -1; stickX_ = stickY_ = 0.f; }
    Result result() const { return {stickX_, stickY_, false}; }
private:
    void moveStick(float x, float y, float height, double margin, double r) {
        // Double precision also prevents overflow for extreme finite input coordinates.
        double dx = (x - margin - r) / r, dy = (y - (height-margin-r)) / r;
        const double len = std::hypot(dx,dy);
        if (len > 1.0) { dx /= len; dy /= len; }
        stickX_ = static_cast<float>(dx); stickY_ = static_cast<float>(dy);
    }
    int stickPointer_ = -1, buttonPointer_ = -1;
    float stickX_ = 0.f, stickY_ = 0.f;
};
}
