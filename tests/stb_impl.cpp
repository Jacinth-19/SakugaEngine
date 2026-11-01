// One translation unit with the stb_image implementation, so the tests can decode the vendored LUT PNGs (asset/Lut.cpp)
// and the reference screenshots (pixel_diff_test) without pulling in the renderer.
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
