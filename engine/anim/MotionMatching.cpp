#include "anim/MotionMatching.h"
#include "core/Log.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <atomic>
#include <thread>
#include <cstdio>
#include <filesystem>

namespace sk {
namespace {
using glm::vec3; using glm::quat; using glm::mat4;
const vec3 kUp(0, 1, 0);
constexpr float kLn2 = 0.69314718f;

float wrapPi(float a) { while (a > glm::pi<float>()) a -= glm::two_pi<float>(); while (a < -glm::pi<float>()) a += glm::two_pi<float>(); return a; }
quat yawQuat(float yaw) { return glm::angleAxis(yaw, kUp); }
vec3 rotY(float yaw, const vec3& v) { return yawQuat(yaw) * v; }
quat qabs(quat q) { return q.w < 0 ? -q : q; }
vec3 quatToScaledAA(quat q) {
    q = qabs(q);
    float s = glm::length(vec3(q.x, q.y, q.z));
    if (s < 1e-8f) return 2.f * vec3(q.x, q.y, q.z);
    return vec3(q.x, q.y, q.z) / s * (2.f * std::atan2(s, q.w));
}
quat quatFromScaledAA(const vec3& v) {
    float h = glm::length(v) * 0.5f;
    if (h < 1e-8f) return glm::normalize(quat(1.f, v.x * 0.5f, v.y * 0.5f, v.z * 0.5f));
    float s = std::sin(h) / (2.f * h);
    return quat(std::cos(h), v.x * s, v.y * s, v.z * s);
}
// critically damped decay (Holden): x and v relax to zero with the given half-life
void decay(vec3& x, vec3& v, float halflife, float dt) {
    float y = (4.f * kLn2 / (halflife + 1e-5f)) / 2.f;
    vec3 j1 = v + x * y; float e = std::exp(-y * dt);
    x = e * (x + j1 * dt); v = e * (v - j1 * y * dt);
}

const char* kBvhNames[kHumanBoneCount] = {"Hips", "Spine", "Spine1", "Spine2", "Neck", "Head",
    "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm", "RightForeArm", "RightHand",
    "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToe", "RightUpLeg", "RightLeg", "RightFoot", "RightToe"};
#define HB(n) ((int)HumanBone::n)
}

// =============================================================================== database
// =============================================================================== database
namespace {
uint64_t fnv(uint64_t h, const void* d, size_t n) { const unsigned char* p = (const unsigned char*)d; for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h; }
template <class T> void wvec(std::FILE* f, const std::vector<T>& v) { uint64_t n = v.size(); std::fwrite(&n, 8, 1, f); if (n) std::fwrite(v.data(), sizeof(T), n, f); }
template <class T> bool rvec(std::FILE* f, std::vector<T>& v) { uint64_t n; if (std::fread(&n, 8, 1, f) != 1 || n > (1ull << 33)) return false; v.resize(n); return !n || std::fread(v.data(), sizeof(T), n, f) == n; }
struct ClipOut { std::string name; int n = 0, tEnd = 0; bool ok = false; std::vector<quat> rot; std::vector<vec3> hips, root; std::vector<float> yaw, feat; std::vector<char> valid; };
}

const char* kMotionCategoryNames[kMotionCategoryCount] = {"Walk", "Run / sprint", "Obstacles", "Jumps", "Interaction (push, multi-action)", "Dance", "Fight / sports", "Aiming", "Ground / fall-and-get-up"};
int motionCategoryOf(const std::string& stem) {
    auto has = [&](const char* p) { return stem.rfind(p, 0) == 0; };
    if (has("walk")) return 0;
    if (has("run") || has("sprint")) return 1;
    if (has("obstacles")) return 2;
    if (has("jumps")) return 3;
    if (has("multipleActions") || has("push")) return 4;
    if (has("dance")) return 5;
    if (has("fight")) return 6;
    if (has("aiming")) return 7;
    return 8;
}

bool MotionDatabase::saveCache(const std::string& path, uint64_t key) const {
    std::FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    uint32_t magic = 0x534B4D4D, ver = 2; std::fwrite(&magic, 4, 1, f); std::fwrite(&ver, 4, 1, f); std::fwrite(&key, 8, 1, f);
    std::fwrite(&fps, 4, 1, f); std::fwrite(&frameCount, 4, 1, f); std::fwrite(&retargetScale, 4, 1, f);
    for (int b = 0; b < kHumanBoneCount; ++b) { char m = mapped[b]; std::fwrite(&m, 1, 1, f); }
    std::fwrite(featureMean, 4, kFeatureDim, f); std::fwrite(featureScale, 4, FG_Count, f);
    uint64_t nc = clipNames.size(); std::fwrite(&nc, 8, 1, f);
    for (auto& s : clipNames) { uint32_t l = (uint32_t)s.size(); std::fwrite(&l, 4, 1, f); std::fwrite(s.data(), 1, l, f); }
    wvec(f, clipStart); wvec(f, clipEnd); wvec(f, clipCategory); wvec(f, frameClip);
    wvec(f, localRot); wvec(f, hipsLocalPos); wvec(f, rootPos); wvec(f, rootYaw); wvec(f, features); wvec(f, valid);
    std::fclose(f); return true;
}

bool MotionDatabase::loadCache(const std::string& path, uint64_t key) {
    std::FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    bool ok = true; uint32_t magic = 0, ver = 0; uint64_t k = 0;
    ok = ok && std::fread(&magic, 4, 1, f) == 1 && std::fread(&ver, 4, 1, f) == 1 && std::fread(&k, 8, 1, f) == 1;
    if (!ok || magic != 0x534B4D4D || ver != 2 || k != key) { std::fclose(f); return false; }
    ok = ok && std::fread(&fps, 4, 1, f) == 1 && std::fread(&frameCount, 4, 1, f) == 1 && std::fread(&retargetScale, 4, 1, f) == 1;
    for (int b = 0; b < kHumanBoneCount; ++b) { char m = 0; ok = ok && std::fread(&m, 1, 1, f) == 1; mapped[b] = m != 0; }
    ok = ok && std::fread(featureMean, 4, kFeatureDim, f) == (size_t)kFeatureDim && std::fread(featureScale, 4, FG_Count, f) == (size_t)FG_Count;
    uint64_t nc = 0; ok = ok && std::fread(&nc, 8, 1, f) == 1 && nc < 100000;
    clipNames.clear();
    for (uint64_t i = 0; ok && i < nc; ++i) { uint32_t l = 0; ok = std::fread(&l, 4, 1, f) == 1 && l < 4096; std::string s(l, ' '); ok = ok && (!l || std::fread(&s[0], 1, l, f) == l); clipNames.push_back(s); }
    ok = ok && rvec(f, clipStart) && rvec(f, clipEnd) && rvec(f, clipCategory) && rvec(f, frameClip);
    ok = ok && rvec(f, localRot) && rvec(f, hipsLocalPos) && rvec(f, rootPos) && rvec(f, rootYaw) && rvec(f, features) && rvec(f, valid);
    std::fclose(f);
    if (!ok || (int)valid.size() != frameCount) return false;
    buildFrameCategories();
    return true;
}

void MotionDatabase::buildFrameCategories() {
    frameCat.assign(frameCount, 0);
    for (size_t c = 0; c < clipStart.size(); ++c) for (int f = clipStart[c]; f < clipEnd[c]; ++f) frameCat[f] = (uint8_t)clipCategory[c];
}

bool MotionDatabase::buildFromFiles(const Rig& rig, std::vector<std::string> files, const std::string& cacheDir, int maxClips, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (files.empty()) return fail("no BVH files");
    if (!rig.isHumanoid()) return fail("rig has no humanoid map");
    std::sort(files.begin(), files.end());
    if (maxClips > 0 && (int)files.size() > maxClips) files.resize(maxClips);
    size_t refIdx = 0;
    for (size_t i = 0; i < files.size(); ++i) if (std::filesystem::path(files[i]).stem() == "walk1_subject1") refIdx = i;

    // --- cache key: file list + sizes + the target rig's humanoid rest pose
    uint64_t key = 1469598103934665603ull; uint32_t ver = 3; key = fnv(key, &ver, 4);
    for (auto& f : files) { std::string st = std::filesystem::path(f).stem().string(); key = fnv(key, st.data(), st.size());
        std::error_code ec; uint64_t sz = std::filesystem::file_size(f, ec); key = fnv(key, &sz, 8); }
    { Pose rp = rig.restPose(); for (int b = 0; b < kHumanBoneCount; ++b) { int n = rig.human[b]; key = fnv(key, &n, 4); if (n >= 0) { glm::ivec4 q(glm::round(glm::vec4(rp.r[n].x, rp.r[n].y, rp.r[n].z, rp.r[n].w) * 1000.f)); glm::ivec3 t(glm::round(rp.t[n] * 1000.f)); key = fnv(key, &q, sizeof q); key = fnv(key, &t, sizeof t); } } }
    std::string cacheFile;
    if (!cacheDir.empty()) { std::error_code ec; std::filesystem::create_directories(cacheDir, ec); char hex[32]; std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)key); cacheFile = cacheDir + "/mm_" + hex + ".bin"; }
    if (!cacheFile.empty() && loadCache(cacheFile, key)) { SK_LOG("mm: loaded cached database (%zu clips, %d frames) from %s", clipNames.size(), frameCount, cacheFile.c_str()); return true; }

    BvhClip c0;
    { std::string err; if (!loadBvh(files[refIdx], c0, &err)) return fail("cannot load the reference clip"); }
    fps = 1.f / c0.frameTime;

    for (int b = 0; b < kHumanBoneCount; ++b) mapped[b] = rig.human[b] >= 0 && c0.findJoint(kBvhNames[b]) >= 0;
    if (!mapped[HB(Hips)] || !mapped[HB(LFoot)] || !mapped[HB(RFoot)] || !mapped[HB(LUpperLeg)] || !mapped[HB(RUpperLeg)]) return fail("missing essential bones");

    // --- rest pose of the target rig
    std::vector<mat4> restG; rig.globals(rig.restPose(), restG);
    std::vector<quat> restRot(rig.nodes.size()); std::vector<vec3> restPos(rig.nodes.size());
    for (size_t i = 0; i < rig.nodes.size(); ++i) {
        glm::mat3 m(restG[i]); for (int c = 0; c < 3; ++c) m[c] = glm::normalize(m[c]);
        restRot[i] = glm::normalize(glm::quat_cast(m)); restPos[i] = vec3(restG[i][3]);
    }
    std::vector<int> nodeToBone(rig.nodes.size(), -1);
    for (int b = 0; b < kHumanBoneCount; ++b) if (mapped[b]) nodeToBone[rig.human[b]] = b;
    // child used for bone-direction (swing) alignment
    int childOf[kHumanBoneCount]; for (int& c : childOf) c = -1;
    auto link = [&](HumanBone a, HumanBone b) { if (mapped[(int)a] && mapped[(int)b]) childOf[(int)a] = (int)b; };
    link(HumanBone::Hips, HumanBone::Spine); link(HumanBone::Spine, HumanBone::Chest);
    if (mapped[HB(UpperChest)]) { link(HumanBone::Chest, HumanBone::UpperChest); link(HumanBone::UpperChest, HumanBone::Neck); }
    else link(HumanBone::Chest, HumanBone::Neck);
    link(HumanBone::Neck, HumanBone::Head);
    link(HumanBone::LShoulder, HumanBone::LUpperArm); link(HumanBone::LUpperArm, HumanBone::LLowerArm); link(HumanBone::LLowerArm, HumanBone::LHand);
    link(HumanBone::RShoulder, HumanBone::RUpperArm); link(HumanBone::RUpperArm, HumanBone::RLowerArm); link(HumanBone::RLowerArm, HumanBone::RHand);
    link(HumanBone::LUpperLeg, HumanBone::LLowerLeg); link(HumanBone::LLowerLeg, HumanBone::LFoot); link(HumanBone::LFoot, HumanBone::LToes);
    link(HumanBone::RUpperLeg, HumanBone::RLowerLeg); link(HumanBone::RLowerLeg, HumanBone::RFoot); link(HumanBone::RFoot, HumanBone::RToes);
    int swingParent[kHumanBoneCount]; for (int& c : swingParent) c = -1;     // leaf bones borrow their parent's swing
    swingParent[HB(Head)] = HB(Neck); swingParent[HB(LHand)] = HB(LLowerArm); swingParent[HB(RHand)] = HB(RLowerArm);
    swingParent[HB(LToes)] = HB(LFoot); swingParent[HB(RToes)] = HB(RFoot);
    int bvhIdx0[kHumanBoneCount]; for (int b = 0; b < kHumanBoneCount; ++b) bvhIdx0[b] = mapped[b] ? c0.findJoint(kBvhNames[b]) : -1;

    // --- reference frame (clip 0, frame 0) -> swing corrections S_b and scale
    auto yawOf = [&](const std::vector<vec3>& pos, const int* bi) {
        vec3 l = pos[bi[HB(LUpperLeg)]] - pos[bi[HB(RUpperLeg)]]; l.y = 0;
        vec3 acc = glm::normalize(l);
        if (mapped[HB(LUpperArm)] && mapped[HB(RUpperArm)]) { vec3 s = pos[bi[HB(LUpperArm)]] - pos[bi[HB(RUpperArm)]]; s.y = 0; if (glm::length(s) > 1e-5f) acc += glm::normalize(s); }
        vec3 f = glm::cross(acc, kUp);                       // left x up = forward (+Z when left = +X)
        return std::atan2(f.x, f.z);
    };
    std::vector<vec3> bp; std::vector<quat> br;
    c0.fk(0, 0.01f, bp, br);
    float vrmLeg = restPos[rig.human[HB(Hips)]].y - restPos[rig.human[HB(LFoot)]].y;
    float bvhLeg = bp[bvhIdx0[HB(Hips)]].y - bp[bvhIdx0[HB(LFoot)]].y;
    retargetScale = vrmLeg / std::max(bvhLeg, 1e-3f);
    float k = retargetScale;
    float refYaw = yawOf(bp, bvhIdx0);
    quat refYawInv = glm::inverse(yawQuat(refYaw));
    vec3 refHips = bp[bvhIdx0[HB(Hips)]];
    quat Nref[kHumanBoneCount], S[kHumanBoneCount];
    vec3 Pref[kHumanBoneCount];
    for (int b = 0; b < kHumanBoneCount; ++b) if (mapped[b]) { Nref[b] = refYawInv * br[bvhIdx0[b]]; Pref[b] = refYawInv * (bp[bvhIdx0[b]] - refHips); S[b] = quat(1, 0, 0, 0); }
    for (int b = 0; b < kHumanBoneCount; ++b) {
        if (!mapped[b]) continue;
        if (childOf[b] >= 0) {
            int c = childOf[b];
            vec3 dv = restPos[rig.human[c]] - restPos[rig.human[b]], db = Pref[c] - Pref[b];
            if (glm::length(dv) > 1e-6f && glm::length(db) > 1e-6f) S[b] = glm::rotation(glm::normalize(dv), glm::normalize(db));
        }
    }
    for (int b = 0; b < kHumanBoneCount; ++b) if (mapped[b] && childOf[b] < 0 && swingParent[b] >= 0 && mapped[swingParent[b]]) S[b] = S[swingParent[b]];
    float restHipsY = restPos[rig.human[HB(Hips)]].y;
    mat4 hipsParentInv(1.f);
    if (rig.nodes[rig.human[HB(Hips)]].parent >= 0) hipsParentInv = glm::inverse(restG[rig.nodes[rig.human[HB(Hips)]].parent]);

    // nearest mapped ancestor per mapped bone (+ the rest rotation of its immediate parent node)
    int ancestorBone[kHumanBoneCount]; quat parentRest[kHumanBoneCount];
    for (int b = 0; b < kHumanBoneCount; ++b) {
        ancestorBone[b] = -1; parentRest[b] = quat(1, 0, 0, 0);
        if (!mapped[b]) continue;
        int pn = rig.nodes[rig.human[b]].parent;
        if (pn >= 0) parentRest[b] = restRot[pn];
        for (int a = pn; a >= 0; a = rig.nodes[a].parent) if (nodeToBone[a] >= 0) { ancestorBone[b] = nodeToBone[a]; break; }
    }

    // --- per clip retarget + features (clips are loaded, processed and released one at a time, two worker threads)
    const int smoothR = 8;
    std::vector<ClipOut> outs(files.size());
    std::atomic<size_t> nextFile{0}, doneFiles{0};
    auto processClip = [&](const std::string& path, ClipOut& out) {
        BvhClip c; std::string err;
        if (!loadBvh(path, c, &err)) { SK_WARN("bvh %s: %s", path.c_str(), err.c_str()); return; }
        int bi[kHumanBoneCount]; for (int b = 0; b < kHumanBoneCount; ++b) bi[b] = mapped[b] ? c.findJoint(kBvhNames[b]) : -1;
        bool ok = true; for (int b = 0; b < kHumanBoneCount; ++b) if (mapped[b] && bi[b] < 0) ok = false;
        if (!ok) { SK_WARN("mm: clip %s has a different skeleton, skipped", c.name.c_str()); return; }
        int n = c.frameCount;
        std::vector<vec3> bpL, hip(n), lf(n), rf(n), fwd(n);
        std::vector<quat> brL;
        std::vector<quat> rots((size_t)n * c.joints.size());
        std::vector<char> armsDown(n, 1), tLike(n, 0);
        for (int f = 0; f < n; ++f) {
            c.fk(f, 0.01f, bpL, brL);
            if (mapped[HB(LUpperArm)] && mapped[HB(RUpperArm)] && mapped[HB(LLowerArm)] && mapped[HB(RLowerArm)]) {
                vec3 la = bpL[bi[HB(LLowerArm)]] - bpL[bi[HB(LUpperArm)]], ra = bpL[bi[HB(RLowerArm)]] - bpL[bi[HB(RUpperArm)]];
                armsDown[f] = (la.y < -0.55f * glm::length(la)) && (ra.y < -0.55f * glm::length(ra));
                // both forearms level and pointing in opposite directions = a (calibration) T-pose, which also appears in the middle of a few clips
                float ll = glm::length(la), rl = glm::length(ra);
                tLike[f] = ll > 1e-5f && rl > 1e-5f && std::fabs(la.y) < 0.3f * ll && std::fabs(ra.y) < 0.3f * rl && glm::dot(la, ra) < -0.6f * ll * rl;
            }
            hip[f] = bpL[bi[HB(Hips)]] * k; lf[f] = bpL[bi[HB(LFoot)]] * k; rf[f] = bpL[bi[HB(RFoot)]] * k;
            vec3 l = bpL[bi[HB(LUpperLeg)]] - bpL[bi[HB(RUpperLeg)]]; l.y = 0;
            vec3 acc = glm::normalize(l);
            if (mapped[HB(LUpperArm)] && mapped[HB(RUpperArm)]) { vec3 s = bpL[bi[HB(LUpperArm)]] - bpL[bi[HB(RUpperArm)]]; s.y = 0; if (glm::length(s) > 1e-5f) acc += glm::normalize(s); }
            fwd[f] = glm::normalize(glm::cross(glm::normalize(acc), kUp));
            std::copy(brL.begin(), brL.end(), rots.begin() + (size_t)f * c.joints.size());
        }
        const size_t nj = c.joints.size();
        std::vector<vec3> root(n); std::vector<float> yaw(n);
        for (int f = 0; f < n; ++f) {                                     // smoothed root (box filter)
            vec3 p(0), d(0); int cnt = 0;
            for (int j = std::max(0, f - smoothR); j <= std::min(n - 1, f + smoothR); ++j) { p += hip[j]; d += fwd[j]; ++cnt; }
            root[f] = vec3(p.x / cnt, 0, p.z / cnt); yaw[f] = std::atan2(d.x, d.z);
        }
        out.rot.reserve((size_t)n * kHumanBoneCount); out.hips.reserve(n);
        for (int f = 0; f < n; ++f) {
            quat yi = glm::inverse(yawQuat(yaw[f]));
            quat Gt[kHumanBoneCount];
            for (int b = 0; b < kHumanBoneCount; ++b) if (mapped[b]) {
                quat Nb = yi * rots[(size_t)f * nj + bi[b]];
                Gt[b] = glm::normalize((Nb * glm::inverse(Nref[b])) * S[b] * restRot[rig.human[b]]);
            }
            for (int b = 0; b < kHumanBoneCount; ++b) {
                quat L(1, 0, 0, 0);
                if (mapped[b]) {
                    quat P = parentRest[b];
                    if (ancestorBone[b] >= 0) { int a = ancestorBone[b]; P = Gt[a] * glm::inverse(restRot[rig.human[a]]) * parentRest[b]; }
                    L = glm::normalize(glm::inverse(P) * Gt[b]);
                    if (L.w < 0) L = -L;
                }
                out.rot.push_back(L);
            }
            vec3 hl = yi * (hip[f] - root[f]);
            vec3 charHips(hl.x, restHipsY + (hip[f].y - refHips.y * k), hl.z);
            out.hips.push_back(vec3(hipsParentInv * glm::vec4(charHips, 1.f)));
        }
        // LAFAN1 sequences start with a T-pose calibration phase: never match against it
        int tEnd = 0;
        for (int f = 0; f + 15 <= n; ++f) {
            bool all = true; for (int j = 0; j < 15; ++j) if (!armsDown[f + j]) { all = false; break; }
            if (all) { tEnd = f; break; }
            if (f > 600) break;
        }
        { std::vector<char> d(tLike); for (int f = 0; f < n; ++f) if (tLike[f]) for (int j = std::max(0, f - 8); j <= std::min(n - 1, f + 8); ++j) d[j] = 1; tLike.swap(d); }
        float dt = c.frameTime;
        out.feat.assign((size_t)n * kFeatureDim, 0.f); out.valid.assign(n, 0);
        for (int f = 0; f < n; ++f) {
            quat yi = glm::inverse(yawQuat(yaw[f]));
            int f0 = std::max(0, f - 1), f1 = std::min(n - 1, f + 1);
            float inv = 1.f / std::max((f1 - f0) * dt, 1e-4f);
            float* x = &out.feat[(size_t)f * kFeatureDim];
            vec3 a = yi * (lf[f] - root[f]), b2 = yi * (rf[f] - root[f]);
            vec3 va = yi * ((lf[f1] - lf[f0]) * inv), vb = yi * ((rf[f1] - rf[f0]) * inv), vh = yi * ((hip[f1] - hip[f0]) * inv);
            for (int d = 0; d < 3; ++d) { x[d] = a[d]; x[3 + d] = b2[d]; x[6 + d] = va[d]; x[9 + d] = vb[d]; x[12 + d] = vh[d]; }
            bool ok2 = f >= std::max(1, tEnd + 10) && !tLike[f];
            for (int t = 0; t < kTrajSamples; ++t) {
                int ft = f + trajOffsets[t];
                if (ft >= n) { ok2 = false; continue; }
                vec3 dp = yi * (root[ft] - root[f]); float dy = wrapPi(yaw[ft] - yaw[f]);
                x[15 + t * 2] = dp.x; x[15 + t * 2 + 1] = dp.z;
                x[21 + t * 2] = std::sin(dy); x[21 + t * 2 + 1] = std::cos(dy);
            }
            out.valid[f] = ok2 ? 1 : 0;
        }
        out.root = std::move(root); out.yaw = std::move(yaw);
        out.name = c.name; out.n = n; out.tEnd = tEnd; out.ok = true;
    };
    auto worker = [&]() {
        for (;;) {
            size_t i = nextFile.fetch_add(1); if (i >= files.size()) break;
            processClip(files[i], outs[i]);
            size_t d = ++doneFiles; if (d % 8 == 0 || d == files.size()) SK_LOG("mm: retargeted %zu / %zu clips", d, files.size());
        }
    };
    {
        unsigned nt = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
        std::vector<std::thread> th; for (unsigned t = 1; t < nt; ++t) th.emplace_back(worker); worker(); for (auto& t : th) t.join();
    }
    localRot.clear(); hipsLocalPos.clear(); rootPos.clear(); rootYaw.clear(); features.clear(); valid.clear();
    clipNames.clear(); clipStart.clear(); clipEnd.clear(); clipCategory.clear(); frameClip.clear(); frameCount = 0;
    size_t total = 0; for (auto& o : outs) if (o.ok) total += o.n;
    localRot.reserve(total * kHumanBoneCount); hipsLocalPos.reserve(total); rootPos.reserve(total); rootYaw.reserve(total); features.reserve(total * kFeatureDim); valid.reserve(total); frameClip.reserve(total);
    for (size_t i = 0; i < outs.size(); ++i) {
        ClipOut& o = outs[i]; if (!o.ok) continue;
        int ci = (int)clipNames.size();
        clipNames.push_back(o.name); clipStart.push_back(frameCount); clipEnd.push_back(frameCount + o.n); clipCategory.push_back(motionCategoryOf(o.name));
        localRot.insert(localRot.end(), o.rot.begin(), o.rot.end()); hipsLocalPos.insert(hipsLocalPos.end(), o.hips.begin(), o.hips.end());
        rootPos.insert(rootPos.end(), o.root.begin(), o.root.end()); rootYaw.insert(rootYaw.end(), o.yaw.begin(), o.yaw.end());
        features.insert(features.end(), o.feat.begin(), o.feat.end()); valid.insert(valid.end(), o.valid.begin(), o.valid.end());
        frameClip.insert(frameClip.end(), (size_t)o.n, ci);
        frameCount += o.n;
        o = ClipOut{};                                                  // release per-clip memory
    }
    outs.clear(); outs.shrink_to_fit();
    if (frameCount == 0) return fail("no usable clips");

    // --- normalise: per-dim mean, per-group scale (keeps geometry of vectors intact)
    double sum[kFeatureDim] = {}; size_t cnt = 0;
    for (int f = 0; f < frameCount; ++f) if (valid[f]) { for (int d = 0; d < kFeatureDim; ++d) sum[d] += features[(size_t)f * kFeatureDim + d]; ++cnt; }
    for (int d = 0; d < kFeatureDim; ++d) featureMean[d] = (float)(sum[d] / std::max<size_t>(cnt, 1));
    double var[FG_Count] = {}; int dims[FG_Count] = {};
    for (int d = 0; d < kFeatureDim; ++d) dims[groupOfDim(d)]++;
    for (int f = 0; f < frameCount; ++f) if (valid[f]) for (int d = 0; d < kFeatureDim; ++d) { double e = features[(size_t)f * kFeatureDim + d] - featureMean[d]; var[groupOfDim(d)] += e * e; }
    for (int g = 0; g < FG_Count; ++g) featureScale[g] = (float)std::sqrt(var[g] / std::max<double>(1.0, (double)cnt * dims[g])) + 1e-4f;
    for (int f = 0; f < frameCount; ++f) for (int d = 0; d < kFeatureDim; ++d) { float& v = features[(size_t)f * kFeatureDim + d]; v = (v - featureMean[d]) / featureScale[groupOfDim(d)]; }
    buildFrameCategories();
    SK_LOG("mm: database ready: %zu clips, %d frames, %zu searchable, retarget scale %.3f", clipNames.size(), frameCount, cnt, retargetScale);
    if (!cacheFile.empty() && saveCache(cacheFile, key)) SK_LOG("mm: wrote database cache %s", cacheFile.c_str());
    return true;
}


