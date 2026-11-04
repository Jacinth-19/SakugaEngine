// GPU-free check of ordinary walk poses retargeted to the bundled VRM's actual rig.
#include "Check.h"
#include "anim/MotionMatching.h"
#include "asset/GltfBinary.h"
#include "core/FileUtil.h"
#include "core/Json.h"
#include <glm/gtc/quaternion.hpp>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

int main() {
    SK_TEST("real_rig_retarget_test");
    std::vector<uint8_t> bytes;
    if (!sk::readFile(std::string(SAKUGA_ROOT) + "/assets/models/AvatarSample.vrm", bytes)) {
        CHECK_MSG(false, "could not read bundled VRM"); return t_.finish();
    }
    sk::GlbChunks chunks; std::string error;
    if (!sk::parseGlbContainer(bytes.data(), bytes.size(), chunks, &error)) {
        CHECK_MSG(false, error); return t_.finish();
    }
    auto json = sk::Json::parse(std::string(chunks.json, chunks.json + chunks.jsonSize));
    cgltf_options opt{}; cgltf_data* data = nullptr;
    if (cgltf_parse(&opt, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
        CHECK_MSG(false, "could not parse VRM nodes"); return t_.finish();
    }
    sk::Rig r;
    for (size_t i = 0; i < data->nodes_count; ++i) {
        auto& n = data->nodes[i]; sk::Node x;
        x.name = n.name ? n.name : ""; x.parent = n.parent ? int(n.parent - data->nodes) : -1;
        if (n.has_translation) x.t = {n.translation[0], n.translation[1], n.translation[2]};
        if (n.has_rotation) x.r = {n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]};
        if (n.has_scale) x.s = {n.scale[0], n.scale[1], n.scale[2]};
        r.nodes.push_back(x);
    }
    for (int i = 0; i < sk::kHumanBoneCount; ++i) {
        auto& j = json["extensions"]["VRMC_vrm"]["humanoid"]["humanBones"][sk::humanBoneName((sk::HumanBone)i)];
        r.human[i] = j["node"].integer(-1);
    }
    r.buildOrder(); cgltf_free(data);
    std::string path = std::string(SAKUGA_ROOT) + "/assets/motion/walk1_subject1.bvh";
    sk::MotionDatabase db;
    if (!db.buildFromFiles(r, {path}, "", 0, &error)) { CHECK_MSG(false, error); return t_.finish(); }
    sk::BvhClip clip;
    if (!sk::loadBvh(path, clip, &error)) { CHECK_MSG(false, error); return t_.finish(); }
    int hips = r.bone(sk::HumanBone::Hips), neck = r.bone(sk::HumanBone::Neck);
    int sourceHips = clip.findJoint("Hips"), sourceNeck = clip.findJoint("Neck");
    if (hips < 0 || neck < 0 || sourceHips < 0 || sourceNeck < 0) {
        CHECK_MSG(false, "expected torso bones missing"); return t_.finish();
    }
    // Preserve the source pose instead of automatically straightening intentional bends.
    for (int f : {100, 400, 700, 1000, 1500}) {
        CHECK(f < db.frameCount); if (f >= db.frameCount) continue;
        auto pose = r.restPose();
        for (int b = 0; b < sk::kHumanBoneCount; ++b)
            if (db.mapped[b]) pose.r[r.human[b]] = db.localRot[(size_t)f * sk::kHumanBoneCount + b];
        pose.t[hips] = db.hipsLocalPos[f];
        std::vector<glm::mat4> globals; r.globals(pose, globals);
        std::vector<glm::vec3> p; std::vector<glm::quat> q; clip.fk(f, 0.01f, p, q);
        glm::vec3 source = glm::inverse(glm::angleAxis(db.rootYaw[f], glm::vec3(0,1,0))) * (p[sourceNeck] - p[sourceHips]);
        glm::vec3 target = glm::vec3(globals[neck][3]) - glm::vec3(globals[hips][3]);
        CHECK(glm::length(source) > 0.01f); CHECK(glm::length(target) > 0.01f);
        CHECK_MSG(glm::dot(glm::normalize(source), glm::normalize(target)) > 0.98f, "retargeted torso follows source pose");
    }
    return t_.finish();
}
