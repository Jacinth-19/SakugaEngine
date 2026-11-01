// Image comparison: decodes the reference screenshots in docs/screenshots and compares them, so "the render changed" can be
// checked without a GPU and without eyeballing. Round 7 shipped a "pixel diff test" that opened no files and reference
// screenshots (ps_on.png / ps_off.png / blink_test.png) that were never committed - the last check here makes sure every
// screenshot the README mentions actually exists.
#include "Check.h"

#include <stb/stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA8
    bool ok() const { return w > 0 && h > 0 && px.size() == (size_t)w * h * 4; }
};

static Image load(const std::string& path) {
    Image img;
    int c = 0;
    unsigned char* data = stbi_load(path.c_str(), &img.w, &img.h, &c, 4);
    if (!data) return img;
    img.px.assign(data, data + (size_t)img.w * img.h * 4);
    stbi_image_free(data);
    return img;
}

struct Diff {
    double meanAbs = 0;   // mean |a-b| over all channels, 0..255
    int maxAbs = 0;
    double changedFrac = 0;   // fraction of pixels with any channel differing by > 8
    bool valid = false;
};

static Diff compare(const Image& a, const Image& b) {
    Diff d;
    if (!a.ok() || !b.ok() || a.w != b.w || a.h != b.h) return d;
    double sum = 0;
    size_t changed = 0;
    for (size_t i = 0; i < a.px.size(); i += 4) {
        int worst = 0;
        for (int c = 0; c < 4; ++c) {
            int diff = std::abs((int)a.px[i + c] - (int)b.px[i + c]);
            sum += diff;
            worst = std::max(worst, diff);
        }
        d.maxAbs = std::max(d.maxAbs, worst);
        if (worst > 8) ++changed;
    }
    d.meanAbs = sum / (double)a.px.size();
    d.changedFrac = (double)changed / (double)(a.px.size() / 4);
    d.valid = true;
    return d;
}

// Standard deviation of the luma channel: catches a blank/solid image that would make every comparison meaningless.
static double lumaStdDev(const Image& img) {
    if (!img.ok()) return 0;
    size_t n = img.px.size() / 4;
    double mean = 0;
    for (size_t i = 0; i < img.px.size(); i += 4) mean += 0.299 * img.px[i] + 0.587 * img.px[i + 1] + 0.114 * img.px[i + 2];
    mean /= (double)n;
    double var = 0;
    for (size_t i = 0; i < img.px.size(); i += 4) {
        double l = 0.299 * img.px[i] + 0.587 * img.px[i + 1] + 0.114 * img.px[i + 2];
        var += (l - mean) * (l - mean);
    }
    return std::sqrt(var / (double)n);
}