// =============================================================================== matcher
void MotionMatcher::init(const Rig* rig, const MotionDatabase* db) {
    rig_ = rig; db_ = db;
    for (auto& q : offR_) q = quat(1, 0, 0, 0);
    for (auto& v : offRv_) v = vec3(0);
    offP_ = offPv_ = vec3(0);
}

void MotionMatcher::samplePose(float frameF, Sample& s) const {
    int i = (int)std::floor(frameF); float a = frameF - i;
    int clip = db_->frameClip[std::clamp(i, 0, db_->frameCount - 1)];
    int i1 = std::min(i + 1, db_->clipEnd[clip] - 1);
    for (int b = 0; b < kHumanBoneCount; ++b)
        s.r[b] = glm::slerp(db_->localRot[(size_t)i * kHumanBoneCount + b], db_->localRot[(size_t)i1 * kHumanBoneCount + b], a);
    s.hips = glm::mix(db_->hipsLocalPos[i], db_->hipsLocalPos[i1], a);
}

void MotionMatcher::sampleVel(int f, std::array<vec3, kHumanBoneCount>& w, vec3& hv) const {
    int clip = db_->frameClip[f];
    int f0 = std::max(db_->clipStart[clip], f - 0), f1 = std::min(db_->clipEnd[clip] - 1, f + 1);
    if (f1 == f0) { f0 = std::max(db_->clipStart[clip], f - 1); }
    float inv = db_->fps / std::max(1, f1 - f0);
    for (int b = 0; b < kHumanBoneCount; ++b) {
        quat q0 = db_->localRot[(size_t)f0 * kHumanBoneCount + b], q1 = db_->localRot[(size_t)f1 * kHumanBoneCount + b];
        w[b] = quatToScaledAA(q1 * glm::inverse(q0)) * inv;
    }
    hv = (db_->hipsLocalPos[f1] - db_->hipsLocalPos[f0]) * inv;
}

