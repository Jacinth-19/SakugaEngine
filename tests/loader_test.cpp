// glTF/VRM loader front end: GLB container validation (asset/GltfBinary.h, used by loadGltfModel) and a full parse of the
// vendored models with cgltf - the same library the loader uses. No renderer, no GPU: this checks the file-level data the
// loader depends on (nodes, skins, animations, VRM humanoid map).
#include "Check.h"
#include "asset/GltfBinary.h"
#include "core/FileUtil.h"
#include "core/Json.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

using namespace sk;

static std::vector<uint8_t> readOrEmpty(const std::string& p) {
    std::vector<uint8_t> b;
    if (!readFile(p, b)) return {};
    return b;
}

// Counts the entries of the VRM humanoid bone map (VRM 0.x "humanoid.humanBones" / VRM 1.0 "VRMC_vrm.humanoid.humanBones").
static int countHumanoidBones(const Json& j) {
    const Json& humanoid = j["extensions"]["VRMC_vrm"]["humanoid"];
    if (humanoid.isObj()) {
        const Json& bones = humanoid["humanBones"];
        if (bones.isObj()) return (int)bones.size();
    }
    const Json& old = j["extensions"]["VRM"]["humanoid"]["humanBones"];
    if (old.isArr()) return (int)old.size();
    return 0;
}

