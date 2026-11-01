#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct ImGuiStyle;   // engine/gfx/UiTheme.cpp is the only file that needs Dear ImGui itself

namespace sk {

// One ImGui colour role, addressed by ImGui's own name ("WindowBg", "Button", "Text", ...). The name is what appears in
// the JSON, so a theme stays readable and keeps working when ImGui reorders its ImGuiCol_ enum.
struct UiThemeColor {
    std::string name;
    glm::vec4 rgba{1.f, 1.f, 1.f, 1.f};
};

// The part of the look that is worth changing without a rebuild: the rounding/border/spacing knobs of ImGuiStyle plus the
// colour palette. Everything here round-trips through JSON (uiThemeFromJson / uiThemeToJson), so a theme can be tweaked
// in the running app (F12, ImGui's style editor), saved to a file and committed.
struct UiTheme {
    std::string name = "Sakuga Dark";
    float fontScale = 1.f;                 // multiplies the font pixel size (DPI / "make everything bigger")
    float windowRounding = 6.f, childRounding = 4.f, frameRounding = 4.f, popupRounding = 4.f;
    float grabRounding = 4.f, tabRounding = 4.f, scrollbarRounding = 4.f;
    float windowBorderSize = 1.f, frameBorderSize = 0.f;
    float itemSpacingX = 8.f, itemSpacingY = 5.f;
    float panelAlpha = 0.96f;              // global window alpha (ImGuiStyle::Alpha)
    std::vector<UiThemeColor> colors;      // empty = keep the colours of the preset this theme was derived from

    // Colour lookup for tests and callers; nullptr when the role is not part of this theme.
    const glm::vec4* color(const char* role) const;
    void setColor(const std::string& role, const glm::vec4& rgba);
};

// Built-in presets, in the order the UI lists them.
std::vector<std::string> uiThemeNames();
const char* uiThemeDefaultName();                       // "Sakuga Dark"

// Unknown names fall back to the default preset (never returns an empty theme).
UiTheme uiThemePreset(const std::string& name);

// The role names in the order applyUiTheme() writes them into ImGuiStyle::Colors, i.e. ImGuiCol_ 0..n.
const std::vector<std::string>& uiThemeColorNames();

// Parses a theme file. Missing members keep the values of `out` (pass the preset as the starting point to make a partial
// file meaningful); malformed JSON or a non-object root returns false and fills `error`. Unknown members are ignored, and
// every numeric value is clamped to a sane range by uiThemeSanitize(), so a hand-edited file cannot make the UI unusable.
bool uiThemeFromJson(const std::string& text, UiTheme* out, std::string* error = nullptr);
std::string uiThemeToJson(const UiTheme& theme);

// Loads "<name>.json" from `dir` when it exists, otherwise the built-in preset of that name. `path` receives the file that
// was used ("" for a built-in preset) so the caller can offer "edit this file".
UiTheme uiThemeLoad(const std::string& nameOrPath, const std::string& dir, std::string* path = nullptr,
                    std::string* error = nullptr);

// Clamps every field to a usable range (fontScale 0.5..3, rounding 0..24, border 0..4, spacing 0..48, alpha 0..1,
// colours 0..1). Idempotent.
void uiThemeSanitize(UiTheme* theme);

// Applies a theme to an ImGui style (definitions live in UiTheme.cpp, which includes imgui.h).
void applyUiTheme(const UiTheme& theme, ImGuiStyle& style);

// Captures the live style (what the user tweaked in ImGui's style editor) back into a theme that can be saved.
UiTheme uiThemeFromLiveStyle(const ImGuiStyle& style, const std::string& name);

} // namespace sk