void MotionMatcher::rootAt(float frameF, vec3& p, float& yaw) const {
    int i = (int)std::floor(frameF); float a = frameF - i;
    int clip = db_->frameClip[std::clamp(i, 0, db_->frameCount - 1)];
    int i1 = std::min(i + 1, db_->clipEnd[clip] - 1);
    p = glm::mix(db_->rootPos[i], db_->rootPos[i1], a);
    yaw = db_->rootYaw[i] + wrapPi(db_->rootYaw[i1] - db_->rootYaw[i]) * a;
}

void MotionMatcher::transitionTo(int nf) {
    Sample src, dst; samplePose(frameF_, src); samplePose((float)nf, dst);
    std::array<vec3, kHumanBoneCount> sw, dw; vec3 shv, dhv;
    sampleVel(std::clamp((int)std::floor(frameF_), 0, db_->frameCount - 1), sw, shv); sampleVel(nf, dw, dhv);
    for (int b = 0; b < kHumanBoneCount; ++b) {
        if (!db_->mapped[b]) continue;
        offR_[b] = qabs(glm::normalize(offR_[b] * src.r[b] * glm::inverse(dst.r[b])));
        offRv_[b] = offRv_[b] + sw[b] - dw[b];
    }
    offP_ = offP_ + src.hips - dst.hips; offPv_ = offPv_ + shv - dhv;
    frameF_ = (float)nf;
    rootAt(frameF_, prevRoot_, prevRootYaw_);
    debug.transitions++;
}

