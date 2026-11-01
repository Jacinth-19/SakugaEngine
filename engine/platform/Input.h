#pragma once
// Sakuga input: loads Unity "Input System" .inputactions assets (JSON) and evaluates them against
// a device-state snapshot that the window (or a headless simulator) fills in each frame.
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace sk {

struct DeviceState {
    static constexpr int kKeys = 350;
    bool key[kKeys] = {};
    bool mouseButton[8] = {};
    glm::vec2 mousePos{0}, mouseDelta{0}, scroll{0};
    bool padConnected = false;
    int padCount = 0;                 // connected gamepads (all of them are merged into the single pad state below)
    bool padButton[15] = {};
    float padAxis[6] = {};
};

class InputSystem {
public:
    enum class ActionType { Value, Button, PassThrough };
    struct Source {                    // one resolved control
        enum Kind { None, Key, MouseButton, PadButton, PadStick, PointerDelta, Scroll } kind = None;
        int code = 0;                  // key / button index / 0=left 1=right stick
    };
    struct Binding {
        std::string path, group;
        bool composite = false;
        Source src;                    // for plain bindings
        Source up, down, left, right;  // for 2DVector composites
    };
    struct Action {
        std::string name, map;
        ActionType type = ActionType::Button;
        std::string control;           // Vector2 / Button / ...
        std::vector<Binding> bindings;
        // evaluated state
        glm::vec2 value{0};
        bool held = false, pressed = false, released = false;
    };

    bool loadActions(const std::string& jsonPath);
    void update(const DeviceState& now);           // call once per frame
    void enableMap(const std::string& map, bool on) { mapEnabled_[map] = on; }

    // Query helpers (name is the action name, searched in enabled maps)
    glm::vec2 vec2(const std::string& name) const;
    float     axis(const std::string& name) const { return vec2(name).x; }
    bool held(const std::string& name) const;
    bool pressed(const std::string& name) const;
    bool released(const std::string& name) const;
    const std::vector<Action>& actions() const { return actions_; }
    const DeviceState& device() const { return dev_; }

private:
    const Action* find(const std::string& name) const;
    float  readButton(const Source& s) const;
    glm::vec2 readVec(const Source& s) const;
    std::vector<Action> actions_;
    std::map<std::string, bool> mapEnabled_;
    DeviceState dev_;
};

}
