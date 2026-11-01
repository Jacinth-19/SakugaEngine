// Debug line plumbing (gfx/LineBatch.h): the renderer binds a LINE_LIST pipeline, so every segment is a vertex pair.
// Round 7 claimed "line width 2.5" and a "camera-facing quad offset"; neither exists (the pipeline is lineWidth = 1 and the
// buffer holds plain pairs), so this test pins what the code actually does: packing, pair integrity, the vertex budget and
// the skeleton/polyline builders.
#include "Check.h"
#include "gfx/LineBatch.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <vector>

using namespace sk;

int main() {
    SK_TEST("debug_lines_test");

    // ------------------------------------------------------------------ colour packing (R8G8B8A8_UNORM, memory order)
    {
        CHECK_NEAR((double)packColor(1.f, 0.f, 0.f, 1.f), (double)0xFF0000FFu, 0);   // r | g<<8 | b<<16 | a<<24
        CHECK_NEAR((double)packColor(0.f, 1.f, 0.f, 1.f), (double)0xFF00FF00u, 0);
        CHECK_NEAR((double)packColor(0.f, 0.f, 1.f, 1.f), (double)0xFFFF0000u, 0);
        CHECK_NEAR((double)packColor(0.f, 0.f, 0.f, 0.f), 0.0, 0);
        CHECK_NEAR((double)(packColor(1.f, 1.f, 1.f, 1.f) & 0xFF), 255.0, 0);
        // out-of-range input is clamped, not wrapped
        CHECK_NEAR((double)packColor(-3.f, 5.f, 0.5f, 2.f), (double)packColor(0.f, 1.f, 0.5f, 1.f), 0);
        // mid grey round-trips
        uint32_t c = packColor(0.5f, 0.5f, 0.5f, 1.f);
        CHECK_NEAR((double)(c & 0xFF), 128.0, 0);      // 0.5 * 255 + 0.5 = 128
    }

    // ------------------------------------------------------------------ one segment = two vertices
    {
        std::vector<LineVertex> lines;
        appendLine(lines, glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f), 0xFF00FF00u);
        CHECK_NEAR((double)lines.size(), 2.0, 0);
        CHECK_NEAR(lines[0].pos.x, 0.f, 1e-9f);
        CHECK_NEAR(lines[1].pos.x, 1.f, 1e-9f);
        CHECK_NEAR((double)lines[0].rgba, (double)lines[1].rgba, 0);

        // degenerate segments are dropped (they would be invisible but still cost a slot)
        appendLine(lines, glm::vec3(1.f), glm::vec3(1.f), 0xFFFFFFFFu);
        CHECK_NEAR((double)lines.size(), 2.0, 0);

        // the vertex count is always even: the renderer memcpys `size & ~1`
        for (int i = 0; i < 17; ++i) appendLine(lines, glm::vec3((float)i, 0, 0), glm::vec3((float)i, 1, 0), 0xFF0000FFu);
        CHECK_NEAR((double)(lines.size() % 2), 0.0, 0);
    }

    // ------------------------------------------------------------------ vertex budget
    {
        std::vector<LineVertex> lines;
        size_t cap = 10;
        int appended = 0;
        for (int i = 0; i < 20; ++i) appended += appendLineCapped(lines, glm::vec3((float)i, 0, 0), glm::vec3((float)i, 1, 0), 1u, cap) ? 1 : 0;
        CHECK(lines.size() <= cap);
        CHECK_NEAR((double)(lines.size() % 2), 0.0, 0);         // never a half-written segment
        CHECK_NEAR((double)appended, 5.0, 0);                    // 5 segments of 2 vertices fit in 10
        // a full buffer refuses to append
        CHECK(!appendLineCapped(lines, glm::vec3(0.f), glm::vec3(1.f), 1u, cap));
    }

    // ------------------------------------------------------------------ skeleton segments
    {
        std::vector<int> parent = {-1, 0, 1, 2, -1};
        std::vector<glm::mat4> globals(5, glm::mat4(1.f));
        for (int i = 0; i < 5; ++i) globals[i][3] = glm::vec4(0.f, (float)i, 0.f, 1.f);
        std::vector<char> isJoint = {1, 1, 1, 0, 1};
        std::vector<LineVertex> lines;
        appendSkeletonSegments(lines, parent, globals, isJoint, glm::mat4(1.f), 0xFF00FF00u);
        // pairs: (0,1), (1,2) - (2,3) is skipped because 3 is not a joint, 4 has no joint parent
        CHECK_NEAR((double)lines.size(), 4.0, 0);
        CHECK_NEAR(lines[0].pos.y, 0.f, 1e-6f);
        CHECK_NEAR(lines[1].pos.y, 1.f, 1e-6f);
        CHECK_NEAR(lines[2].pos.y, 1.f, 1e-6f);
        CHECK_NEAR(lines[3].pos.y, 2.f, 1e-6f);

        // the entity transform is applied
        std::vector<LineVertex> moved;
        glm::mat4 T = glm::translate(glm::mat4(1.f), glm::vec3(10.f, 0.f, 0.f));
        appendSkeletonSegments(moved, parent, globals, isJoint, T, 1u);
        CHECK_NEAR(moved[0].pos.x, 10.f, 1e-6f);

        // mismatched array sizes are truncated, not read out of bounds
        std::vector<LineVertex> truncated;
        appendSkeletonSegments(truncated, parent, std::vector<glm::mat4>(2, glm::mat4(1.f)), isJoint, glm::mat4(1.f), 1u);
        CHECK(truncated.size() <= 2);
    }

    // ------------------------------------------------------------------ polylines (trajectory prediction)
    {
        std::vector<glm::vec3> pts = {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}};
        std::vector<LineVertex> lines;
        appendPolyline(lines, pts, 0xFF00FF00u);
        CHECK_NEAR((double)lines.size(), 6.0, 0);                 // 3 segments
        CHECK_NEAR(lines.front().pos.z, 0.f, 1e-9f);
        CHECK_NEAR(lines.back().pos.z, 3.f, 1e-9f);
        // duplicate samples do not produce zero-length segments
        std::vector<glm::vec3> dup = {{0, 0, 0}, {0, 0, 0}, {0, 0, 1}};
        std::vector<LineVertex> d;
        appendPolyline(d, dup, 1u);
        CHECK_NEAR((double)d.size(), 2.0, 0);
        // a single point is not a line
        std::vector<LineVertex> one;
        appendPolyline(one, std::vector<glm::vec3>{{0, 0, 0}}, 1u);
        CHECK(one.empty());
    }

    return t_.finish();
}