void MotionMatcher::search(const vec3& pos, float yaw, const vec3& desiredVel, bool force) {
    const MotionDatabase& db = *db_;
    int cur = std::clamp((int)std::floor(frameF_), 0, db.frameCount - 1);
    float q[kFeatureDim];
    const float* cf = db.feat(cur);
    for (int d = 0; d < 15; ++d) q[d] = cf[d];                         // pose part: continue from where we are
    float moving = glm::length(desiredVel);
    for (int t = 0; t < kTrajSamples; ++t) {
        float tt = db.trajOffsets[t] / db.fps, kk = kLn2 / settings.velHalflife;
        vec3 p = desiredVel * tt + (vel_ - desiredVel) * ((1.f - std::exp(-kk * tt)) / kk);
        float yt = yaw;
        if (haveYaw_ && moving > 0.1f) yt = desiredYaw_ + wrapPi(yaw - desiredYaw_) * std::exp(-kLn2 * tt / settings.yawHalflife);
        vec3 lp = rotY(-yaw, p); float dy = wrapPi(yt - yaw);
        float raw[4] = {lp.x, lp.z, std::sin(dy), std::cos(dy)};
        q[15 + t * 2] = (raw[0] - db.featureMean[15 + t * 2]) / db.featureScale[FG_TrajPos];
        q[15 + t * 2 + 1] = (raw[1] - db.featureMean[15 + t * 2 + 1]) / db.featureScale[FG_TrajPos];
        q[21 + t * 2] = (raw[2] - db.featureMean[21 + t * 2]) / db.featureScale[FG_TrajDir];
        q[21 + t * 2 + 1] = (raw[3] - db.featureMean[21 + t * 2 + 1]) / db.featureScale[FG_TrajDir];
        debug.predicted[t] = pos + rotY(yaw, lp); debug.predictedDir[t] = rotY(yt, vec3(0, 0, 1));
    }
    float w[kFeatureDim]; for (int d = 0; d < kFeatureDim; ++d) w[d] = settings.weights[MotionDatabase::groupOfDim(d)];
    bool standing = moving < 0.05f && glm::length(vel_) < 0.3f;
    if (standing && idleScore_.empty()) buildIdleScores();
    const bool idleBias = standing && !idleScore_.empty() && settings.idlePoseWeight > 0.f;
    auto cost = [&](int f, float limit) {
        const float* x = db.feat(f); float c = idleBias ? settings.idlePoseWeight * idleScore_[f] : 0.f;
        for (int d = 0; d < kFeatureDim; ++d) { float e = q[d] - x[d]; c += w[d] * e * e; if (c > limit) return c; }
        return c;
    };
    uint32_t mask = settings.categoryMask;
    if (settings.contextMask) {                      // context: jumps only on request; gentle speeds only search the Walk family
        if (jumpRequest_ > 0.f) mask |= 1u << 3;
        if (moving < settings.runGateSpeed && (mask & 1u)) mask &= ~(1u << 1);
    }
    float curCost = (db.valid[cur] && ((mask >> db.frameCat[cur]) & 1u)) ? cost(cur, 1e30f) : 1e30f;
    float best = 1e30f; int bestF = cur;
    for (int f = 0; f < db.frameCount; ++f) { if (!db.valid[f] || !((mask >> db.frameCat[f]) & 1u)) continue; float c = cost(f, best); if (c < best) { best = c; bestF = f; } }
    debug.cost = best; debug.currentCost = curCost;
    for (int d = 0; d < kFeatureDim; ++d) {
        int g = MotionDatabase::groupOfDim(d);
        debug.queryRaw[d] = q[d] * db.featureScale[g] + db.featureMean[d];
        debug.matchRaw[d] = db.feat(bestF)[d] * db.featureScale[g] + db.featureMean[d];
        debug.currentRaw[d] = cf[d] * db.featureScale[g] + db.featureMean[d];
    }
    debug.matchPos = pos; debug.matchYaw = yaw; debug.hasSearch = true;
    bool sameRegion = db.frameClip[bestF] == db.frameClip[cur] && std::abs(bestF - cur) < 4;
    if ((force || best + settings.switchMargin < curCost) && !sameRegion) { transitionTo(bestF); }
}

