#include "platform/Input.h"
#include "core/Json.h"
#include "core/FileUtil.h"
#include "core/Log.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_map>

namespace sk {
namespace {
using Kind = InputSystem::Source;

int keyFromName(const std::string& n) {
    static const std::unordered_map<std::string, int> m = {
        {"space", GLFW_KEY_SPACE}, {"enter", GLFW_KEY_ENTER}, {"escape", GLFW_KEY_ESCAPE}, {"tab", GLFW_KEY_TAB},
        {"leftShift", GLFW_KEY_LEFT_SHIFT}, {"rightShift", GLFW_KEY_RIGHT_SHIFT},
        {"leftCtrl", GLFW_KEY_LEFT_CONTROL}, {"rightCtrl", GLFW_KEY_RIGHT_CONTROL},
        {"leftAlt", GLFW_KEY_LEFT_ALT}, {"rightAlt", GLFW_KEY_RIGHT_ALT}, {"backspace", GLFW_KEY_BACKSPACE},
        {"upArrow", GLFW_KEY_UP}, {"downArrow", GLFW_KEY_DOWN}, {"leftArrow", GLFW_KEY_LEFT}, {"rightArrow", GLFW_KEY_RIGHT},
        {"f1", GLFW_KEY_F1}, {"f2", GLFW_KEY_F2}, {"f3", GLFW_KEY_F3}, {"f4", GLFW_KEY_F4}, {"f5", GLFW_KEY_F5}, {"f11", GLFW_KEY_F11},
    };
    auto it = m.find(n);
    if (it != m.end()) return it->second;
    if (n.size() == 1) {
        char c = n[0];
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    return -1;
}

InputSystem::Source resolve(const std::string& path) {
    InputSystem::Source s;
    size_t slash = path.find('/');
    if (path.empty() || path[0] != '<' || slash == std::string::npos) return s;
    std::string dev = path.substr(1, path.find('>') - 1), ctl = path.substr(slash + 1);
    if (dev == "Keyboard") { int k = keyFromName(ctl); if (k >= 0) { s.kind = Kind::Key; s.code = k; } }
    else if (dev == "Mouse" || dev == "Pointer") {
        if (ctl == "leftButton")   { s.kind = Kind::MouseButton; s.code = 0; }
        else if (ctl == "rightButton")  { s.kind = Kind::MouseButton; s.code = 1; }
        else if (ctl == "middleButton") { s.kind = Kind::MouseButton; s.code = 2; }
        else if (ctl == "delta")  { s.kind = Kind::PointerDelta; }
        else if (ctl == "scroll") { s.kind = Kind::Scroll; }
    } else if (dev == "Gamepad") {
        static const std::unordered_map<std::string, int> b = {
            {"buttonSouth", GLFW_GAMEPAD_BUTTON_A}, {"buttonEast", GLFW_GAMEPAD_BUTTON_B},
            {"buttonWest", GLFW_GAMEPAD_BUTTON_X}, {"buttonNorth", GLFW_GAMEPAD_BUTTON_Y},
            {"leftShoulder", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER}, {"rightShoulder", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
            {"select", GLFW_GAMEPAD_BUTTON_BACK}, {"start", GLFW_GAMEPAD_BUTTON_START},
            {"leftStickPress", GLFW_GAMEPAD_BUTTON_LEFT_THUMB}, {"rightStickPress", GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
            {"dpad/up", GLFW_GAMEPAD_BUTTON_DPAD_UP}, {"dpad/right", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
            {"dpad/down", GLFW_GAMEPAD_BUTTON_DPAD_DOWN}, {"dpad/left", GLFW_GAMEPAD_BUTTON_DPAD_LEFT},
        };
        auto it = b.find(ctl);
        if (it != b.end()) { s.kind = Kind::PadButton; s.code = it->second; }
        else if (ctl == "leftStick")  { s.kind = Kind::PadStick; s.code = 0; }
        else if (ctl == "rightStick") { s.kind = Kind::PadStick; s.code = 1; }
    }
    return s;
}
}  // namespace

bool InputSystem::loadActions(const std::string& jsonPath) {
    std::string text;
    if (!readTextFile(jsonPath, text)) { SK_ERR("input: cannot read %s", jsonPath.c_str()); return false; }
    bool ok = false;
    Json root = Json::parse(text, &ok);
    if (!ok) { SK_ERR("input: invalid json %s", jsonPath.c_str()); return false; }
    actions_.clear();
    for (size_t mi = 0; mi < root["maps"].size(); ++mi) {
        const Json& m = root["maps"][mi];
        std::string mapName = m["name"].str();
        mapEnabled_.emplace(mapName, mapName == "Player");
        size_t first = actions_.size();
        for (size_t ai = 0; ai < m["actions"].size(); ++ai) {
            const Json& a = m["actions"][ai];
            Action act; act.name = a["name"].str(); act.map = mapName; act.control = a["expectedControlType"].str();
            std::string t = a["type"].str();
            act.type = t == "Button" ? ActionType::Button : t == "PassThrough" ? ActionType::PassThrough : ActionType::Value;
            actions_.push_back(act);
        }
        Binding* comp = nullptr; Action* compAction = nullptr;
        for (size_t bi = 0; bi < m["bindings"].size(); ++bi) {
            const Json& b = m["bindings"][bi];
            std::string an = b["action"].str();
            auto it = std::find_if(actions_.begin() + first, actions_.end(), [&](const Action& x) { return x.name == an; });
            if (it == actions_.end()) continue;
            if (b["isComposite"].boolean()) {
                it->bindings.emplace_back(); comp = &it->bindings.back(); compAction = &*it;
                comp->composite = true; comp->path = b["path"].str(); continue;
            }
            if (b["isPartOfComposite"].boolean() && comp && compAction == &*it) {
                std::string part = b["name"].str(); Source s = resolve(b["path"].str());
                // multiple keys can feed the same direction (WASD + arrows): chain them through extra composites
                Source* slot = part == "up" ? &comp->up : part == "down" ? &comp->down : part == "left" ? &comp->left : &comp->right;
                if (slot->kind != Kind::None) {                       // already used -> clone composite with this key
                    Binding extra; extra.composite = true; extra.path = comp->path;
                    Source* es = part == "up" ? &extra.up : part == "down" ? &extra.down : part == "left" ? &extra.left : &extra.right;
                    *es = s; it->bindings.push_back(extra);
                    comp = &it->bindings[it->bindings.size() - 1 - 0];  // keep pointer valid after push_back
                    // find the original composite again (first one with same path and empty slot is fine)
                    // Simplification: extras are summed together at evaluation time.
                } else *slot = s;
                continue;
            }
            comp = nullptr;
            Binding bd; bd.path = b["path"].str(); bd.group = b["groups"].str(); bd.src = resolve(bd.path);
            if (bd.src.kind != Kind::None) it->bindings.push_back(bd);
        }
    }
    SK_LOG("input: loaded %zu actions from %s", actions_.size(), jsonPath.c_str());
    return true;
}

float InputSystem::readButton(const Source& s) const {
    switch (s.kind) {
        case Source::Key: return dev_.key[s.code] ? 1.f : 0.f;
        case Source::MouseButton: return dev_.mouseButton[s.code] ? 1.f : 0.f;
        case Source::PadButton: return (dev_.padConnected && dev_.padButton[s.code]) ? 1.f : 0.f;
        default: return 0.f;
    }
}
glm::vec2 InputSystem::readVec(const Source& s) const {
    if (s.kind == Source::PadStick && dev_.padConnected) {
        glm::vec2 v = s.code == 0 ? glm::vec2(dev_.padAxis[GLFW_GAMEPAD_AXIS_LEFT_X], -dev_.padAxis[GLFW_GAMEPAD_AXIS_LEFT_Y])
                                  : glm::vec2(dev_.padAxis[GLFW_GAMEPAD_AXIS_RIGHT_X], -dev_.padAxis[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        float l = glm::length(v);
        if (l < 0.15f) return glm::vec2(0);                     // Unity default stick deadzone
        return v / l * std::min(1.f, (l - 0.15f) / 0.8f);
    }
    if (s.kind == Source::PointerDelta) return dev_.mouseDelta;
    if (s.kind == Source::Scroll) return dev_.scroll;
    return glm::vec2(0);
}

void InputSystem::update(const DeviceState& now) {
    DeviceState prev = dev_;
    dev_ = now;
    (void)prev;
    for (auto& a : actions_) {
        bool wasHeld = a.held;
        glm::vec2 v(0); float btn = 0.f;
        auto en = mapEnabled_.find(a.map);
        if (en != mapEnabled_.end() && en->second) {
            for (auto& b : a.bindings) {
                if (b.composite) {
                    v += glm::vec2(readButton(b.right) - readButton(b.left), readButton(b.up) - readButton(b.down));
                } else if (a.type == ActionType::Button) btn = std::max(btn, readButton(b.src));
                else {
                    glm::vec2 r = readVec(b.src);
                    if (b.src.kind == Source::Key || b.src.kind == Source::MouseButton || b.src.kind == Source::PadButton) btn = std::max(btn, readButton(b.src));
                    v += r;
                }
            }
            // keyboard composites are normalised, analog sticks are clamped to the unit disc (Unity behaviour)
            if (a.control == "Vector2") { float l = glm::length(v); bool isLook = a.name == "Look" || a.name == "ScrollWheel"; if (l > 1.f && !isLook) v /= l; }
        }
        a.value = v;
        a.held = a.type == ActionType::Button ? btn > 0.5f : (glm::length(v) > 0.001f || btn > 0.5f);
        a.pressed = a.held && !wasHeld;
        a.released = !a.held && wasHeld;
    }
}

const InputSystem::Action* InputSystem::find(const std::string& name) const {
    for (auto& a : actions_) {
        auto en = mapEnabled_.find(a.map);
        if (a.name == name && en != mapEnabled_.end() && en->second) return &a;
    }
    return nullptr;
}
glm::vec2 InputSystem::vec2(const std::string& n) const { auto a = find(n); return a ? a->value : glm::vec2(0); }
bool InputSystem::held(const std::string& n) const { auto a = find(n); return a && a->held; }
bool InputSystem::pressed(const std::string& n) const { auto a = find(n); return a && a->pressed; }
bool InputSystem::released(const std::string& n) const { auto a = find(n); return a && a->released; }
}  // namespace sk
