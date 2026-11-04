#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_HDR
#include <stb/stb_image.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include "asset/GltfLoader.h"
#include "asset/GltfBinary.h"
#include "core/FileUtil.h"
#include "core/Json.h"
#include "core/Log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <filesystem>
#include <map>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cctype>
#include <functional>

namespace sk {
namespace {
// Rasterises the face mesh into UV space and bakes a left/right-light "SDF" from the head-local X coordinate.
// G (used when the light comes from +X of the head) rises towards +X, R (light from -X) rises towards -X, so a side light first
// reveals the cheek it comes from and the terminator then sweeps across the face as the light turns to the front.
Texture* bakeFaceSDF(Renderer& R, const MeshData& md, const std::string& name) {
    if (md.vertices.empty()) return nullptr;
    float ymin = 1e30f, ymax = -1e30f;
    for (auto& v : md.vertices) { ymin = std::min(ymin, v.pos.y); ymax = std::max(ymax, v.pos.y); }
    std::vector<float> xs;
    for (auto& v : md.vertices) if (v.pos.y > ymin + 0.4f * (ymax - ymin)) xs.push_back(v.pos.x);   // skip the neck
    if (xs.size() < 16) return nullptr;
    std::sort(xs.begin(), xs.end());
    float x0 = xs[xs.size() * 3 / 100], x1 = xs[xs.size() * 97 / 100], cx = 0.5f * (x0 + x1), hw = std::max(0.5f * (x1 - x0), 1e-4f);
    const int W = 1024, H = 1024;
    std::vector<float> val((size_t)W * H, -1.f);                       // head-local X in [-1, 1], -1 marker = uncovered (store as 2 = empty)
    std::fill(val.begin(), val.end(), 2.f);
    for (size_t t = 0; t + 2 < md.indices.size(); t += 3) {
        const Vertex* v[3] = {&md.vertices[md.indices[t]], &md.vertices[md.indices[t + 1]], &md.vertices[md.indices[t + 2]]};
        glm::vec2 p[3]; float xn[3];
        for (int k = 0; k < 3; ++k) { p[k] = glm::vec2(v[k]->uv.x * W, v[k]->uv.y * H); xn[k] = glm::clamp((v[k]->pos.x - cx) / hw, -1.f, 1.f); }
        glm::vec2 mn = glm::min(p[0], glm::min(p[1], p[2])), mx = glm::max(p[0], glm::max(p[1], p[2]));
        int ix0 = std::max(0, (int)std::floor(mn.x) - 1), ix1 = std::min(W - 1, (int)std::ceil(mx.x) + 1), iy0 = std::max(0, (int)std::floor(mn.y) - 1), iy1 = std::min(H - 1, (int)std::ceil(mx.y) + 1);
        float den = (p[1].y - p[2].y) * (p[0].x - p[2].x) + (p[2].x - p[1].x) * (p[0].y - p[2].y);
        if (std::fabs(den) < 1e-8f) continue;
        for (int y = iy0; y <= iy1; ++y) for (int x = ix0; x <= ix1; ++x) {
            glm::vec2 q(x + 0.5f, y + 0.5f);
            float a = ((p[1].y - p[2].y) * (q.x - p[2].x) + (p[2].x - p[1].x) * (q.y - p[2].y)) / den;
            float b = ((p[2].y - p[0].y) * (q.x - p[2].x) + (p[0].x - p[2].x) * (q.y - p[2].y)) / den, c = 1.f - a - b;
            const float e = -0.02f; if (a < e || b < e || c < e) continue;
            val[(size_t)y * W + x] = a * xn[0] + b * xn[1] + c * xn[2];
        }
    }
    for (int pass = 0; pass < 6; ++pass) {                             // dilate so bilinear/mip sampling never reads empty texels
        std::vector<float> nv = val;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            if (val[(size_t)y * W + x] < 1.5f) continue;
            float acc = 0; int n = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue; float q = val[(size_t)yy * W + xx]; if (q < 1.5f) { acc += q; ++n; } }
            if (n) nv[(size_t)y * W + x] = acc / n;
        }
        val.swap(nv);
    }
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (size_t i = 0; i < val.size(); ++i) {
        float x = val[i] > 1.5f ? 0.f : val[i];
        float g = glm::clamp((x + 1.f) * 0.5f, 0.f, 1.f), r = 1.f - g;
        px[i * 4] = (uint8_t)(r * 255.f + .5f); px[i * 4 + 1] = (uint8_t)(g * 255.f + .5f); px[i * 4 + 2] = 0; px[i * 4 + 3] = 255;
    }
    return R.createTexture(name + "/faceSDF", px.data(), W, H, false, true, false);
}
}
}