void MotionMatcher::buildIdleScores() {
    const MotionDatabase& db = *db_;
    idleScore_.assign((size_t)db.frameCount, 0.f);
    static const HumanBone bones[] = {HumanBone::Spine, HumanBone::Chest, HumanBone::UpperChest, HumanBone::Neck, HumanBone::Head,
                                      HumanBone::LUpperArm, HumanBone::RUpperArm, HumanBone::LLowerArm, HumanBone::RLowerArm};
    constexpr int nb = (int)(sizeof(bones) / sizeof(bones[0]));
    auto rawf = [&](int f, int d) { int g = MotionDatabase::groupOfDim(d); return db.feat(f)[d] * db.featureScale[g] + db.featureMean[d]; };
    std::vector<quat> mean(nb, quat(0, 0, 0, 0)); std::vector<quat> ref(nb);
    std::vector<int> still;
    for (int f = 0; f < db.frameCount; ++f) {
        if (!db.valid[f] || db.frameCat[f] > 1) continue;                           // Walk / Run families only
        float hv = std::sqrt(rawf(f, 12) * rawf(f, 12) + rawf(f, 13) * rawf(f, 13) + rawf(f, 14) * rawf(f, 14));
        float fv = 0; for (int d = 6; d < 12; ++d) fv = std::max(fv, std::abs(rawf(f, d)));
        if (hv < 0.12f && fv < 0.25f) still.push_back(f);
    }
    if (still.size() < 20) return;                                                  // nothing to learn from: leave all scores at 0
    for (int b = 0; b < nb; ++b) ref[b] = db.localRot[(size_t)still[0] * kHumanBoneCount + (int)bones[b]];
    for (int f : still) for (int b = 0; b < nb; ++b) {
        quat q = db.localRot[(size_t)f * kHumanBoneCount + (int)bones[b]];
        if (glm::dot(q, ref[b]) < 0) q = -q;
        mean[b] += q;
    }
    for (int b = 0; b < nb; ++b) mean[b] = glm::normalize(mean[b]);
    for (int f = 0; f < db.frameCount; ++f) {
        if (!db.valid[f]) continue;
        float s = 0;
        for (int b = 0; b < nb; ++b) {
            quat q = db.localRot[(size_t)f * kHumanBoneCount + (int)bones[b]];
            s += 2.f * std::acos(std::min(1.f, std::abs(glm::dot(q, mean[b]))));
        }
        idleScore_[f] = s;
    }
}

