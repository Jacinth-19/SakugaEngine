// UI theme: the knobs and colours of the ImGui look, loadable from JSON so the interface can be tweaked in the running app.
//
// This is the only file in engine/gfx that includes Dear ImGui; the data itself (UiTheme) is plain C++ so it can be unit
// tested without a device (tests/ui_theme_test.cpp).
#include "gfx/UiTheme.h"

#include "core/Json.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sk {
namespace {

// ImGuiCol_ names in enum order. Keep in sync with imgui.h - the static_assert below fails loudly when ImGui adds one.
const char* const kColorNames[] = {
    "Text", "TextDisabled", "WindowBg", "ChildBg", "PopupBg", "Border", "BorderShadow", "FrameBg", "FrameBgHovered",
    "FrameBgActive", "TitleBg", "TitleBgActive", "TitleBgCollapsed", "MenuBarBg", "ScrollbarBg", "ScrollbarGrab",
    "ScrollbarGrabHovered", "ScrollbarGrabActive", "CheckMark", "SliderGrab", "SliderGrabActive", "Button",
    "ButtonHovered", "ButtonActive", "Header", "HeaderHovered", "HeaderActive", "Separator", "SeparatorHovered",
    "SeparatorActive", "ResizeGrip", "ResizeGripHovered", "ResizeGripActive", "TabHovered", "Tab", "TabSelected",
    "TabSelectedOverline", "TabDimmed", "TabDimmedSelected", "TabDimmedSelectedOverline", "PlotLines",
    "PlotLinesHovered", "PlotHistogram", "PlotHistogramHovered", "TableHeaderBg", "TableBorderStrong", "TableBorderLight",
    "TableRowBg", "TableRowBgAlt", "TextLink", "TextSelectedBg", "DragDropTarget", "NavCursor", "NavWindowingHighlight",
    "NavWindowingDimBg", "ModalWindowDimBg",
};
constexpr int kColorCount = (int)(sizeof(kColorNames) / sizeof(kColorNames[0]));
static_assert(kColorCount == ImGuiCol_COUNT, "the ImGuiCol_ name table is out of date (imgui.h added/removed a colour)");

int colorIndex(const std::string& name) {
    for (int i = 0; i < kColorCount; ++i) if (name == kColorNames[i]) return i;
    return -1;
}

glm::vec4 toVec(const ImVec4& c) { return {c.x, c.y, c.z, c.w}; }
ImVec4 toIm(const glm::vec4& c) { return {c.x, c.y, c.z, c.w}; }

// A preset starts from ImGui's own dark/light palette (so every role is defined) and then overrides the handful of roles
// that give the editor its look.
UiTheme baseTheme(const char* name, bool light) {
    ImGuiStyle st;
    if (light) ImGui::StyleColorsLight(&st); else ImGui::StyleColorsDark(&st);
    UiTheme t;
    t.name = name;
    t.colors.reserve(kColorCount);
    for (int i = 0; i < kColorCount; ++i) t.colors.push_back({kColorNames[i], toVec(st.Colors[i])});
    return t;
}

void tint(UiTheme& t, const char* role, float r, float g, float b, float a = -1.f) {
    if (const glm::vec4* c = t.color(role)) t.setColor(role, {r, g, b, a < 0.f ? c->a : a});
}

UiTheme presetSakugaDark() {
    UiTheme t = baseTheme("Sakuga Dark", false);
    // Ink-coloured surfaces, violet interaction states and a warm coral accent. Roles stay distinct
    // in the collapsed editor as well as in the material inspector; the entire palette remains editable.
    t.windowRounding = 10.f; t.childRounding = 8.f; t.frameRounding = 6.f; t.popupRounding = 8.f;
    t.grabRounding = 6.f; t.tabRounding = 6.f; t.scrollbarRounding = 8.f;
    t.itemSpacingX = 10.f; t.itemSpacingY = 6.f; t.panelAlpha = 1.f;
    tint(t, "Text", 0.93f, 0.93f, 0.97f, 1.f);
    tint(t, "TextDisabled", 0.58f, 0.60f, 0.69f, 1.f);
    tint(t, "WindowBg", 0.055f, 0.060f, 0.090f, 0.97f);
    tint(t, "ChildBg", 0.075f, 0.080f, 0.120f, 0.95f);
    tint(t, "PopupBg", 0.075f, 0.078f, 0.115f, 0.99f);
    tint(t, "Border", 0.25f, 0.26f, 0.35f, 0.65f);
    tint(t, "TitleBg", 0.065f, 0.068f, 0.105f, 1.f);
    tint(t, "TitleBgActive", 0.125f, 0.105f, 0.195f, 1.f);
    tint(t, "TitleBgCollapsed", 0.065f, 0.068f, 0.105f, 0.90f);
    tint(t, "MenuBarBg", 0.075f, 0.078f, 0.115f, 1.f);
    tint(t, "FrameBg", 0.115f, 0.125f, 0.175f, 1.f);
    tint(t, "FrameBgHovered", 0.195f, 0.180f, 0.285f, 1.f);
    tint(t, "FrameBgActive", 0.255f, 0.215f, 0.375f, 1.f);
    tint(t, "Button", 0.185f, 0.155f, 0.270f, 1.f);
    tint(t, "ButtonHovered", 0.385f, 0.270f, 0.535f, 1.f);
    tint(t, "ButtonActive", 0.485f, 0.315f, 0.625f, 1.f);
    tint(t, "Header", 0.145f, 0.125f, 0.220f, 1.f);
    tint(t, "HeaderHovered", 0.300f, 0.225f, 0.425f, 1.f);
    tint(t, "HeaderActive", 0.380f, 0.270f, 0.505f, 1.f);
    tint(t, "Tab", 0.105f, 0.100f, 0.160f, 1.f);
    tint(t, "TabSelected", 0.255f, 0.195f, 0.355f, 1.f);
    tint(t, "TabHovered", 0.400f, 0.275f, 0.510f, 1.f);
    tint(t, "TabSelectedOverline", 0.98f, 0.56f, 0.49f, 1.f);
    tint(t, "CheckMark", 1.00f, 0.64f, 0.55f, 1.f);
    tint(t, "SliderGrab", 0.89f, 0.51f, 0.48f, 1.f);
    tint(t, "SliderGrabActive", 1.00f, 0.68f, 0.57f, 1.f);
    tint(t, "TextLink", 0.79f, 0.70f, 1.00f, 1.f);
    tint(t, "Separator", 0.26f, 0.25f, 0.35f, 0.8f);
    tint(t, "ScrollbarBg", 0.05f, 0.05f, 0.08f, 0.60f);
    tint(t, "ScrollbarGrab", 0.26f, 0.24f, 0.34f, 1.f);
    tint(t, "ScrollbarGrabHovered", 0.39f, 0.32f, 0.49f, 1.f);
    tint(t, "ScrollbarGrabActive", 0.50f, 0.37f, 0.60f, 1.f);
    tint(t, "TableHeaderBg", 0.13f, 0.12f, 0.19f, 1.f);
    tint(t, "TextSelectedBg", 0.40f, 0.29f, 0.56f, 0.65f);
    return t;
}

UiTheme presetSakugaLight() {
    UiTheme t = baseTheme("Sakuga Light", true);
    t.windowRounding = 7.f; t.frameRounding = 5.f; t.grabRounding = 5.f; t.tabRounding = 5.f;
    tint(t, "WindowBg", 0.965f, 0.968f, 0.975f, 0.97f);
    tint(t, "ChildBg", 0.985f, 0.988f, 0.992f, 0.85f);
    tint(t, "PopupBg", 0.980f, 0.983f, 0.990f, 0.98f);
    tint(t, "TitleBgActive", 0.880f, 0.910f, 0.940f);
    tint(t, "FrameBg", 0.905f, 0.915f, 0.930f);
    tint(t, "FrameBgHovered", 0.860f, 0.880f, 0.905f);
    tint(t, "Button", 0.870f, 0.900f, 0.930f);
    tint(t, "ButtonHovered", 0.760f, 0.870f, 0.950f);
    tint(t, "ButtonActive", 0.640f, 0.800f, 0.930f);
    tint(t, "Header", 0.800f, 0.860f, 0.920f, 0.85f);
    tint(t, "HeaderHovered", 0.740f, 0.830f, 0.920f, 0.90f);
    tint(t, "CheckMark", 0.130f, 0.430f, 0.640f);
    tint(t, "SliderGrab", 0.230f, 0.520f, 0.760f);
    tint(t, "TextLink", 0.110f, 0.380f, 0.680f);
    tint(t, "Separator", 0.780f, 0.800f, 0.830f);
    t.panelAlpha = 0.97f;
    return t;
}

UiTheme presetMidnight() {
    UiTheme t = baseTheme("Midnight", false);
    t.windowRounding = 9.f; t.frameRounding = 6.f; t.grabRounding = 6.f; t.tabRounding = 6.f; t.windowBorderSize = 1.f;
    t.itemSpacingX = 10.f; t.itemSpacingY = 6.f;
    tint(t, "WindowBg", 0.045f, 0.045f, 0.070f, 0.97f);
    tint(t, "ChildBg", 0.055f, 0.055f, 0.085f, 0.85f);
    tint(t, "PopupBg", 0.040f, 0.040f, 0.062f, 0.98f);
    tint(t, "TitleBgActive", 0.120f, 0.100f, 0.220f);
    tint(t, "Button", 0.140f, 0.120f, 0.260f);
    tint(t, "ButtonHovered", 0.300f, 0.230f, 0.520f);
    tint(t, "ButtonActive", 0.420f, 0.320f, 0.700f);
    tint(t, "Header", 0.220f, 0.180f, 0.400f, 0.85f);
    tint(t, "HeaderHovered", 0.320f, 0.260f, 0.560f, 0.90f);
    tint(t, "CheckMark", 0.700f, 0.560f, 1.000f);
    tint(t, "SliderGrab", 0.520f, 0.420f, 0.880f);
    tint(t, "SliderGrabActive", 0.660f, 0.560f, 0.980f);
    tint(t, "TextLink", 0.620f, 0.520f, 0.980f);
    tint(t, "TabSelected", 0.230f, 0.190f, 0.420f);
    t.panelAlpha = 0.97f;
    return t;
}

UiTheme presetMonochrome() {
    UiTheme t = baseTheme("Monochrome", false);
    t.windowRounding = 2.f; t.frameRounding = 2.f; t.grabRounding = 2.f; t.tabRounding = 2.f; t.popupRounding = 2.f;
    t.windowBorderSize = 1.f; t.panelAlpha = 1.f;
    for (UiThemeColor& c : t.colors) {           // keep alpha, flatten the hue to a neutral grey ramp
        float l = 0.2126f * c.rgba.r + 0.7152f * c.rgba.g + 0.0722f * c.rgba.b;
        c.rgba = {l, l, l, c.rgba.a};
    }
    tint(t, "ButtonHovered", 0.480f, 0.480f, 0.480f);
    tint(t, "ButtonActive", 0.620f, 0.620f, 0.620f);
    tint(t, "HeaderHovered", 0.400f, 0.400f, 0.400f, 0.9f);
    tint(t, "CheckMark", 0.940f, 0.940f, 0.940f);
    tint(t, "TextLink", 0.860f, 0.860f, 0.860f);
    return t;
}

float clampf(double v, float lo, float hi) { return (float)std::clamp(v, (double)lo, (double)hi); }

// Minimal JSON string escaping (role names are identifiers, but a theme name is free text).
std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)c); o += buf; }
        else o += c;
    }
    return o;
}

