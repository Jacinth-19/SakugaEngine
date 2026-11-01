#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace sk {
// A 3D colour lookup table laid out as a 2D strip (size*size wide, size high; slice = blue, row = green, column = red), RGBA8.
struct LutStrip { int size = 0; std::vector<uint8_t> rgba; };
// Parses an Adobe/Resolve .cube file (LUT_3D_SIZE, DOMAIN_MIN/MAX; red varies fastest). Returns false on failure.
bool loadCubeLut(const std::string& path, LutStrip& out);
LutStrip identityLut(int size = 16);
// Adobe/Autodesk .3dl (blue index varies fastest, like three.js' LUT3dlLoader)
bool load3dlLut(const std::string& path, LutStrip& out);
// LUT image: horizontal strip (size*size x size, as in three.js examples/luts/*.png) or vertical stack of slices
bool loadImageLut(const std::string& path, LutStrip& out);
// Dispatches on the extension (.cube / .3dl / .png)
bool loadLutFile(const std::string& path, LutStrip& out);
}