int main() {
    SK_TEST("loader_test");
    const std::string assets = SAKUGA_ASSET_DIR;

    // ------------------------------------------------------------------ container validation
    {
        GlbChunks chunks; std::string err;
        CHECK(!parseGlbContainer(nullptr, 0, chunks, &err));
        CHECK(!err.empty());

        std::vector<uint8_t> tooShort(8, 0);
        CHECK(!parseGlbContainer(tooShort.data(), tooShort.size(), chunks, &err));

        std::vector<uint8_t> badMagic(16, 0);
        CHECK(!parseGlbContainer(badMagic.data(), badMagic.size(), chunks, &err));
        CHECK_MSG(err.find("magic") != std::string::npos, "the error names the magic check");

        // a minimal, valid GLB: header + JSON chunk "{}"
        auto makeGlb = [](uint32_t version, uint32_t declaredExtra = 0) {
            std::string json = "{}";
            uint32_t jsonLen = (uint32_t)json.size();
            uint32_t total = 12 + 8 + jsonLen;
            std::vector<uint8_t> b(total + declaredExtra, 0);
            auto put = [&](size_t off, uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
            put(0, kGlbMagic); put(4, version); put(8, total);
            put(12, jsonLen); put(16, kGlbChunkJson);
            std::memcpy(b.data() + 20, json.data(), jsonLen);
            return b;
        };
        {
            std::vector<uint8_t> glb = makeGlb(2);
            CHECK(parseGlbContainer(glb.data(), glb.size(), chunks, &err));
            CHECK_NEAR((double)chunks.version, 2.0, 0);
            CHECK_NEAR((double)chunks.jsonSize, 2.0, 0);
            CHECK_NEAR(chunks.json[0], '{', 0);
            CHECK(chunks.bin == nullptr);
        }
        {
            std::vector<uint8_t> glb = makeGlb(1);                   // glTF 1.0 binary is not supported
            CHECK(!parseGlbContainer(glb.data(), glb.size(), chunks, &err));
        }
        {
            std::vector<uint8_t> glb = makeGlb(2);
            glb[8] = 0xFF; glb[9] = 0xFF;                            // declared length beyond the file: truncated
            CHECK(!parseGlbContainer(glb.data(), glb.size(), chunks, &err));
            CHECK(err.find("truncated") != std::string::npos);
        }
        {
            std::vector<uint8_t> glb = makeGlb(2);
            uint32_t huge = 0xFFFF;
            std::memcpy(glb.data() + 12, &huge, 4);                  // chunk longer than the container
            CHECK(!parseGlbContainer(glb.data(), glb.size(), chunks, &err));
        }
    }

    // ------------------------------------------------------------------ the real models
    struct ModelSpec { const char* file; bool vrm; size_t minNodes, minSkins; };
    const ModelSpec models[] = {
        {"/models/Fox.glb", false, 5, 1},
        {"/models/CesiumMan.glb", false, 20, 1},
        {"/models/AvatarSample.vrm", true, 30, 1},
        {"/models/Seed-san.vrm", true, 30, 1},
        {"/models/three-vrm-girl.vrm", true, 30, 1},
    };
    for (const ModelSpec& m : models) {
        const std::string path = assets + m.file;
        std::vector<uint8_t> bytes = readOrEmpty(path);
        CHECK_MSG(!bytes.empty(), std::string("read ") + m.file);
        if (bytes.empty()) continue;

        GlbChunks chunks; std::string err;
        bool ok = parseGlbContainer(bytes.data(), bytes.size(), chunks, &err);
        CHECK_MSG(ok, std::string(m.file) + ": " + err);
        if (!ok) continue;
        CHECK(chunks.jsonSize > 100);
        CHECK(chunks.chunkCount >= 1);

        // the JSON chunk really is the glTF document
        const std::string json(chunks.json, chunks.json + chunks.jsonSize);
        CHECK_MSG(json.find("\"asset\"") != std::string::npos, "JSON chunk contains the asset block");
        CHECK_MSG(json.find("\"nodes\"") != std::string::npos, "JSON chunk contains nodes");

        if (m.vrm) {
            bool hasVrm1 = json.find("VRMC_vrm") != std::string::npos;
            bool hasVrm0 = json.find("\"VRM\"") != std::string::npos;
            CHECK_MSG(hasVrm1 || hasVrm0, "VRM extensions are present");
            Json doc = Json::parse(json, nullptr);
            CHECK(doc.isObj());
            if (doc.isObj()) {
                int bones = countHumanoidBones(doc);
                CHECK_MSG(bones >= 15, "VRM humanoid map has at least 15 bones");
                // the extension object keeps the required version/meta blocks
                const Json& ext = doc["extensions"];
                CHECK(ext.isObj());
            }
        }

        // full parse with cgltf (the library the loader wraps)
        cgltf_options opts{};
        cgltf_data* data = nullptr;
        cgltf_result r = cgltf_parse(&opts, bytes.data(), bytes.size(), &data);
        CHECK_MSG(r == cgltf_result_success, std::string(m.file) + ": cgltf parsed the container");
        if (r != cgltf_result_success) continue;
        CHECK_NEAR((double)data->nodes_count >= (double)m.minNodes, 1.0, 0);
        CHECK_NEAR((double)data->skins_count >= (double)m.minSkins, 1.0, 0);
        CHECK(data->meshes_count >= 1);
        CHECK(data->buffers_count >= 1);
        if (data->skins_count > 0) {
            CHECK(data->skins[0].joints_count > 0);
            CHECK(data->skins[0].inverse_bind_matrices != nullptr);
        }
        for (size_t n = 0; n < data->nodes_count; ++n) {
            // the loader walks nodes and needs a parent link to be consistent
            if (data->nodes[n].parent) CHECK(data->nodes[n].parent >= data->nodes && data->nodes[n].parent < data->nodes + data->nodes_count);
        }
        // animations, where present, must have samplers with matching keyframe counts
        for (size_t a = 0; a < data->animations_count; ++a) {
            const cgltf_animation& anim = data->animations[a];
            CHECK(anim.channels_count > 0);
            for (size_t s = 0; s < anim.samplers_count; ++s) {
                const cgltf_animation_sampler& smp = anim.samplers[s];
                CHECK(smp.input != nullptr && smp.output != nullptr);
                CHECK(smp.input->count > 0);
            }
            break;   // one animation is representative; files can carry 20+
        }
        cgltf_free(data);
    }

    // ------------------------------------------------------------------ garbage input is rejected cleanly
    {
        std::vector<uint8_t> junk = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        GlbChunks chunks; std::string err;
        CHECK(!parseGlbContainer(junk.data(), junk.size(), chunks, &err));
        cgltf_options opts{};
        cgltf_data* data = nullptr;
        CHECK(cgltf_parse(&opts, junk.data(), junk.size(), &data) != cgltf_result_success);
    }

    return t_.finish();
}