bool readColor(const Json& j, glm::vec4* out) {
    if (j.type != Json::Arr || (j.a.size() != 3 && j.a.size() != 4)) return false;
    glm::vec4 c(0, 0, 0, 1);
    for (size_t i = 0; i < j.a.size(); ++i) {
        if (j.a[i].type != Json::Num) return false;
        c[i] = clampf(j.a[i].n, 0.f, 1.f);
    }
    *out = c;
    return true;
}

} // namespace

const glm::vec4* UiTheme::color(const char* role) const {
    if (!role) return nullptr;
    for (const UiThemeColor& c : colors) if (c.name == role) return &c.rgba;
    return nullptr;
}

void UiTheme::setColor(const std::string& role, const glm::vec4& rgba) {
    for (UiThemeColor& c : colors) if (c.name == role) { c.rgba = rgba; return; }
    colors.push_back({role, rgba});
}

std::vector<std::string> uiThemeNames() { return {"Sakuga Dark", "Sakuga Light", "Midnight", "Monochrome"}; }
const char* uiThemeDefaultName() { return "Sakuga Dark"; }

const std::vector<std::string>& uiThemeColorNames() {
    static const std::vector<std::string> names(kColorNames, kColorNames + kColorCount);
    return names;
}

UiTheme uiThemePreset(const std::string& name) {
    if (name == "Sakuga Light") return presetSakugaLight();
    if (name == "Midnight") return presetMidnight();
    if (name == "Monochrome") return presetMonochrome();
    return presetSakugaDark();
}

