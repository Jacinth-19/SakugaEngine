// BVH parsing: the behaviour the engine has to match the reference importer on (reference/unity/BVHParser.cs), and the
// corruption cases it has to reject. Written after the port was found to (a) reject lowercase keywords that the reference
// accepts, (b) truncate joint names containing spaces, (c) silently zero-fill a truncated motion section, and (d) turn a
// millisecond "Frame Time" into a 0.03 fps clip.
#include "Check.h"
#include "anim/Bvh.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sk;

namespace {

std::string writeTemp(const std::string& name, const std::string& text) {
    std::filesystem::path p = std::filesystem::temp_directory_path() / ("sakuga_bvh_" + name + ".bvh");
    std::ofstream f(p, std::ios::binary);
    f << text;
    f.close();
    return p.string();
}

// root + one joint + an End Site, three frames (9 channels per frame).
std::string goodBvh() {
    return "HIERARCHY\n"
           "ROOT Hips\n"
           "{\n"
           "  OFFSET 0.0 90.0 0.0\n"
           "  CHANNELS 6 Xposition Yposition Zposition Zrotation Yrotation Xrotation\n"
           "  JOINT LeftArm\n"
           "  {\n"
           "    OFFSET 20.0 0.0 0.0\n"
           "    CHANNELS 3 Zrotation Xrotation Yrotation\n"
           "    End Site\n"
           "    {\n"
           "      OFFSET 0.0 0.0 50.0\n"
           "    }\n"
           "  }\n"
           "}\n"
           "MOTION\n"
           "Frames: 3\n"
           "Frame Time: 0.033333\n"
           "0 90 0 0 0 0 0 0 0\n"
           "1 90 0 0 0 0 10 20 30\n"
           "2 90 0 0 0 0 40 50 60\n";
}

}  // namespace

