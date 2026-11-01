// Colour LUT parsing: .cube (Adobe/Resolve), .3dl (Autodesk) and LUT images (three.js layout), plus the identity table.
// The strip layout is the one the shader samples (x = blue slice * N + red, y = green), so a wrong layout here is a
// wrong-colour render; the tests index the strip directly and compare against analytically known values.
#include "Check.h"
#include "asset/Lut.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace sk;

static size_t stripIndex(const LutStrip& l, int r, int g, int b) { return ((size_t)g * l.size * l.size + (size_t)b * l.size + (size_t)r) * 4; }

// Writes a small .cube with a known transform: out = in (identity) unless `redBias` is set.
static std::string writeTempCube(int n, bool redBias) {
    std::string path = "/tmp/sakuga_test_lut.cube";
    std::ofstream f(path);
    f << "TITLE \"test\"\nLUT_3D_SIZE " << n << "\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    for (int b = 0; b < n; ++b) for (int g = 0; g < n; ++g) for (int r = 0; r < n; ++r) {
        float R = redBias ? 1.f : (float)r / (n - 1);
        f << R << " " << (float)g / (n - 1) << " " << (float)b / (n - 1) << "\n";
    }
    return path;
}

int main() {
    SK_TEST("lut_parsing_test");
    const std::string assets = SAKUGA_ASSET_DIR;

    // ------------------------------------------------------------------ identity table
    {
        LutStrip id = identityLut(16);
        CHECK_NEAR((double)id.size, 16.0, 0);
        CHECK_NEAR((double)id.rgba.size(), 16.0 * 16 * 16 * 4, 0);
        for (int v : {0, 1, 7, 15}) {
            size_t i = stripIndex(id, v, v, v);
            CHECK_NEAR((double)id.rgba[i + 0], (double)id.rgba[i + 1], 0);
            CHECK_NEAR((double)id.rgba[i + 1], (double)id.rgba[i + 2], 0);
            CHECK_NEAR((double)((int)(255.f * v / 15.f + 0.5f)), (double)id.rgba[i], 0);
        }
        size_t black = stripIndex(id, 0, 0, 0), white = stripIndex(id, 15, 15, 15);
        CHECK_NEAR((double)id.rgba[black], 0.0, 0);
        CHECK_NEAR((double)id.rgba[white], 255.0, 0);
        CHECK_NEAR((double)id.rgba[stripIndex(id, 15, 0, 0)], 255.0, 0);      // red axis
        CHECK_NEAR((double)id.rgba[stripIndex(id, 0, 15, 0) + 1], 255.0, 0);  // green axis
        CHECK_NEAR((double)id.rgba[stripIndex(id, 0, 0, 15) + 2], 255.0, 0);  // blue axis
    }

    // ------------------------------------------------------------------ .cube, synthetic (exact values)
    {
        LutStrip lut;
        CHECK(loadCubeLut(writeTempCube(5, false), lut));
        CHECK_NEAR((double)lut.size, 5.0, 0);
        CHECK_NEAR((double)lut.rgba.size(), 5.0 * 5 * 5 * 4, 0);
        // identity cube == identity table
        LutStrip id = identityLut(5);
        CHECK(lut.rgba == id.rgba);
        // red axis really is red-only
        size_t i = stripIndex(lut, 4, 0, 0);
        CHECK_NEAR((double)lut.rgba[i + 0], 255.0, 0);
        CHECK_NEAR((double)lut.rgba[i + 1], 0.0, 0);
        CHECK_NEAR((double)lut.rgba[i + 2], 0.0, 0);
        CHECK_NEAR((double)lut.rgba[stripIndex(lut, 0, 1, 0) + 1], (double)(uint8_t)(255.f * 1 / 4 + 0.5f), 0);

        // a red bias in the file shows up on the red channel only
        LutStrip biased;
        CHECK(loadCubeLut(writeTempCube(4, true), biased));
        CHECK_NEAR((double)biased.rgba[stripIndex(biased, 0, 2, 2) + 0], 255.0, 0);
    }

    // ------------------------------------------------------------------ .cube, real grading tables
    {
        for (const char* name : {"Anime Shinkai", "Anime Ghibli", "Anime Pastel", "Anime Punchy"}) {
            LutStrip lut;
            const std::string path = assets + "/luts/" + name + ".cube";
            bool ok = loadCubeLut(path, lut);
            CHECK_MSG(ok, std::string("loaded ") + name);
            if (!ok) continue;
            CHECK_NEAR((double)lut.size, 32.0, 0);                                  // LUT_3D_SIZE 32
            CHECK_NEAR((double)lut.rgba.size(), 32.0 * 32 * 32 * 4, 0);
            // sane table: all channels present, alpha fully opaque, and it is not the identity
            LutStrip id = identityLut(32);
            size_t diff = 0;
            for (size_t i = 0; i < lut.rgba.size(); i += 4) {
                if (lut.rgba[i + 3] != 255) { CHECK_MSG(false, "alpha is opaque"); break; }
                if (lut.rgba[i] != id.rgba[i] || lut.rgba[i + 1] != id.rgba[i + 1] || lut.rgba[i + 2] != id.rgba[i + 2]) ++diff;
            }
            CHECK_MSG(diff > lut.rgba.size() / 8, "the grade actually changes a substantial part of the cube");
            CHECK_MSG(diff < lut.rgba.size() / 4, "but not all of it (the table is a grade, not noise)");
        }
    }

    // ------------------------------------------------------------------ .3dl
    {
        LutStrip lut;
        bool ok = load3dlLut(assets + "/luts/Presetpro-Cinematic.3dl", lut);
        CHECK_MSG(ok, "loaded Presetpro-Cinematic.3dl");
        if (ok) {
            CHECK(lut.size >= 2);
            CHECK_NEAR((double)lut.rgba.size(), (double)lut.size * lut.size * lut.size * 4, 0);
            // 10-bit source values are normalised into the 8-bit strip
            int nonZero = 0;
            for (size_t i = 0; i < lut.rgba.size(); i += 4) if (lut.rgba[i] || lut.rgba[i + 1] || lut.rgba[i + 2]) ++nonZero;
            CHECK(nonZero > 0);
        }
    }

    // ------------------------------------------------------------------ LUT images (three.js layout)
    {
        LutStrip neutral;
        CHECK(loadImageLut(assets + "/luts/NeutralLUT.png", neutral));
        if (neutral.size > 0) {
            LutStrip id = identityLut(neutral.size);
            size_t diff = 0;
            for (size_t i = 0; i < neutral.rgba.size() && i < id.rgba.size(); i += 4)
                if (neutral.rgba[i] != id.rgba[i] || neutral.rgba[i + 1] != id.rgba[i + 1] || neutral.rgba[i + 2] != id.rgba[i + 2]) ++diff;
            CHECK_MSG(diff < neutral.rgba.size() / 20, "NeutralLUT is (nearly) an identity table");
        }

        LutStrip night;
        CHECK(loadImageLut(assets + "/luts/NightLUT.png", night));
        CHECK(night.size >= 2);
        CHECK_NEAR((double)night.rgba.size(), (double)night.size * night.size * night.size * 4, 0);

        LutStrip bw;
        CHECK(loadImageLut(assets + "/luts/B&WLUT.png", bw));
        CHECK(bw.size >= 2);

        // dispatch by extension
        LutStrip viaDispatch;
        CHECK(loadLutFile(assets + "/luts/NeutralLUT.png", viaDispatch));
        CHECK(loadLutFile(assets + "/luts/Anime Shinkai.cube", viaDispatch));
        CHECK(!loadLutFile(assets + "/luts/README.md", viaDispatch));      // unsupported extension
    }

    // ------------------------------------------------------------------ failures are reported, not crashed on
    {
        LutStrip lut;
        CHECK(!loadCubeLut("/tmp/definitely_missing.cube", lut));
        CHECK(!loadCubeLut(assets + "/luts/README.md", lut));              // exists, but is not a cube
        CHECK(!loadImageLut("/tmp/definitely_missing.png", lut));
        CHECK(!load3dlLut("/tmp/definitely_missing.3dl", lut));
        // a truncated cube file (size says 32, data is short) is rejected
        std::string path = "/tmp/sakuga_bad.cube";
        { std::ofstream f(path); f << "LUT_3D_SIZE 32\n0 0 0\n1 1 1\n"; }
        CHECK(!loadCubeLut(path, lut));
        // 1D LUTs are not 3D LUTs
        { std::ofstream f(path); f << "LUT_1D_SIZE 16\n0 0 0\n1 1 1\n"; }
        CHECK(!loadCubeLut(path, lut));
    }

    return t_.finish();
}