void uiThemeSanitize(UiTheme* t) {
    if (!t) return;
    t->fontScale = clampf(t->fontScale, 0.5f, 3.f);
    t->windowRounding = clampf(t->windowRounding, 0.f, 24.f);
    t->childRounding = clampf(t->childRounding, 0.f, 24.f);
    t->frameRounding = clampf(t->frameRounding, 0.f, 24.f);
    t->popupRounding = clampf(t->popupRounding, 0.f, 24.f);
    t->grabRounding = clampf(t->grabRounding, 0.f, 24.f);
    t->tabRounding = clampf(t->tabRounding, 0.f, 24.f);
    t->scrollbarRounding = clampf(t->scrollbarRounding, 0.f, 24.f);
    t->windowBorderSize = clampf(t->windowBorderSize, 0.f, 4.f);
    t->frameBorderSize = clampf(t->frameBorderSize, 0.f, 4.f);
    t->itemSpacingX = clampf(t->itemSpacingX, 0.f, 48.f);
    t->itemSpacingY = clampf(t->itemSpacingY, 0.f, 48.f);
    t->panelAlpha = clampf(t->panelAlpha, 0.f, 1.f);
    for (UiThemeColor& c : t->colors) c.rgba = glm::clamp(c.rgba, glm::vec4(0.f), glm::vec4(1.f));
}