void MotionMatcher::playClip(int clip, const vec3& pos, float yaw) {
    if (!db_ || clip < 0 || clip >= (int)db_->clipStart.size()) return;
    if (!started_) reset(pos, yaw);
    int start = db_->clipStart[clip];
    for (int f = db_->clipStart[clip]; f < db_->clipEnd[clip]; ++f) if (db_->valid[f]) { start = f; break; }
    transitionTo(start);
    browse_ = true;
}

void MotionMatcher::reset(const vec3& pos, float yaw) {
    if (!db_) return;
    vel_ = vec3(0); haveYaw_ = false; searchTimer_ = 0;
    for (auto& q : offR_) q = quat(1, 0, 0, 0);
    for (auto& v : offRv_) v = vec3(0);
    offP_ = offPv_ = vec3(0);
    // start at the most "standing" frame: search with zero desired velocity
    browse_ = false;
    frameF_ = (float)db_->clipStart[0];
    for (size_t c = 0; c < db_->clipStart.size(); ++c) {
        if (!((settings.categoryMask >> db_->clipCategory[c]) & 1u)) continue;
        bool found = false;
        for (int f = db_->clipStart[c]; f < db_->clipEnd[c]; ++f) if (db_->valid[f]) { frameF_ = (float)f; found = true; break; }
        if (found) break;
    }
    rootAt(frameF_, prevRoot_, prevRootYaw_);
    search(pos, yaw, vec3(0), true);
    offR_.fill(quat(1, 0, 0, 0)); offRv_.fill(vec3(0)); offP_ = offPv_ = vec3(0);
    debug.transitions = 0; started_ = true;
}