namespace sk {
namespace {

struct ImageCache {
    Renderer& r; const cgltf_data* data; std::string dir; int maxSize;
    std::map<std::pair<int, bool>, Texture*> cache;
    Texture* get(const cgltf_texture* tex, bool srgb) {
        if (!tex || !tex->image) return nullptr;
        int idx = (int)(tex->image - data->images);
        auto key = std::make_pair(idx, srgb);
        auto it = cache.find(key); if (it != cache.end()) return it->second;
        const cgltf_image* im = tex->image;
        std::vector<uint8_t> file; const uint8_t* ptr = nullptr; size_t len = 0;
        if (im->buffer_view) { ptr = (const uint8_t*)im->buffer_view->buffer->data + im->buffer_view->offset; len = im->buffer_view->size; }
        else if (im->uri && std::strncmp(im->uri, "data:", 5) != 0) {
            std::string uri = im->uri; cgltf_decode_uri(uri.data());
            if (!readFile(dir + "/" + uri.c_str(), file)) { SK_WARN("gltf: missing image %s", uri.c_str()); return nullptr; }
            ptr = file.data(); len = file.size();
        } else return nullptr;
        int w, h, c;
        uint8_t* px = stbi_load_from_memory(ptr, (int)len, &w, &h, &c, 4);
        if (!px) { SK_WARN("gltf: failed to decode image %d", idx); return nullptr; }
        std::vector<uint8_t> buf(px, px + (size_t)w * h * 4); stbi_image_free(px);
        while (std::max(w, h) > maxSize) {                      // 2x2 box downsample
            int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            std::vector<uint8_t> o((size_t)nw * nh * 4);
            for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int k = 0; k < 4; ++k) {
                int x1 = std::min(2 * x + 1, w - 1), y1 = std::min(2 * y + 1, h - 1);
                int s = buf[((size_t)(2 * y) * w + 2 * x) * 4 + k] + buf[((size_t)(2 * y) * w + x1) * 4 + k] + buf[((size_t)y1 * w + 2 * x) * 4 + k] + buf[((size_t)y1 * w + x1) * 4 + k];
                o[((size_t)y * nw + x) * 4 + k] = (uint8_t)(s / 4);
            }
            buf.swap(o); w = nw; h = nh;
        }
        Texture* t = r.createTexture(im->name ? im->name : "image" + std::to_string(idx), buf.data(), w, h, srgb);
        cache[key] = t; return t;
    }
};

glm::vec3 jvec3(const Json& j, glm::vec3 d) { return j.size() >= 3 ? glm::vec3((float)j[(size_t)0].num(), (float)j[(size_t)1].num(), (float)j[(size_t)2].num()) : d; }

const cgltf_extension* findExt(const cgltf_extension* e, size_t n, const char* name) {
    for (size_t i = 0; i < n; ++i) if (e[i].name && !std::strcmp(e[i].name, name)) return &e[i];
    return nullptr;
}
bool contains(const std::string& s, const char* sub) {
    std::string a = s, b = sub; for (auto& c : a) c = (char)std::tolower(c); for (auto& c : b) c = (char)std::tolower(c);
    return a.find(b) != std::string::npos;
}
}  // namespace

namespace {
// GLB/VRM container pre-flight: cgltf reports a truncated or corrupt binary deep inside its own parse, far away from
// the file that caused it. Validating the header/chunks here gives a precise message (and is unit tested directly).
bool validateGlbFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext != ".glb" && ext != ".vrm") return true;                 // .gltf is JSON, nothing to check
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) { SK_ERR("gltf: cannot read %s", path.c_str()); return false; }
    GlbChunks chunks; std::string err;
    if (!parseGlbContainer(bytes.data(), bytes.size(), chunks, &err)) { SK_ERR("gltf: %s: %s", path.c_str(), err.c_str()); return false; }
    return true;
}
}  // namespace