std::string uiThemeToJson(const UiTheme& theme) {
    char buf[256];
    std::string o = "{\n";
    o += "  \"name\": \"" + esc(theme.name) + "\",\n";
    std::snprintf(buf, sizeof buf,
                  "  \"fontScale\": %.3f,\n"
                  "  \"panelAlpha\": %.3f,\n"
                  "  \"rounding\": { \"window\": %.1f, \"child\": %.1f, \"frame\": %.1f, \"popup\": %.1f, \"grab\": %.1f, \"tab\": %.1f, \"scrollbar\": %.1f },\n"
                  "  \"borders\": { \"window\": %.1f, \"frame\": %.1f },\n"
                  "  \"spacing\": { \"x\": %.1f, \"y\": %.1f },\n",
                  theme.fontScale, theme.panelAlpha, theme.windowRounding, theme.childRounding, theme.frameRounding,
                  theme.popupRounding, theme.grabRounding, theme.tabRounding, theme.scrollbarRounding,
                  theme.windowBorderSize, theme.frameBorderSize, theme.itemSpacingX, theme.itemSpacingY);
    o += buf;
    o += "  \"colors\": {\n";
    for (size_t i = 0; i < theme.colors.size(); ++i) {
        const UiThemeColor& c = theme.colors[i];
        std::snprintf(buf, sizeof buf, "    \"%s\": [%.4f, %.4f, %.4f, %.4f]%s\n", c.name.c_str(),
                      c.rgba.r, c.rgba.g, c.rgba.b, c.rgba.a, i + 1 < theme.colors.size() ? "," : "");
        o += buf;
    }
    o += "  }\n}\n";
    return o;
}