int main() {
    SK_TEST("pixel_diff_test");
    const std::string shots = std::string(SAKUGA_ROOT) + "/docs/screenshots/";

    // ------------------------------------------------------------------ decode
    // same-resolution captures of the same stage: the only fair pixel comparisons
    Image day = load(shots + "lab_day.png");              // 960x540
    Image night = load(shots + "lab_night.png");          // 1280x720
    Image sunset = load(shots + "lab_sunset.png");        // 1280x720
    Image shaderLab = load(shots + "shader_lab.png");     // 1280x720
    Image npr = load(shots + "lab_npr.png");              // 900x900
    Image vrm = load(shots + "vrm_standing.png");         // 900x900
    Image face = load(shots + "face_in_motion.png");      // 1440x180 strip
    Image ui = load(shots + "lab_ui.png");                // 1600x900
    Image editorUi = load(shots + "editor_ui.png");       // 1500x900

    CHECK_MSG(day.ok(), "lab_day.png decoded");
    CHECK_MSG(night.ok(), "lab_night.png decoded");
    CHECK_MSG(sunset.ok(), "lab_sunset.png decoded");
    CHECK_MSG(shaderLab.ok(), "shader_lab.png decoded");
    CHECK_MSG(npr.ok(), "lab_npr.png decoded");
    CHECK_MSG(vrm.ok(), "vrm_standing.png decoded");
    CHECK_MSG(face.ok(), "face_in_motion.png decoded");
    CHECK_MSG(ui.ok(), "lab_ui.png decoded");
    CHECK_MSG(editorUi.ok(), "editor_ui.png decoded");
    if (!night.ok() || !sunset.ok()) return t_.finish();

    CHECK(night.w > 200 && night.h > 200);
    CHECK_NEAR((double)night.w, (double)sunset.w, 0);        // same capture resolution
    CHECK_NEAR((double)night.h, (double)sunset.h, 0);

    // a missing/unreadable file is reported, not crashed on
    Image missing = load(shots + "does_not_exist.png");
    CHECK(!missing.ok());

    // ------------------------------------------------------------------ self comparison is exactly zero
    {
        Diff d = compare(day, day);
        CHECK(d.valid);
        CHECK_NEAR(d.meanAbs, 0.0, 0.0);
        CHECK_NEAR((double)d.maxAbs, 0.0, 0.0);
        CHECK_NEAR(d.changedFrac, 0.0, 0.0);
    }

    // ------------------------------------------------------------------ the screenshots are real renders, not blank images
    for (const auto& entry : {std::pair<const char*, const Image*>("lab_day", &day), {"lab_night", &night}, {"lab_sunset", &sunset},
                              {"lab_npr", &npr}, {"vrm_standing", &vrm}, {"face_in_motion", &face}}) {
        if (!entry.second->ok()) continue;
        double sd = lumaStdDev(*entry.second);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "%s is not a flat image (luma sd %.2f)", entry.first, sd);
        t_.check(sd > 5.0, "lumaStdDev", __FILE__, __LINE__, msg);
    }

    // ------------------------------------------------------------------ night vs sunset (same stage, same resolution)
    {
        Diff d = compare(night, sunset);
        CHECK(d.valid);
        CHECK_MSG(d.meanAbs > 5.0, "time of day changes the frame");
        CHECK_MSG(d.changedFrac > 0.2, "most pixels changed between night and sunset");
        CHECK(d.maxAbs > 40);
    }

    // ------------------------------------------------------------------ shader lab vs the character stage
    {
        Diff d = compare(night, shaderLab);
        CHECK(d.valid);
        CHECK_MSG(d.changedFrac > 0.2, "a different scene is a different image");
        CHECK(d.meanAbs > 1.0);
    }

    // ------------------------------------------------------------------ NPR character vs PBR character (same resolution)
    {
        Diff d = compare(vrm, npr);
        CHECK(d.valid);
        CHECK_MSG(d.changedFrac > 0.2, "the NPR/PBR renderings differ");
        CHECK(d.meanAbs > 1.0);
    }

    // ------------------------------------------------------------------ a small edit is detected precisely
    {
        Image edited = night;
        for (int y = 10; y < 20; ++y) for (int x = 10; x < 20; ++x) {
            size_t i = ((size_t)y * edited.w + (size_t)x) * 4;
            edited.px[i] = (uint8_t)std::min(255, edited.px[i] + 80);
        }
        Diff d = compare(night, edited);
        CHECK(d.valid);
        double expected = 100.0 / ((double)night.w * night.h);      // 10x10 pixels of one channel
        CHECK_NEAR(d.changedFrac, expected, 1e-6 + expected * 0.1);
        CHECK_NEAR((double)d.maxAbs, 80.0, 1.0);
        CHECK(d.meanAbs > 0.0);
    }

    // ------------------------------------------------------------------ mismatched sizes are refused
    {
        Diff d = compare(ui, editorUi);        // 1600x900 vs 1500x900
        CHECK(!d.valid);
        Image cropped = night;
        cropped.w -= 1;
        CHECK(!compare(night, cropped).valid);
    }

    // ------------------------------------------------------------------ every screenshot the README advertises exists
    {
        const std::string root = SAKUGA_ROOT;
        std::ifstream readme(root + "/README.md");
        CHECK_MSG(readme.good(), "README.md is present");
        std::string text((std::istreambuf_iterator<char>(readme)), std::istreambuf_iterator<char>());
        int refs = 0, missingRefs = 0;
        const std::string token = "](" + std::string("docs/screenshots/");
        for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + token.size())) {
            size_t start = pos + token.size(), end = start;
            while (end < text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_' || text[end] == '-' || text[end] == '.')) ++end;
            std::string name = text.substr(start, end - start);
            if (name.size() < 4 || name.substr(name.size() - 4) != ".png") continue;
            ++refs;
            bool exists = std::filesystem::exists(root + "/docs/screenshots/" + name);
            if (!exists) ++missingRefs;
            t_.check(exists, "referenced screenshot exists", __FILE__, __LINE__, root + "/docs/screenshots/" + name);
        }
        CHECK_MSG(refs >= 4, "the README still embeds its screenshots");
        CHECK_NEAR((double)missingRefs, 0.0, 0);      // Round 7 linked four images that were never captured
    }

    return t_.finish();
}