int main() {
    SK_TEST("bvh_parse_test");

    // ------------------------------------------------------------------ the basic contract
    {
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("good", goodBvh()), c, &err), err);
        CHECK_NEAR((double)c.joints.size(), 2.0, 0);            // the End Site is not a joint
        CHECK_NEAR((double)c.frameCount, 3.0, 0);
        CHECK_NEAR((double)c.channelCount, 9.0, 0);
        CHECK_NEAR(c.frameTime, 1.f / 30.f, 0.0005f);
        CHECK_NEAR(c.fps(), 30.f, 0.05f);
        CHECK_NEAR((double)c.data.size(), 27.0, 0);

        CHECK_MSG(c.joints[0].name == "Hips", "root name");
        CHECK_MSG(c.joints[1].name == "LeftArm", "child name");
        CHECK_NEAR((double)c.joints[0].parent, -1.0, 0);
        CHECK_NEAR((double)c.joints[1].parent, 0.0, 0);
        CHECK_NEAR(c.joints[1].offset.x, 20.f, 1e-5f);
        CHECK_NEAR(c.joints[1].offset.y, 0.f, 1e-5f);
        CHECK_NEAR((double)c.joints[0].channelStart, 0.0, 0);
        CHECK_NEAR((double)c.joints[1].channelStart, 6.0, 0);
        // channel ids: 0..2 = position, 3..5 = rotation, in the order the file lists them
        CHECK_NEAR((double)c.joints[0].channels[3], 5.0, 0);    // Zrotation
        CHECK_NEAR((double)c.joints[1].channels[0], 5.0, 0);    // Zrotation
        CHECK_NEAR((double)c.joints[1].channels[1], 3.0, 0);    // Xrotation
        CHECK_NEAR((double)c.joints[1].channels[2], 4.0, 0);    // Yrotation
        // values land in file order
        CHECK_NEAR(c.data[0], 0.f, 1e-6f);
        CHECK_NEAR(c.data[1], 90.f, 1e-6f);
        CHECK_NEAR(c.data[9 + 6], 10.f, 1e-6f);                  // frame 1, joint 1, Zrotation
        CHECK_NEAR(c.data[18 + 8], 60.f, 1e-6f);                 // frame 2, joint 1, Yrotation
        CHECK_NEAR((double)(c.findJoint("LeftArm") >= 0), 1.0, 0);
        CHECK_NEAR((double)c.findJoint("Nope"), -1.0, 0);
    }

    // ------------------------------------------------------------------ tolerance the reference parser has
    {
        // lowercase keywords and channel names (BVHParser.cs uppercases through its char map)
        std::string lower = goodBvh();
        for (const char* kw : {"HIERARCHY", "ROOT", "JOINT", "OFFSET", "CHANNELS", "MOTION", "Frames:", "Frame Time:"}) {
            size_t pos = 0;
            while ((pos = lower.find(kw, pos)) != std::string::npos) {
                std::string rep = kw;
                for (char& ch : rep) ch = (char)std::tolower((unsigned char)ch);
                lower.replace(pos, std::strlen(kw), rep);
                pos += rep.size();
            }
        }
        for (const char* ch : {"Xposition", "Yposition", "Zposition", "Xrotation", "Yrotation", "Zrotation"}) {
            size_t pos = 0;
            while ((pos = lower.find(ch, pos)) != std::string::npos) {
                std::string rep = ch;
                for (char& c : rep) c = (char)std::tolower((unsigned char)c);
                lower.replace(pos, std::strlen(ch), rep);
                pos += rep.size();
            }
        }
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("lower", lower), c, &err), "lowercase BVH is accepted: " + err);
        CHECK_NEAR((double)c.joints.size(), 2.0, 0);
        CHECK_NEAR((double)c.frameCount, 3.0, 0);
        CHECK_NEAR(c.data[1], 90.f, 1e-6f);
    }
    {
        // a joint name with spaces, and '{' on the same line (BVHParser.cs reads the rest of the line as the name)
        std::string spaced = goodBvh();
        size_t pos = spaced.find("JOINT LeftArm\n  {");
        CHECK(pos != std::string::npos);
        spaced.replace(pos, std::string("JOINT LeftArm\n  {").size(), "JOINT Left Upper Arm {");
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("spaced", spaced), c, &err), err);
        CHECK_NEAR((double)c.joints.size(), 2.0, 0);
        CHECK_MSG(c.joints[1].name == "Left Upper Arm", "the whole name is kept: '" + c.joints[1].name + "'");
        CHECK(c.findJoint("Left Upper Arm") == 1);
        // the hierarchy is unchanged
        CHECK_NEAR((double)c.joints[1].parent, 0.0, 0);
    }
    {
        // scientific notation and comma decimals (both handled by BVHParser.cs getFloat)
        std::string nums = "HIERARCHY\nROOT R\n{\nOFFSET 1e2 0 0\nCHANNELS 3 Xrotation Yrotation Zrotation\n}\n"
                           "MOTION\nFrames: 1\nFrame Time: 0.033333\n1e2 0,5 -2.5e-1\n";
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("numbers", nums), c, &err), err);
        CHECK_NEAR(c.joints[0].offset.x, 100.f, 1e-4f);
        CHECK_NEAR(c.data[0], 100.f, 1e-4f);
        CHECK_NEAR(c.data[1], 0.5f, 1e-6f);
        CHECK_NEAR(c.data[2], -0.25f, 1e-6f);
    }
    {
        // "End Site" in any case, and a joint-only hierarchy without one
        std::string es = goodBvh();
        size_t pos = es.find("End Site");
        es.replace(pos, 8, "end site");
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("endsite", es), c, &err), err);
        CHECK_NEAR((double)c.joints.size(), 2.0, 0);
    }

    // ------------------------------------------------------------------ frame time handling
    {
        std::string ms = goodBvh();
        size_t pos = ms.find("Frame Time: 0.033333");
        ms.replace(pos, 20, "Frame Time: 33.3333");          // milliseconds, as some exporters write them
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("millis", ms), c, &err), err);
        CHECK_NEAR(c.frameTime, 0.0333333f, 1e-4f);
        CHECK_NEAR(c.fps(), 30.f, 0.05f);

        std::string bogus = goodBvh();
        pos = bogus.find("Frame Time: 0.033333");
        bogus.replace(pos, 20, "Frame Time: 0.0000001");     // implausible: falls back to 30 fps instead of 10 MHz
        CHECK(loadBvh(writeTemp("bogusft", bogus), c, &err));
        CHECK_NEAR(c.fps(), 30.f, 0.05f);
    }

    // ------------------------------------------------------------------ corruption is rejected, never zero-filled
    {
        BvhClip c; std::string err;
        // declared 3 frames but only 1.5 frames of data
        std::string trunc = goodBvh();
        trunc = trunc.substr(0, trunc.find("0 90 0 0 0 0 0 0 0") + 20);
        CHECK(!loadBvh(writeTemp("trunc", trunc), c, &err));
        CHECK_MSG(err.find("trunc") != std::string::npos, "the error says the data is truncated: " + err);

        // a file that ends in the middle of the data at all
        std::string cut = goodBvh();
        cut = cut.substr(0, cut.size() - 30);
        CHECK(!loadBvh(writeTemp("cut", cut), c, &err));

        // missing MOTION
        std::string noMotion = goodBvh();
        noMotion = noMotion.substr(0, noMotion.find("MOTION"));
        CHECK(!loadBvh(writeTemp("nomotion", noMotion), c, &err));

        // unbalanced braces
        std::string unbalanced = goodBvh();
        unbalanced.replace(unbalanced.find("}\nMOTION"), 2, "\nMOTION");
        CHECK(!loadBvh(writeTemp("unbalanced", unbalanced), c, &err));

        // zero / negative / absurd frame counts
        for (const char* frames : {"0", "-5", "2000000000"}) {
            std::string bad = goodBvh();
            size_t pos = bad.find("Frames: 3");
            bad.replace(pos, 9, std::string("Frames: ") + frames);
            CHECK(!loadBvh(writeTemp("frames", bad), c, &err));
        }

        // CHANNELS with no joint open: previously read out of bounds
        std::string noJoint = "HIERARCHY\nCHANNELS 3 Xrotation Yrotation Zrotation\n}\nMOTION\nFrames: 1\nFrame Time: 0.033333\n0 0 0\n";
        CHECK(!loadBvh(writeTemp("nojoint", noJoint), c, &err));

        // an unknown channel name
        std::string badChannel = goodBvh();
        size_t pos = badChannel.find("Zrotation Xrotation Yrotation");
        badChannel.replace(pos, 31, "Zrotation Xrotation Wrotation");
        CHECK(!loadBvh(writeTemp("badchannel", badChannel), c, &err));

        // a joint inside an End Site
        std::string nested = goodBvh();
        pos = nested.find("OFFSET 0.0 0.0 50.0");
        nested.replace(pos, 21, "OFFSET 0.0 0.0 50.0\n JOINT Inner\n {\n OFFSET 0 0 0\n CHANNELS 3 Xrotation Yrotation Zrotation\n }");
        CHECK(!loadBvh(writeTemp("nested", nested), c, &err));

        // a missing file
        CHECK(!loadBvh("/tmp/sakuga_no_such_clip.bvh", c, &err));
        CHECK(!err.empty());
    }

    // ------------------------------------------------------------------ forward kinematics
    {
        // Rotation channels compose in the file's order: Z then X, both 90 degrees. A child 1 m up ends up on +Z;
        // the reversed order would put it on -X.
        std::string rot = "HIERARCHY\nROOT R\n{\nOFFSET 0 0 0\nCHANNELS 2 Zrotation Xrotation\n"
                          "JOINT Child\n{\nOFFSET 0 100 0\nCHANNELS 1 Xrotation\n}\n}\n"
                          "MOTION\nFrames: 1\nFrame Time: 0.033333\n90 90 0\n";
        BvhClip c; std::string err;
        CHECK_MSG(loadBvh(writeTemp("rot", rot), c, &err), err);
        std::vector<glm::vec3> pos; std::vector<glm::quat> rq;
        c.fk(0, 0.01f, pos, rq);
        CHECK_NEAR((double)pos.size(), 2.0, 0);
        // root rotates by Rz(90) * Rx(90); its child offset (0,1,0) goes to +Z
        CHECK_NEAR(pos[1].x, 0.f, 1e-4f);
        CHECK_NEAR(pos[1].y, 0.f, 1e-4f);
        CHECK_NEAR(pos[1].z, 1.f, 1e-4f);
        // rotations are unit quaternions
        for (auto& q : rq) CHECK_NEAR(glm::length(q), 1.f, 1e-4f);
    }

    // ------------------------------------------------------------------ the vendored LAFAN1 clips
    {
        for (const char* name : {"walk1_subject1.bvh", "run1_subject2.bvh"}) {
            BvhClip c; std::string err;
            CHECK_MSG(loadBvh(std::string(SAKUGA_ASSET_DIR) + "/motion/" + name, c, &err), std::string(name) + ": " + err);
            if (c.joints.empty()) continue;
            CHECK_NEAR((double)c.joints.size(), 22.0, 0);        // 27 bones - 5 End Sites
            CHECK_NEAR(c.fps(), 30.f, 0.05f);
            CHECK_NEAR((double)c.channelCount, 69.0, 0);        // 6 for the root + 21 * 3
            CHECK_NEAR((double)(c.data.size() / (size_t)c.channelCount), (double)c.frameCount, 0);
            // Y-up, centimetres, feet below the hips
            std::vector<glm::vec3> pos; std::vector<glm::quat> rq;
            c.fk(0, 0.01f, pos, rq);
            int hips = c.findJoint("Hips"), lf = c.findJoint("LeftFoot"), rf = c.findJoint("RightFoot");
            CHECK(hips >= 0 && lf >= 0 && rf >= 0);
            CHECK_MSG(pos[hips].y > 0.5f && pos[hips].y < 2.f, "hips ~1 m above the origin (cm scaled to m)");
            CHECK_MSG(pos[hips].y - pos[lf].y > 0.5f, "feet below the hips: the clip is Y-up");
            CHECK_MSG(pos[hips].y - pos[rf].y > 0.5f, "feet below the hips (right)");
            CHECK_MSG(std::fabs(pos[lf].y) < 0.5f, "feet near the ground plane");
            for (auto& p : pos) { CHECK_FINITE(p.x); CHECK_FINITE(p.y); CHECK_FINITE(p.z); }
            // every value in the motion section is a real number (no silent zeros from a mis-parse)
            int zeros = 0;
            for (size_t i = 0; i < c.data.size(); i += 97) if (c.data[i] == 0.f) ++zeros;
            CHECK_MSG(zeros < (int)(c.data.size() / 97), "the motion data is populated");
        }
    }

    return t_.finish();
}