bool uiThemeFromJson(const std::string& text, UiTheme* out, std::string* error) {
    if (!out) return false;
    bool ok = false;
    Json root = Json::parse(text, &ok);
    if (!ok || !root.isObj()) {
        if (error) *error = ok ? "theme file must contain a JSON object" : "invalid JSON";
        return false;
    }
    if (root.has("name")) out->name = root["name"].str(out->name);
    if (root.has("fontScale")) out->fontScale = clampf(root["fontScale"].num(out->fontScale), 0.5f, 3.f);
    if (root.has("panelAlpha")) out->panelAlpha = clampf(root["panelAlpha"].num(out->panelAlpha), 0.f, 1.f);
    const Json& r = root["rounding"];
    if (r.isObj()) {
        out->windowRounding = clampf(r["window"].num(out->windowRounding), 0.f, 24.f);
        out->childRounding = clampf(r["child"].num(out->childRounding), 0.f, 24.f);
        out->frameRounding = clampf(r["frame"].num(out->frameRounding), 0.f, 24.f);
        out->popupRounding = clampf(r["popup"].num(out->popupRounding), 0.f, 24.f);
        out->grabRounding = clampf(r["grab"].num(out->grabRounding), 0.f, 24.f);
        out->tabRounding = clampf(r["tab"].num(out->tabRounding), 0.f, 24.f);
        out->scrollbarRounding = clampf(r["scrollbar"].num(out->scrollbarRounding), 0.f, 24.f);
    }
    const Json& b = root["borders"];
    if (b.isObj()) {
        out->windowBorderSize = clampf(b["window"].num(out->windowBorderSize), 0.f, 4.f);
        out->frameBorderSize = clampf(b["frame"].num(out->frameBorderSize), 0.f, 4.f);
    }
    const Json& sp = root["spacing"];
    if (sp.isObj()) {
        out->itemSpacingX = clampf(sp["x"].num(out->itemSpacingX), 0.f, 48.f);
        out->itemSpacingY = clampf(sp["y"].num(out->itemSpacingY), 0.f, 48.f);
    }
    const Json& cols = root["colors"];
    if (cols.isObj()) {
        int applied = 0, ignored = 0;
        for (const auto& kv : cols.o) {
            glm::vec4 c;
            if (colorIndex(kv.first) < 0 || !readColor(kv.second, &c)) { ++ignored; continue; }
            out->setColor(kv.first, c);
            ++applied;
        }
        (void)applied; (void)ignored;   // tolerance is the point: an unknown role or a malformed colour is skipped
    }
    return true;
}

UiTheme uiThemeLoad(const std::string& nameOrPath, const std::string& dir, std::string* path, std::string* error) {
    if (path) path->clear();
    const bool jsonSuffix = nameOrPath.size() > 5 && nameOrPath.compare(nameOrPath.size() - 5, 5, ".json") == 0;
    const bool looksLikeFile = nameOrPath.find('/') != std::string::npos ||
                               nameOrPath.find('\\') != std::string::npos || jsonSuffix;
    std::string file = nameOrPath;
    if (!looksLikeFile) file = dir.empty() ? nameOrPath + ".json" : dir + "/" + nameOrPath + ".json";
    if (FILE* f = std::fopen(file.c_str(), "rb")) {
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
        UiTheme t = uiThemePreset(nameOrPath);     // partial files inherit the preset they are named after
        std::string err;
        if (uiThemeFromJson(text, &t, &err)) {
            uiThemeSanitize(&t);
            if (path) *path = file;
            return t;
        }
        if (error) *error = file + ": " + err;
        if (path) *path = file;
        return t;
    }
    if (looksLikeFile) {
        if (error) *error = file + ": not found";
    }
    return uiThemePreset(nameOrPath);
}

void applyUiTheme(const UiTheme& theme, ImGuiStyle& style) {
    style.WindowRounding = theme.windowRounding;
    style.ChildRounding = theme.childRounding;
    style.FrameRounding = theme.frameRounding;
    style.PopupRounding = theme.popupRounding;
    style.GrabRounding = theme.grabRounding;
    style.TabRounding = theme.tabRounding;
    style.ScrollbarRounding = theme.scrollbarRounding;
    style.WindowBorderSize = theme.windowBorderSize;
    style.FrameBorderSize = theme.frameBorderSize;
    style.ItemSpacing = ImVec2(theme.itemSpacingX, theme.itemSpacingY);
    style.Alpha = theme.panelAlpha;
    for (const UiThemeColor& c : theme.colors) {
        int i = colorIndex(c.name);
        if (i >= 0) style.Colors[i] = toIm(c.rgba);
    }
}

UiTheme uiThemeFromLiveStyle(const ImGuiStyle& style, const std::string& name) {
    UiTheme t;
    t.name = name;
    t.windowRounding = style.WindowRounding; t.childRounding = style.ChildRounding;
    t.frameRounding = style.FrameRounding; t.popupRounding = style.PopupRounding;
    t.grabRounding = style.GrabRounding; t.tabRounding = style.TabRounding;
    t.scrollbarRounding = style.ScrollbarRounding;
    t.windowBorderSize = style.WindowBorderSize; t.frameBorderSize = style.FrameBorderSize;
    t.itemSpacingX = style.ItemSpacing.x; t.itemSpacingY = style.ItemSpacing.y;
    t.panelAlpha = style.Alpha;
    t.colors.reserve(kColorCount);
    for (int i = 0; i < kColorCount; ++i) t.colors.push_back({kColorNames[i], toVec(style.Colors[i])});
    return t;
}

} // namespace sk
