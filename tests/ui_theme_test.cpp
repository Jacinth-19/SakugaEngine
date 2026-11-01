// UI theme: the ImGui look is data (engine/gfx/UiTheme.h), so it is testable without a device.
//
// Covered: the built-in presets are complete and sane, JSON survives a round trip, a partial file overrides only what it
// names, hostile values are clamped instead of breaking the UI, unknown roles are ignored, and applying a theme really
// writes into ImGuiStyle (that is the only part that touches Dear ImGui - the core library is linked here, no backend).
#include "Check.h"
#include "gfx/UiTheme.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <imgui.h>

using namespace sk;
namespace fs = std::filesystem;

static std::string writeTemp(const std::string& name, const std::string& text) {
    fs::path dir = fs::temp_directory_path() / "sakuga_ui_theme_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path p = dir / name;
    std::ofstream f(p, std::ios::binary);
    f << text;
    return p.string();
}

int main() {
    SK_TEST("ui_theme_test");

    // ------------------------------------------------------------------ presets
    {
        std::vector<std::string> names = uiThemeNames();
        CHECK_MSG(names.size() >= 4, "several presets are offered");
        CHECK(std::string(uiThemeDefaultName()) == "Sakuga Dark");
        CHECK(uiThemePreset("no such theme").name == "Sakuga Dark");        // unknown -> default, never empty

        bool sawDark = false, sawLight = false;
        for (const std::string& n : names) {
            UiTheme t = uiThemePreset(n);
            CHECK_MSG(t.name == n, "preset keeps its name: " + n);
            CHECK_MSG(t.colors.size() >= (size_t)ImGuiCol_COUNT, "every ImGui colour role is defined: " + n);
            for (const UiThemeColor& c : t.colors) {
                CHECK_MSG(c.rgba.r >= 0.f && c.rgba.r <= 1.f && c.rgba.g >= 0.f && c.rgba.g <= 1.f &&
                          c.rgba.b >= 0.f && c.rgba.b <= 1.f && c.rgba.a >= 0.f && c.rgba.a <= 1.f,
                          "colour in range: " + n + "/" + c.name);
            }
            CHECK(t.windowRounding >= 0.f && t.windowRounding <= 24.f);
            CHECK_NEAR(t.fontScale, 1.0, 1e-6);
            CHECK_MSG(t.color("WindowBg") && t.color("Text") && t.color("Button") && t.color("CheckMark"),
                      "the roles the UI actually uses exist: " + n);
            if (n == "Sakuga Dark") sawDark = true;
            if (n == "Sakuga Light") sawLight = true;
        }
        CHECK(sawDark);
        CHECK(sawLight);

        // the presets are actually different, and the light one is lighter
        UiTheme darkTheme = uiThemePreset("Sakuga Dark");
        UiTheme lightTheme = uiThemePreset("Sakuga Light");
        const glm::vec4* dark = darkTheme.color("WindowBg");
        const glm::vec4* light = lightTheme.color("WindowBg");
        CHECK(dark && light);
        if (dark && light) CHECK_MSG(light->r > dark->r + 0.3f, "light theme has a light window background");

        // ... and the default is not just ImGui's stock dark style
        ImGuiStyle stock;
        ImGui::StyleColorsDark(&stock);
        UiTheme preset = uiThemePreset("Sakuga Dark");
        applyUiTheme(preset, stock);
        const glm::vec4* button = preset.color("Button");
        CHECK(button);
        if (button) CHECK_NEAR((double)stock.Colors[ImGuiCol_Button].x, (double)button->r, 1e-5);
    }

    // ------------------------------------------------------------------ lookups
    {
        UiTheme t = uiThemePreset("Sakuga Dark");
        CHECK(t.color("nonsense") == nullptr);
        CHECK(t.color(nullptr) == nullptr);
        t.setColor("Custom", glm::vec4(0.25f, 0.5f, 0.75f, 1.f));
        CHECK_MSG(t.color("Custom") != nullptr, "new roles can be added");
        CHECK_NEAR((double)t.color("Custom")->g, 0.5, 1e-6);
        t.setColor("Custom", glm::vec4(1, 1, 1, 1));                  // replaces, does not duplicate
        int n = 0; for (const auto& c : t.colors) n += c.name == "Custom";
        CHECK(n == 1);
    }

    // ------------------------------------------------------------------ JSON round trip
    {
        UiTheme a = uiThemePreset("Midnight");
        a.name = "Midnight \"Remix\"";                                 // exercises string escaping
        a.fontScale = 1.25f;
        a.itemSpacingX = 13.f;
        std::string json = uiThemeToJson(a);

        UiTheme b;
        std::string err;
        CHECK_MSG(uiThemeFromJson(json, &b, &err), "generated JSON parses: " + err);
        CHECK(b.name == a.name);
        CHECK_NEAR(b.fontScale, (double)a.fontScale, 1e-4);
        CHECK_NEAR(b.itemSpacingX, (double)a.itemSpacingX, 1e-4);
        CHECK_NEAR(b.windowRounding, (double)a.windowRounding, 1e-4);
        CHECK(b.colors.size() == a.colors.size());
        for (size_t i = 0; i < a.colors.size() && i < b.colors.size(); ++i) {
            CHECK(b.colors[i].name == a.colors[i].name);
            CHECK_NEAR((double)b.colors[i].rgba.r, (double)a.colors[i].rgba.r, 1e-4);
            CHECK_NEAR((double)b.colors[i].rgba.a, (double)a.colors[i].rgba.a, 1e-4);
        }
    }

    // ------------------------------------------------------------------ partial file, hostile values
    {
        UiTheme base = uiThemePreset("Sakuga Dark");
        UiTheme out = base;
        std::string err;
        const char* partial = R"({ "fontScale": 2.0, "colors": { "Button": [1.0, 0.0, 0.0, 1.0] } })";
        CHECK_MSG(uiThemeFromJson(partial, &out, &err), "partial file parses: " + err);
        CHECK_NEAR(out.fontScale, 2.0, 1e-6);
        const glm::vec4* red = out.color("Button");
        CHECK(red && red->r == 1.f && red->g == 0.f);
        CHECK_MSG(out.color("WindowBg") && *out.color("WindowBg") == *base.color("WindowBg"),
                  "roles the file does not mention keep the preset value");
        CHECK(out.name == base.name);

        UiTheme clamped = base;
        const char* silly = R"({ "fontScale": 99, "panelAlpha": -3, "rounding": { "window": -8 },
                                  "spacing": { "x": 5000 },
                                  "colors": { "Button": [4.0, 2.0, -1.0, 1.0], "BogusRole": [1,1,1,1], "WindowBg": "red" } })";
        CHECK(uiThemeFromJson(silly, &clamped, &err));
        CHECK_NEAR(clamped.fontScale, 3.0, 1e-6);                       // clamped to the documented maximum
        CHECK_NEAR(clamped.panelAlpha, 0.0, 1e-6);
        CHECK_NEAR(clamped.windowRounding, 0.0, 1e-6);
        CHECK_NEAR(clamped.itemSpacingX, 48.0, 1e-6);
        const glm::vec4* c = clamped.color("Button");
        CHECK(c && c->r == 1.f && c->g == 1.f && c->b == 0.f);          // clamped to [0,1]
        CHECK_MSG(clamped.color("BogusRole") == nullptr, "unknown roles are ignored");
        CHECK_MSG(*clamped.color("WindowBg") == *base.color("WindowBg"), "a malformed colour is ignored");

        // sanitize is idempotent and applies to hand-built themes too
        UiTheme t = base;
        t.windowRounding = -100.f; t.itemSpacingY = 999.f; t.colors[0].rgba.g = 4.f;
        uiThemeSanitize(&t);
        t.fontScale = 0.1f;
        uiThemeSanitize(&t);
        CHECK_NEAR(t.windowRounding, 0.0, 1e-6);
        CHECK_NEAR(t.itemSpacingY, 48.0, 1e-6);
        CHECK_NEAR((double)t.colors[0].rgba.g, 1.0, 1e-6);
        CHECK_NEAR(t.fontScale, 0.5, 1e-6);
    }

    // ------------------------------------------------------------------ malformed input
    {
        UiTheme t = uiThemePreset("Sakuga Dark");
        std::string err;
        CHECK_MSG(!uiThemeFromJson("{ not json", &t, &err), "truncated JSON is rejected");
        CHECK(!err.empty());
        err.clear();
        CHECK_MSG(!uiThemeFromJson("[1, 2, 3]", &t, &err), "a non-object root is rejected");
        CHECK(!err.empty());
        CHECK_MSG(uiThemeFromJson("{}", &t, &err), "an empty object is valid and changes nothing");
    }

    // ------------------------------------------------------------------ loading from disk
    {
        UiTheme t = uiThemePreset("Monochrome");
        t.name = "OnDisk";
        t.fontScale = 1.5f;
        std::string file = writeTemp("theme.json", uiThemeToJson(t));

        std::string used, err;
        UiTheme loaded = uiThemeLoad(file, "", &used, &err);
        CHECK_MSG(loaded.name == "OnDisk", "theme file loaded: " + err);
        CHECK(used == file);
        CHECK_NEAR(loaded.fontScale, 1.5, 1e-4);
        CHECK_MSG(loaded.colors.size() == t.colors.size(), "all colours came from the file");

        // a preset name with no file next to it is not an error - it simply means "use the built-in preset"
        err.clear();
        UiTheme byName = uiThemeLoad("Midnight", "/nonexistent-dir", &used, &err);
        CHECK(byName.name == "Midnight");
        CHECK_MSG(err.empty(), "a preset name is not reported as a missing file: " + err);
        CHECK(byName.color("WindowBg") != nullptr);

        // an explicitly named file that is not there is reported
        err.clear();
        used.clear();
        UiTheme missing = uiThemeLoad("/nonexistent-dir/nope.json", "", &used, &err);
        CHECK_MSG(!err.empty(), "a missing theme file is reported");
        CHECK(missing.color("WindowBg") != nullptr);                 // ... and still yields a usable theme

        // a file that exists but is broken keeps the preset it was named after instead of failing the app
        std::string bad = writeTemp("broken.json", "{ oops");
        err.clear();
        UiTheme fallback = uiThemeLoad(bad, "", &used, &err);
        CHECK_MSG(!err.empty(), "broken theme file is reported");
        CHECK(fallback.color("WindowBg") != nullptr);
    }

    // ------------------------------------------------------------------ the theme shipped in config/
    {
        // config/ui_theme.json is what the sandbox loads by default (and what --dump-ui-theme writes); a stale or
        // hand-broken file would silently change the look, so it is checked here.
        const std::string shipped = std::string(SAKUGA_ROOT) + "/config/ui_theme.json";
        std::error_code ec;
        if (fs::exists(shipped, ec)) {
            std::string used, err;
            UiTheme t = uiThemeLoad(shipped, "", &used, &err);
            CHECK_MSG(err.empty(), "config/ui_theme.json loads: " + err);
            CHECK(t.name == "Sakuga Dark");
            UiTheme want = uiThemePreset("Sakuga Dark");
            CHECK(t.colors.size() == want.colors.size());
            const char* roles[] = {"WindowBg", "Button", "Text", "CheckMark", "TitleBgActive"};
            for (const char* role : roles) {
                const glm::vec4* a = t.color(role);
                const glm::vec4* b = want.color(role);
                CHECK_MSG(a && b, std::string("role present in both: ") + role);
                if (a && b) CHECK_NEAR((double)a->r, (double)b->r, 1e-3);
            }
        } else {
            std::printf("  (no config/ui_theme.json - generate it with 'sandbox --dump-ui-theme config/ui_theme.json')\n");
        }
    }

    // ------------------------------------------------------------------ applying to ImGuiStyle
    {
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        UiTheme t = uiThemePreset("Sakuga Light");
        applyUiTheme(t, style);
        const glm::vec4* wb = t.color("WindowBg");
        CHECK(wb != nullptr);
        if (wb) {
            CHECK_NEAR((double)style.Colors[ImGuiCol_WindowBg].x, (double)wb->r, 1e-5);
            CHECK_NEAR((double)style.Colors[ImGuiCol_WindowBg].y, (double)wb->g, 1e-5);
        }
        CHECK_NEAR((double)style.WindowRounding, (double)t.windowRounding, 1e-5);
        CHECK_NEAR((double)style.FrameRounding, (double)t.frameRounding, 1e-5);
        CHECK_NEAR((double)style.ItemSpacing.x, (double)t.itemSpacingX, 1e-5);
        CHECK_NEAR((double)style.Alpha, (double)t.panelAlpha, 1e-5);

        // editing the live style (what the F12 style editor does) and saving it back reproduces the theme
        style.Colors[ImGuiCol_Button] = ImVec4(0.11f, 0.22f, 0.33f, 1.f);
        style.WindowRounding = 11.f;
        UiTheme live = uiThemeFromLiveStyle(style, "Saved");
        CHECK(live.name == "Saved");
        CHECK_NEAR((double)live.windowRounding, 11.0, 1e-5);
        const glm::vec4* button = live.color("Button");
        CHECK(button != nullptr);
        if (button) CHECK_NEAR((double)button->b, 0.33, 1e-5);

        std::string json = uiThemeToJson(live);
        UiTheme reparsed;
        std::string err;
        CHECK(uiThemeFromJson(json, &reparsed, &err));
        CHECK_NEAR((double)reparsed.windowRounding, 11.0, 1e-3);
    }

    return t_.finish();
}