void MotionMatcher::update(float dt, const vec3& desiredVel, vec3& pos, float& yaw, Pose& pose) {
    if (!db_ || dt <= 0) return;
    if (!started_) reset(pos, yaw);
    float sp = glm::length(desiredVel);
    if (sp > 0.05f) { desiredYaw_ = std::atan2(desiredVel.x, desiredVel.z); haveYaw_ = true; }
    debug.desiredVelocity = desiredVel;

    searchTimer_ += dt; jumpRequest_ = std::max(0.f, jumpRequest_ - dt);
    int cur = std::clamp((int)std::floor(frameF_), 0, db_->frameCount - 1);
    int clip = db_->frameClip[cur];
    if (browse_ && sp > 0.1f) browse_ = false;                 // player moves -> back to motion matching
    bool nearEnd = !db_->valid[cur] || cur + 2 >= db_->clipEnd[clip];
    if (browse_) {
        if (nearEnd) {                                          // loop the clip (skipping the T-pose lead-in)
            int start = db_->clipStart[clip];
            for (int f = db_->clipStart[clip]; f < db_->clipEnd[clip]; ++f) if (db_->valid[f]) { start = f; break; }
            transitionTo(start);
        }
    } else if (searchTimer_ >= settings.searchInterval || nearEnd) { searchTimer_ = 0; search(pos, yaw, desiredVel, nearEnd); }

    // advance playback (clamped so we never run past the clip end)
    clip = db_->frameClip[std::clamp((int)std::floor(frameF_), 0, db_->frameCount - 1)];
    frameF_ = std::min(frameF_ + dt * db_->fps, (float)db_->clipEnd[clip] - 1.001f);

    // root motion
    vec3 r; float ry; rootAt(frameF_, r, ry);
    vec3 localDelta = rotY(-prevRootYaw_, r - prevRoot_);
    float dyaw = wrapPi(ry - prevRootYaw_);
    pos += rotY(yaw, localDelta); yaw = wrapPi(yaw + dyaw);
    vec3 worldVel = rotY(yaw, localDelta) / dt;
    vel_ = glm::mix(vel_, worldVel, 1.f - std::exp(-dt / 0.06f));
    prevRoot_ = r; prevRootYaw_ = ry;

    // pose + inertialisation decay
    Sample s; samplePose(frameF_, s);
    for (int b = 0; b < kHumanBoneCount; ++b) {
        if (!db_->mapped[b]) continue;
        vec3 x = quatToScaledAA(offR_[b]); decay(x, offRv_[b], settings.inertHalflife, dt); offR_[b] = quatFromScaledAA(x);
        pose.r[rig_->human[b]] = glm::normalize(offR_[b] * s.r[b]);
    }
    decay(offP_, offPv_, settings.inertHalflife, dt);
    pose.t[rig_->human[HB(Hips)]] = s.hips + offP_;

    int fi = (int)std::floor(frameF_);
    debug.frame = fi - db_->clipStart[db_->frameClip[fi]]; debug.clip = db_->frameClip[fi]; debug.velocity = vel_;
}
}  // namespace sk