std::unique_ptr<Model> loadGltfModel(Renderer& R, const StyleTextures& style, const std::string& path, const GltfOptions& opt) {
    if (!validateGlbFile(path)) return nullptr;
    cgltf_options co{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&co, path.c_str(), &data) != cgltf_result_success) { SK_ERR("gltf: cannot parse %s", path.c_str()); return nullptr; }
    if (cgltf_load_buffers(&co, data, path.c_str()) != cgltf_result_success) { SK_ERR("gltf: cannot load buffers for %s", path.c_str()); cgltf_free(data); return nullptr; }

    auto model = std::make_unique<Model>();
    model->path = path; model->name = std::filesystem::path(path).stem().string();
    Rig& rig = model->rig;
    ImageCache images{R, data, std::filesystem::path(path).parent_path().string(), opt.maxTextureSize, {}};

    // ---------------------------------------------------------------- nodes
    rig.nodes.resize(data->nodes_count);
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        Node& o = rig.nodes[i]; o.name = n.name ? n.name : "node" + std::to_string(i);
        o.parent = n.parent ? (int)(n.parent - data->nodes) : -1;
        if (n.has_matrix) {
            glm::mat4 m; std::memcpy(&m, n.matrix, 64);
            glm::vec3 sc, tr, sk; glm::vec4 pe; glm::quat q;
            glm::decompose(m, sc, q, tr, sk, pe); o.t = tr; o.r = glm::normalize(q); o.s = sc;
        } else {
            if (n.has_translation) o.t = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) o.r = glm::normalize(glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]));
            if (n.has_scale) o.s = {n.scale[0], n.scale[1], n.scale[2]};
        }
    }
    const cgltf_extension* vrm0Ext = findExt(data->data_extensions, data->data_extensions_count, "VRM");
    if (vrm0Ext) {   // VRM 0.x models face -Z: rotate the roots by 180 degrees about Y so every humanoid faces +Z like VRM 1.0
        glm::quat flip = glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0));
        for (Node& n : rig.nodes) if (n.parent < 0) { n.t = flip * n.t; n.r = glm::normalize(flip * n.r); }
    }
    rig.buildOrder();
    std::vector<glm::mat4> restGlobals; rig.globals(rig.restPose(), restGlobals);

    // ---------------------------------------------------------------- skins
    for (size_t i = 0; i < data->skins_count; ++i) {
        const cgltf_skin& s = data->skins[i]; Skin sk;
        for (size_t j = 0; j < s.joints_count; ++j) sk.joints.push_back((int)(s.joints[j] - data->nodes));
        sk.inverseBind.assign(s.joints_count, glm::mat4(1.f));
        if (s.inverse_bind_matrices) for (size_t j = 0; j < s.joints_count; ++j) cgltf_accessor_read_float(s.inverse_bind_matrices, j, &sk.inverseBind[j][0][0], 16);
        rig.skins.push_back(std::move(sk));
    }

    // ---------------------------------------------------------------- VRM (1.0 and 0.x)
    auto nodeOfMesh = [&](int meshIdx, std::vector<int>& out) { for (size_t ni = 0; ni < data->nodes_count; ++ni) if (data->nodes[ni].mesh && (int)(data->nodes[ni].mesh - data->meshes) == meshIdx) out.push_back((int)ni); };
    auto addVrm1Expr = [&](const std::string& name, const Json& e) {
        Expression ex; ex.name = name; ex.binary = e["isBinary"].boolean(false);
        for (const Json& b : e["morphTargetBinds"].a) {
            int node = b["node"].integer(-1); if (node < 0 || node >= (int)data->nodes_count || !data->nodes[node].mesh) continue;
            ex.binds.push_back({(int)(data->nodes[node].mesh - data->meshes), b["index"].integer(0), (float)b["weight"].num(1.0)});
        }
        model->expressions.push_back(std::move(ex));
    };
    if (const cgltf_extension* vrm = findExt(data->data_extensions, data->data_extensions_count, "VRMC_vrm")) {
        Json j = Json::parse(vrm->data);
        const Json& hb = j["humanoid"]["humanBones"];
        for (auto& kv : hb.o) { int b = humanBoneFromVrmName(kv.first); if (b >= 0) rig.human[b] = kv.second["node"].integer(-1);
                                if (kv.first == "leftEye") rig.eyeL = kv.second["node"].integer(-1); if (kv.first == "rightEye") rig.eyeR = kv.second["node"].integer(-1); }
        model->isVRM = rig.isHumanoid(); model->vrmVersion = 1;
        model->license = j["meta"]["licenseUrl"].str(j["meta"]["avatarPermission"].str(""));
        const Json& pre = j["expressions"]["preset"];
        for (auto& kv : pre.o) addVrm1Expr(kv.first, kv.second);
        const Json& la = j["lookAt"]["offsetFromHeadBone"]; if (la.size() >= 3) model->lookAtOffset = jvec3(la, model->lookAtOffset);
        SK_LOG("vrm 1.0: humanoid map loaded (hips=%d, eyes=%d/%d), %zu expressions", rig.human[(int)HumanBone::Hips], rig.eyeL, rig.eyeR, model->expressions.size());
    } else if (vrm0Ext) {
        Json j = Json::parse(vrm0Ext->data);
        for (const Json& b : j["humanoid"]["humanBones"].a) {
            std::string nm = b["bone"].str(); int node = b["node"].integer(-1);
            int hbIdx = humanBoneFromVrmName(nm); if (hbIdx >= 0) rig.human[hbIdx] = node;
            if (nm == "leftEye") rig.eyeL = node; if (nm == "rightEye") rig.eyeR = node;
        }
        model->isVRM = rig.isHumanoid(); model->vrmVersion = 0;
        model->license = j["meta"]["licenseName"].str("") + " " + j["meta"]["otherLicenseUrl"].str("");
        static const std::pair<const char*, const char*> kMap[] = {{"blink", "blink"}, {"blink_l", "blinkLeft"}, {"blink_r", "blinkRight"}, {"joy", "happy"}, {"angry", "angry"},
            {"sorrow", "sad"}, {"fun", "relaxed"}, {"a", "aa"}, {"i", "ih"}, {"u", "ou"}, {"e", "ee"}, {"o", "oh"}, {"neutral", "neutral"}, {"lookup", "lookUp"}, {"lookdown", "lookDown"}, {"lookleft", "lookLeft"}, {"lookright", "lookRight"}};
        for (const Json& g : j["blendShapeMaster"]["blendShapeGroups"].a) {
            std::string preset = g["presetName"].str(); const char* mapped = nullptr;
            for (auto& m : kMap) if (preset == m.first) mapped = m.second;
            if (!mapped) continue;
            Expression ex; ex.name = mapped;
            for (const Json& b : g["binds"].a) ex.binds.push_back({b["mesh"].integer(-1), b["index"].integer(0), (float)b["weight"].num(100.0) / 100.f});
            model->expressions.push_back(std::move(ex));
        }
        model->lookAtOffset = glm::vec3(0, 0.07f, 0.08f);
        // 0.x secondary animation -> the same SpringData the 1.0 extension produces (each bone with children simulates toward its first child)
        const Json& sa = j["secondaryAnimation"];
        for (const Json& cg : sa["colliderGroups"].a) {
            std::vector<int> ids;
            for (const Json& cl : cg["colliders"].a) {
                SpringCollider sc; sc.node = cg["node"].integer(-1); sc.radius = (float)cl["radius"].num(0.05);
                const Json& o = cl["offset"]; sc.offset = glm::vec3((float)o["x"].num(0), (float)o["y"].num(0), -(float)o["z"].num(0));   // 0.x offsets are stored in Unity's left-handed frame (z negated), node local space is unaffected by the root flip
                ids.push_back((int)model->springs.colliders.size()); model->springs.colliders.push_back(sc);
            }
            model->springs.groups.push_back(ids);
        }
        for (const Json& bg : sa["boneGroups"].a) {
            SpringChain ch; ch.name = bg["comment"].str("spring");
            float stiff = (float)bg["stiffiness"].num(bg["stiffness"].num(1.0)), grav = (float)bg["gravityPower"].num(0), drag = (float)bg["dragForce"].num(0.4), hit = (float)bg["hitRadius"].num(0.02);
            glm::vec3 gd = glm::vec3(-(float)bg["gravityDir"]["x"].num(0), (float)bg["gravityDir"]["y"].num(-1), (float)bg["gravityDir"]["z"].num(0));   // Unity frame -> flipped glTF world
            for (const Json& cgi : bg["colliderGroups"].a) ch.colliderGroups.push_back(cgi.integer(-1));
            std::function<void(int)> addTree = [&](int node) {
                if (node < 0 || node >= (int)rig.nodes.size()) return;
                SpringJoint sj; sj.node = node; sj.stiffness = stiff; sj.drag = drag; sj.gravityPower = grav; sj.hitRadius = hit; sj.gravityDir = gd;
                const cgltf_node& gn = data->nodes[node]; if (gn.children_count) sj.child = (int)(gn.children[0] - data->nodes);
                ch.joints.push_back(sj);
                for (size_t k = 0; k < gn.children_count; ++k) addTree((int)(gn.children[k] - data->nodes));
            };
            for (const Json& root : bg["bones"].a) addTree(root.integer(-1));
            if (!ch.joints.empty()) model->springs.chains.push_back(std::move(ch));
        }
        SK_LOG("vrm 0.x: humanoid map loaded (hips=%d, eyes=%d/%d), %zu expressions, %zu spring groups (VRM0 root flipped to +Z)", rig.human[(int)HumanBone::Hips], rig.eyeL, rig.eyeR, model->expressions.size(), model->springs.chains.size());
    }
    if (const cgltf_extension* sb = findExt(data->data_extensions, data->data_extensions_count, "VRMC_springBone")) {
        Json j = Json::parse(sb->data);
        for (const Json& c : j["colliders"].a) {
            SpringCollider sc; sc.node = c["node"].integer(-1);
            const Json& sh = c["shape"];
            if (sh.has("sphere")) { sc.offset = jvec3(sh["sphere"]["offset"], glm::vec3(0)); sc.radius = (float)sh["sphere"]["radius"].num(0.05); }
            else if (sh.has("capsule")) { sc.capsule = true; sc.offset = jvec3(sh["capsule"]["offset"], glm::vec3(0)); sc.tail = jvec3(sh["capsule"]["tail"], glm::vec3(0)); sc.radius = (float)sh["capsule"]["radius"].num(0.05); }
            model->springs.colliders.push_back(sc);
        }
        for (const Json& g : j["colliderGroups"].a) { std::vector<int> ids; for (const Json& k : g["colliders"].a) ids.push_back(k.integer(-1)); model->springs.groups.push_back(ids); }
        for (const Json& sp : j["springs"].a) {
            SpringChain ch; ch.name = sp["name"].str("spring");
            for (const Json& k : sp["colliderGroups"].a) ch.colliderGroups.push_back(k.integer(-1));
            const auto& js = sp["joints"].a;
            for (size_t k = 0; k < js.size(); ++k) {
                SpringJoint sj; sj.node = js[k]["node"].integer(-1); sj.child = k + 1 < js.size() ? js[k + 1]["node"].integer(-1) : -1;
                sj.hitRadius = (float)js[k]["hitRadius"].num(0.02); sj.stiffness = (float)js[k]["stiffness"].num(1.0); sj.gravityPower = (float)js[k]["gravityPower"].num(0.0);
                sj.drag = (float)js[k]["dragForce"].num(0.5); sj.gravityDir = jvec3(js[k]["gravityDir"], glm::vec3(0, -1, 0));
                ch.joints.push_back(sj);
            }
            if (!ch.joints.empty()) model->springs.chains.push_back(std::move(ch));
        }
        SK_LOG("vrm: %zu spring chains, %zu colliders", model->springs.chains.size(), model->springs.colliders.size());
    }

    // VRM node constraints (twist / aim helpers: sleeves, roll bones, thigh aim)
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        const cgltf_extension* ce = findExt(n.extensions, n.extensions_count, "VRMC_node_constraint");
        if (!ce) continue;
        Json j = Json::parse(ce->data);
        const Json& c = j["constraint"];
        NodeConstraint nc; nc.node = (int)i;
        auto axisIdx = [](const std::string& a) {
            if (a == "PositiveX") return 0; if (a == "NegativeX") return 1; if (a == "PositiveY") return 2;
            if (a == "NegativeY") return 3; if (a == "PositiveZ") return 4; return 5; };
        if (c.has("roll")) { const Json& r = c["roll"]; nc.type = NodeConstraint::Roll; nc.source = r["source"].integer(-1); nc.weight = (float)r["weight"].num(1.0);
            std::string a = r["rollAxis"].str("X"); nc.axis = a == "Y" ? 1 : a == "Z" ? 2 : 0; }
        else if (c.has("aim")) { const Json& r = c["aim"]; nc.type = NodeConstraint::Aim; nc.source = r["source"].integer(-1); nc.weight = (float)r["weight"].num(1.0); nc.axis = axisIdx(r["aimAxis"].str("PositiveX")); }
        else if (c.has("rotation")) { const Json& r = c["rotation"]; nc.type = NodeConstraint::Rotation; nc.source = r["source"].integer(-1); nc.weight = (float)r["weight"].num(1.0); }
        else continue;
        rig.constraints.push_back(nc);
    }
    if (!rig.constraints.empty()) SK_LOG("vrm: %zu node constraints", rig.constraints.size());

    // ---------------------------------------------------------------- materials
    for (size_t i = 0; i < data->materials_count; ++i) {
        const cgltf_material& gm = data->materials[i];
        Material* m = R.createMaterial(gm.name ? gm.name : "material" + std::to_string(i));
        MaterialParams& p = m->params;
        std::string name = m->name;
        const cgltf_pbr_metallic_roughness& pbr = gm.pbr_metallic_roughness;
        p.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
        m->tex[TEX_BASE] = images.get(pbr.base_color_texture.texture, true);
        if (!m->tex[TEX_BASE]) m->tex[TEX_BASE] = R.whiteTex();
        if (gm.normal_texture.texture) { m->tex[TEX_NORMAL] = images.get(gm.normal_texture.texture, false); p.normalStrength = 1.f; }
        if (!m->tex[TEX_NORMAL]) m->tex[TEX_NORMAL] = R.bumpTex();
        if (gm.emissive_texture.texture) { m->tex[TEX_EMISSION] = images.get(gm.emissive_texture.texture, true); }
        glm::vec3 em(gm.emissive_factor[0], gm.emissive_factor[1], gm.emissive_factor[2]);
        if (glm::length(em) > 0.001f) { p.emissionColor = glm::vec4(em, 1.f); p.emissionStrength = 1.f; if (!gm.emissive_texture.texture) m->tex[TEX_EMISSION] = R.whiteTex(); }
        p.doubleSided = gm.double_sided ? 1.f : 0.f;
        {   // glTF BLEND -> real alpha blending (sorted, no depth write). Hair cards stay alpha-tested: blended hair sorts badly.
            bool blend = gm.alpha_mode == cgltf_alpha_mode_blend;
            std::string ln = name; for (auto& ch : ln) ch = (char)std::tolower((unsigned char)ch);
            bool hairLike = ln.find("hair") != std::string::npos;
            m->transparent = blend && !hairLike;
            p.alphaClip = gm.alpha_mode == cgltf_alpha_mode_mask ? gm.alpha_cutoff : (blend ? (m->transparent ? 0.f : 0.35f) : 0.f);
            if (m->transparent) m->castShadow = false;
        }
        // metallic / roughness -> the shader's _P texture is authored data, so approximate with constants
        p.metallicScale = std::clamp(pbr.metallic_factor, 0.f, 1.f) * 0.5f;

        // --- stylisation defaults (the "look" of the engine)
        m->tex[TEX_RAMP] = style.ramp; m->tex[TEX_SPEC_RAMP] = style.specRamp; m->tex[TEX_LUT] = style.lut;
        m->tex[TEX_MATCAP] = style.matcap; m->tex[TEX_FLOW] = style.flow;
        p.outlineAlpha = opt.outlines ? 1.f : 0.f;
        p.outlineWidth = 0.0035f * opt.outlineWidthScale;
        p.shadowColor = glm::vec4(0.74f, 0.66f, 0.80f, 1.f);
        p.rampBlend = 0.8f; p.rampFeather = 0.04f; p.rampThreshold = 0.56f; p.realtimeShadowStrength = 0.9f;
        p.pbrBaseBlend = 0.4f;                    // NPR-dominant lit side, PBR/IBL only adds tonal variation and sheen
        p.occlusionStrength = 0.35f;
        p.matCapStrength = 0.12f; p.specRampStrength = 0.35f; p.smoothnessMin = 0.1f; p.smoothnessMax = 0.6f;
        p.metallicScale = std::min(p.metallicScale, 0.15f);

        // --- MToon -> hybrid mapping
        if (const cgltf_extension* mt = findExt(gm.extensions, gm.extensions_count, "VRMC_materials_mtoon")) {
            Json j = Json::parse(mt->data);
            glm::vec3 shade = jvec3(j["shadeColorFactor"], glm::vec3(0.8f, 0.7f, 0.75f));
            p.shadowColor = glm::vec4(glm::clamp(shade * glm::vec3(0.84f, 0.78f, 0.92f), 0.f, 1.f), 1.f);   // cool, slightly saturated shadow tint
            p.rampBlend = 0.9f;
            glm::vec3 oc = jvec3(j["outlineColorFactor"], glm::vec3(0.05f));
            p.outlineColor = glm::vec4(oc * 0.55f, 1.f);
            float ow = (float)j["outlineWidthFactor"].num(0.0);
            bool hasOutline = j["outlineWidthMode"].str("none") != "none" && ow > 0.f;
            p.outlineWidth = std::max(ow, opt.minOutlineWidth) * opt.outlineWidthScale;
            p.outlineAlpha = (hasOutline && opt.outlines) ? 1.f : 0.f;
            glm::vec3 rim = jvec3(j["parametricRimColorFactor"], glm::vec3(0));
            if (glm::length(rim) > 0.001f) { p.rimColor = glm::vec4(rim, 1.f); p.rimIntensity = 0.5f; p.rimPower = (float)j["parametricRimFresnelPowerFactor"].num(5.0); }
        }

        // --- per-part heuristics for VRoid-style assets
        bool isFaceSkin = contains(name, "face") && contains(name, "skin");
        bool isEye = contains(name, "eye"), isHair = contains(name, "hair"), isFaceDecor = contains(name, "mouth") || contains(name, "brow") || contains(name, "eyeline");
        p.selfShadow = 0.f;      // characters: no self-shadow (see toon.frag)
        p.sssStrength = 0.30f;   // artificial SSS band at the terminator (zeroed for face skin / eyes below)
        if (isFaceSkin) { p.faceColorStabilize = 0.65f; p.rimIntensity = 0.0f; p.outlineAlpha *= 1.f; }
        if (isFaceDecor || isEye) { p.rampBlend = 0.f; p.pbrBaseBlend = 0.f; p.realtimeShadowStrength = 0.f; p.faceColorStabilize = 1.f; p.matCapStrength = 0; p.rimIntensity = 0; }
        if (isEye) p.outlineAlpha = 0.f;
        if (isEye || isFaceDecor) p.sssStrength = 0.f;
        // layering (see toon.vert): skin is the innermost layer, then bottoms, then tops; hair/shoes stay on top
        {
            bool isBody = contains(name, "body") && contains(name, "skin"), isTops = contains(name, "tops"), isBottoms = contains(name, "bottoms");
            if (isBody)    p.depthPush = 0.020f;
            if (isBottoms) { p.layerOffset = 0.003f; p.depthPush = 0.014f; }
            if (isTops)    { p.layerOffset = 0.005f; p.depthPush = 0.006f; }
        }
        if (isHair) {
            m->tex[TEX_HAIR_SPEC] = style.hairSpec; p.hairAnisoStrength = 0.55f; p.hairAnisoPower = 60.f; p.hairSpecShift = -0.05f;
            p.rimIntensity = 0.12f; p.specRampStrength = 0.15f;
        }
        p.wetClothMask = isFaceSkin ? 0.2f : 0.6f; p.wetGlossMask = isFaceSkin ? 0.3f : 0.4f;   // only visible when `wetness` > 0
        m->params = p;
        R.flushMaterial(m);
        model->materials.push_back(m);
    }
    if (model->materials.empty()) {
        Material* m = R.createMaterial("default");
        m->tex[TEX_RAMP] = style.ramp; m->tex[TEX_SPEC_RAMP] = style.specRamp; m->tex[TEX_LUT] = style.lut; m->tex[TEX_MATCAP] = style.matcap;
        m->params.outlineAlpha = opt.outlines ? 1.f : 0.f; m->params.outlineWidth = 0.003f; R.flushMaterial(m);
        model->materials.push_back(m);
    }

    // ---------------------------------------------------------------- meshes
    // Body-under-clothes mask: collect the vertices of tops / bottoms (bind pose), so that body-skin vertices lying under them can be
    // pulled inward below. This is a procedural corrective shape baked into the rest mesh (the VRMs ship no body corrective morphs).
    std::vector<glm::vec3> clothP, clothN;
    auto lowerMatName = [&](const cgltf_primitive& gp) { std::string mn = gp.material && gp.material->name ? gp.material->name : ""; for (auto& ch : mn) ch = (char)std::tolower((unsigned char)ch); return mn; };
    if (opt.bodyUnderMask > 0.f) {
        for (size_t ni = 0; ni < data->nodes_count; ++ni) {
            const cgltf_node& n = data->nodes[ni]; if (!n.mesh) continue;
            bool sk = n.skin != nullptr;
            glm::mat4 bake = sk ? glm::mat4(1.f) : restGlobals[ni]; glm::mat3 nb = glm::mat3(glm::transpose(glm::inverse(bake)));
            for (size_t pi = 0; pi < n.mesh->primitives_count; ++pi) {
                const cgltf_primitive& gp = n.mesh->primitives[pi];
                std::string mn = lowerMatName(gp);
                if (mn.find("tops") == std::string::npos && mn.find("bottoms") == std::string::npos) continue;
                const cgltf_accessor *aP = nullptr, *aN = nullptr;
                for (size_t a = 0; a < gp.attributes_count; ++a) { if (gp.attributes[a].type == cgltf_attribute_type_position && !gp.attributes[a].index) aP = gp.attributes[a].data; if (gp.attributes[a].type == cgltf_attribute_type_normal && !gp.attributes[a].index) aN = gp.attributes[a].data; }
                if (!aP || !aN) continue;
                for (size_t v = 0; v < aP->count; ++v) { float f[3]; cgltf_accessor_read_float(aP, v, f, 3); clothP.push_back(glm::vec3(bake * glm::vec4(f[0], f[1], f[2], 1.f))); cgltf_accessor_read_float(aN, v, f, 3); clothN.push_back(glm::normalize(nb * glm::vec3(f[0], f[1], f[2]))); }
            }
        }
    }
    std::unordered_map<uint64_t, std::vector<int>> clothGrid; const float cell = 0.04f;
    auto cellKey = [&](glm::vec3 p) { int x = (int)std::floor(p.x / cell), y = (int)std::floor(p.y / cell), z = (int)std::floor(p.z / cell); return ((uint64_t)(x + 4096) << 42) ^ ((uint64_t)(y + 4096) << 21) ^ (uint64_t)(z + 4096); };
    for (size_t i = 0; i < clothP.size(); ++i) clothGrid[cellKey(clothP[i])].push_back((int)i);
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& n = data->nodes[ni];
        if (!n.mesh) continue;
        int skinIdx = n.skin ? (int)(n.skin - data->skins) : -1;
        for (size_t pi = 0; pi < n.mesh->primitives_count; ++pi) {
            const cgltf_primitive& gp = n.mesh->primitives[pi];
            if (gp.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *aPos = nullptr, *aNrm = nullptr, *aTan = nullptr, *aUV = nullptr, *aJ = nullptr, *aW = nullptr;
            for (size_t a = 0; a < gp.attributes_count; ++a) {
                const auto& at = gp.attributes[a];
                if (at.type == cgltf_attribute_type_position && !at.index) aPos = at.data;
                else if (at.type == cgltf_attribute_type_normal && !at.index) aNrm = at.data;
                else if (at.type == cgltf_attribute_type_tangent && !at.index) aTan = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && !at.index) aUV = at.data;
                else if (at.type == cgltf_attribute_type_joints && !at.index) aJ = at.data;
                else if (at.type == cgltf_attribute_type_weights && !at.index) aW = at.data;
            }
            if (!aPos) continue;
            MeshData md; md.vertices.resize(aPos->count);
            bool skinned = skinIdx >= 0 && aJ && aW;
            glm::mat4 bake = skinned ? glm::mat4(1.f) : restGlobals[ni];
            glm::mat3 nbake = glm::mat3(glm::transpose(glm::inverse(bake)));
            for (size_t v = 0; v < aPos->count; ++v) {
                Vertex& o = md.vertices[v]; float f[4];
                cgltf_accessor_read_float(aPos, v, f, 3); o.pos = glm::vec3(bake * glm::vec4(f[0], f[1], f[2], 1.f));
                if (aNrm) { cgltf_accessor_read_float(aNrm, v, f, 3); o.normal = glm::normalize(nbake * glm::vec3(f[0], f[1], f[2])); }
                if (aTan) { cgltf_accessor_read_float(aTan, v, f, 4); o.tangent = glm::vec4(glm::normalize(glm::mat3(bake) * glm::vec3(f[0], f[1], f[2])), f[3]); }
                if (aUV) { cgltf_accessor_read_float(aUV, v, f, 2); o.uv = {f[0], f[1]}; }
                if (skinned) {
                    cgltf_uint j[4] = {0, 0, 0, 0}; cgltf_accessor_read_uint(aJ, v, j, 4);
                    cgltf_accessor_read_float(aW, v, f, 4);
                    for (int k = 0; k < 4; ++k) { o.joints[k] = (uint8_t)std::min<cgltf_uint>(j[k], 255); o.weights[k] = f[k]; }
                }
                bmin = glm::min(bmin, o.pos); bmax = glm::max(bmax, o.pos);
            }
            if (gp.indices) { md.indices.resize(gp.indices->count); for (size_t k = 0; k < gp.indices->count; ++k) md.indices[k] = (uint32_t)cgltf_accessor_read_index(gp.indices, k); }
            else for (uint32_t k = 0; k < md.vertices.size(); ++k) md.indices.push_back(k);
            if (!aNrm) md.computeNormals();
            if (!aTan) md.computeTangents();
            if (opt.bodyUnderMask > 0.f && !clothP.empty()) {                 // body skin under clothes: corrective inward shape
                std::string mn = lowerMatName(gp);
                if (mn.find("body") != std::string::npos && mn.find("skin") != std::string::npos) {
                    const size_t nv = md.vertices.size(); std::vector<float> w(nv, 0.f), w2(nv);
                    for (size_t v = 0; v < nv; ++v) {
                        const Vertex& o = md.vertices[v]; int cx = (int)std::floor(o.pos.x / cell), cy = (int)std::floor(o.pos.y / cell), cz = (int)std::floor(o.pos.z / cell);
                        bool covered = false;
                        for (int dx = -1; dx <= 1 && !covered; ++dx) for (int dy = -1; dy <= 1 && !covered; ++dy) for (int dz = -1; dz <= 1 && !covered; ++dz) {
                            auto it = clothGrid.find(cellKey(glm::vec3((cx + dx + 0.5f) * cell, (cy + dy + 0.5f) * cell, (cz + dz + 0.5f) * cell)));
                            if (it == clothGrid.end()) continue;
                            for (int ci : it->second) {
                                glm::vec3 d = clothP[ci] - o.pos; float dist = glm::length(d);
                                if (dist < 0.035f && glm::dot(d, o.normal) > -0.004f && glm::dot(clothN[ci], o.normal) > 0.3f) { covered = true; break; }
                            }
                        }
                        w[v] = covered ? 1.f : 0.f;
                    }
                    // blur the mask over the mesh so the pulled-in area fades out smoothly at the clothing borders
                    std::vector<std::vector<uint32_t>> adj(nv);
                    for (size_t k = 0; k + 2 < md.indices.size(); k += 3) for (int a = 0; a < 3; ++a) { uint32_t i0 = md.indices[k + a], i1 = md.indices[k + (a + 1) % 3]; adj[i0].push_back(i1); adj[i1].push_back(i0); }
                    for (int pass = 0; pass < 3; ++pass) {
                        for (size_t v = 0; v < nv; ++v) { float s = w[v] * 2.f, c = 2.f; for (uint32_t u : adj[v]) { s += w[u]; c += 1.f; } w2[v] = s / c; }
                        w.swap(w2);
                    }
                    int n = 0; for (size_t v = 0; v < nv; ++v) if (w[v] > 0.01f) { md.vertices[v].pos -= md.vertices[v].normal * (opt.bodyUnderMask * w[v]); ++n; }
                    SK_LOG("body-under-clothes mask: %d / %zu body vertices pulled in by up to %.1f mm", n, nv, opt.bodyUnderMask * 1000.f);
                }
            }
            {   // approximate directional face-shadow map baked from the VRoid-style face mesh
                int mi = gp.material ? (int)(gp.material - data->materials) : -1;
                if (mi >= 0 && mi < (int)model->materials.size() && (!model->materials[mi]->tex[TEX_FACE_SDF] || model->materials[mi]->tex[TEX_FACE_SDF] == R.blackTex())) {
                    std::string mn = model->materials[mi]->name; for (auto& ch : mn) ch = (char)std::tolower((unsigned char)ch);
                    if (mn.find("face") != std::string::npos && mn.find("skin") != std::string::npos) {
                        if (Texture* t = bakeFaceSDF(R, md, model->materials[mi]->name)) {
                            Material* fm = model->materials[mi]; MaterialParams& q = fm->params;
                            fm->tex[TEX_FACE_SDF] = t; fm->faceFrame = true;
                            q.sdfDirectionalRG = 1.f; q.sdfSwapRG = 0.f; q.sdfShadowStrength = 1.f; q.sdfSoftness = 0.05f; q.sdfThreshold = 0.5f;
                            q.sdfShadowColor = glm::vec4(glm::clamp(glm::vec3(q.shadowColor) * glm::vec3(1.0f, 0.88f, 0.9f), 0.f, 1.f), 1.f);
                            R.flushMaterial(fm);
                            SK_LOG("face SDF baked for material %s", fm->name.c_str());
                        }
                    }
                }
            }
            Primitive prim; prim.mesh = R.createMesh(md); prim.node = (int)ni; prim.meshIndex = (int)(n.mesh - data->meshes);
            if (gp.targets_count > 0 && !model->expressions.empty()) {   // keep only the morph targets that VRM expressions drive (blink, visemes, emotions)
                std::vector<int> wanted;
                for (const Expression& ex : model->expressions) for (const MorphBind& b : ex.binds)
                    if (b.mesh == prim.meshIndex && b.index >= 0 && b.index < (int)gp.targets_count && std::find(wanted.begin(), wanted.end(), b.index) == wanted.end()) wanted.push_back(b.index);
                std::sort(wanted.begin(), wanted.end());
                std::vector<std::vector<glm::vec3>> deltas;
                for (int ti : wanted) {
                    std::vector<glm::vec3> dv(aPos->count, glm::vec3(0));
                    for (size_t a = 0; a < gp.targets[ti].attributes_count; ++a) {
                        const auto& at = gp.targets[ti].attributes[a];
                        if (at.type != cgltf_attribute_type_position) continue;
                        for (size_t v = 0; v < std::min(at.data->count, dv.size()); ++v) { float f[3]; cgltf_accessor_read_float(at.data, v, f, 3); dv[v] = glm::mat3(bake) * glm::vec3(f[0], f[1], f[2]); }
                    }
                    deltas.push_back(std::move(dv));
                }
                if (!deltas.empty()) R.setMorphTargets(const_cast<GpuMesh*>(prim.mesh), wanted, deltas);
            } prim.skin = skinned ? skinIdx : -1;
            prim.material = gp.material ? (int)(gp.material - data->materials) : 0; prim.name = n.mesh->name ? n.mesh->name : "mesh";
            model->prims.push_back(prim);
        }
    }
    model->boundsMin = bmin; model->boundsMax = bmax;
    model->height = std::max(0.1f, bmax.y - bmin.y);

    // ---------------------------------------------------------------- animations
    for (size_t ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& ga = data->animations[ai];
        AnimClip clip; clip.name = ga.name ? ga.name : "anim" + std::to_string(ai);
        for (size_t ci = 0; ci < ga.channels_count; ++ci) {
            const cgltf_animation_channel& gc = ga.channels[ci];
            if (!gc.target_node || !gc.sampler) continue;
            AnimChannel c; c.node = (int)(gc.target_node - data->nodes);
            switch (gc.target_path) {
                case cgltf_animation_path_type_translation: c.path = AnimChannel::Translation; break;
                case cgltf_animation_path_type_rotation: c.path = AnimChannel::Rotation; break;
                case cgltf_animation_path_type_scale: c.path = AnimChannel::Scale; break;
                default: continue;
            }
            c.step = gc.sampler->interpolation == cgltf_interpolation_type_step;
            bool cubic = gc.sampler->interpolation == cgltf_interpolation_type_cubic_spline;   // approximated by taking the value tangents out
            size_t n = gc.sampler->input->count; c.times.resize(n); c.values.resize(n);
            size_t comps = cgltf_num_components(gc.sampler->output->type);
            for (size_t k = 0; k < n; ++k) {
                cgltf_accessor_read_float(gc.sampler->input, k, &c.times[k], 1);
                float f[4] = {0, 0, 0, 1}; cgltf_accessor_read_float(gc.sampler->output, cubic ? k * 3 + 1 : k, f, comps);
                c.values[k] = {f[0], f[1], f[2], f[3]};
            }
            if (n) clip.duration = std::max(clip.duration, c.times.back());
            clip.channels.push_back(std::move(c));
        }
        model->clips.push_back(std::move(clip));
    }

    SK_LOG("loaded %s: %zu nodes, %zu prims, %zu materials, %zu clips%s, height %.2fm", model->name.c_str(), rig.nodes.size(),
           model->prims.size(), model->materials.size(), model->clips.size(), model->isVRM ? ", VRM humanoid" : "", model->height);
    cgltf_free(data);
    return model;
}
}  // namespace sk

namespace sk {
bool loadHdrImage(const std::string& path, std::vector<float>& rgb, int& w, int& h) {
    int n = 0; float* d = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    if (!d) return false;
    rgb.assign(d, d + (size_t)w * h * 3); stbi_image_free(d); return true;
}
}
