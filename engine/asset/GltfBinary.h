#pragma once
// glTF binary (GLB / VRM) container parsing: the 12-byte header and the JSON/BIN chunks. Kept separate from the loader so
// the container can be validated (and unit tested) without a renderer; loadGltfModel() uses it to fail with a precise
// message before handing the bytes to cgltf.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace sk {

constexpr uint32_t kGlbMagic = 0x46546C67u;   // 'glTF'
constexpr uint32_t kGlbChunkJson = 0x4E4F534Au;
constexpr uint32_t kGlbChunkBin = 0x004E4942u;

struct GlbChunks {
    uint32_t version = 0;
    uint32_t declaredLength = 0;
    const uint8_t* json = nullptr;
    size_t jsonSize = 0;
    const uint8_t* bin = nullptr;
    size_t binSize = 0;
    int chunkCount = 0;
};

namespace glb_detail {
inline uint32_t readU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline bool fail(std::string* error, const char* msg) { if (error) *error = msg; return false; }
}  // namespace glb_detail

// Validates the GLB container and locates its chunks. False (+ a human readable reason) for a truncated header, a bad
// magic, a length field beyond the file, or a chunk that runs past the end - all of which cgltf would otherwise report
// far away from the file that caused it.
inline bool parseGlbContainer(const uint8_t* data, size_t size, GlbChunks& out, std::string* error = nullptr) {
    using namespace glb_detail;
    out = GlbChunks{};
    if (!data) return fail(error, "empty file");
    if (size < 12) return fail(error, "file shorter than the GLB header");
    if (readU32(data) != kGlbMagic) return fail(error, "not a GLB container (bad magic)");
    out.version = readU32(data + 4);
    if (out.version != 2) { if (error) *error = "unsupported GLB version " + std::to_string(out.version); return false; }
    out.declaredLength = readU32(data + 8);
    if (out.declaredLength > size) return fail(error, "GLB declares more bytes than the file contains (truncated)");

    size_t off = 12;
    while (off + 8 <= (size_t)out.declaredLength) {
        uint32_t chunkLen = readU32(data + off), chunkType = readU32(data + off + 4);
        off += 8;
        if (off + chunkLen > (size_t)out.declaredLength) return fail(error, "GLB chunk runs past the end of the container");
        if (chunkType == kGlbChunkJson) {
            if (!out.json) { out.json = data + off; out.jsonSize = chunkLen; }
        } else if (chunkType == kGlbChunkBin) {
            if (!out.bin) { out.bin = data + off; out.binSize = chunkLen; }
        }
        ++out.chunkCount;
        off += chunkLen;
    }
    if (!out.json || out.jsonSize == 0) return fail(error, "GLB has no JSON chunk");
    return true;
}

}  // namespace sk
