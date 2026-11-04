// ZZZ face shading + hair/spring physics.
//
// Face: gfx/FaceShading.h is the reference implementation that shaders/toon.frag mirrors. The old GLSL multiplied the
// whole-face SDF shadow by the nose ellipse and folded the normal flatten and the rim "aura" into that same factor, so
//  * the face shadow vanished wherever the ellipse overlapped it (return (1 - softShadow) * (1 - noseMark)),
//  * the "85% normal flatten" never touched a normal at all, and
//  * the aura was brightest at the nose instead of on the silhouette.
// The checks below pin the three effects to their intended, local behaviour.
//
// Physics: anim/SpringStep.h. The regression under test is the compounding spring tuning (stiffnessScale *= k every frame
// on the entity's persistent settings) and the double-scaled collider radius.
#include "Check.h"
#include "anim/SpringStep.h"
#include "gfx/FaceShading.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace sk;

int main() {
    SK_TEST("face_hair_test");

    // ------------------------------------------------------------------ nose/under-eye lightening
    face::ShadingParams fp;
    {
        CHECK_NEAR(face::noseMask(glm::vec2(0.5f, 0.45f), fp), 1.f, 1e-6f);       // ellipse centre
        CHECK_NEAR(face::noseMask(glm::vec2(0.2f, 0.9f), fp), 0.f, 1e-6f);        // far away: exactly 0 (no global gate)
        CHECK_NEAR(face::noseMask(glm::vec2(0.5f, 0.2f), fp), 0.f, 1e-6f);        // outside the ellipse in v

        // falls off monotonically along +u and reaches 0 at the outline
        float prev = 2.f;
        for (float u = 0.5f; u <= 0.65f; u += 0.01f) {
            float m = face::noseMask(glm::vec2(u, 0.45f), fp);
            CHECK(m >= -1e-6f && m <= 1.f + 1e-6f);
            CHECK(m <= prev + 1e-6f);
            prev = m;
        }
        CHECK_NEAR(face::noseMask(glm::vec2(0.5f + 0.13f, 0.45f), fp), 0.f, 1e-6f);

        // the lightening is bounded, cannot brighten the shadow, and is a no-op outside the ellipse
        const float shadowIn = 0.8f;
        CHECK_NEAR(face::lightenNoseShadow(shadowIn, 0.f, 0.5f), shadowIn, 1e-9f);          // outside: bit-identical
        CHECK_NEAR(face::lightenNoseShadow(shadowIn, 1.f, 0.f), shadowIn, 1e-9f);           // amount 0: no-op
        CHECK_NEAR(face::lightenNoseShadow(shadowIn, 1.f, 0.18f), shadowIn * 0.82f, 1e-6f); // 18% of the shadow released
        CHECK_MSG(face::lightenNoseShadow(shadowIn, 1.f, 0.18f) > 0.f, "the face shadow must not disappear (old bug)");
        CHECK(face::lightenNoseShadow(shadowIn, 1.f, 1.f) <= shadowIn);
        CHECK_NEAR(face::lightenNoseShadow(shadowIn, 1.f, 1.f), 0.f, 1e-6f);                // only at amount 1
        // monotone in the amount and in the mask
        float last = 2.f;
        for (float a = 0.f; a <= 1.f; a += 0.05f) { float v = face::lightenNoseShadow(shadowIn, 1.f, a); CHECK(v <= last + 1e-6f); last = v; }
        last = 2.f;
        for (float m = 0.f; m <= 1.f; m += 0.05f) { float v = face::lightenNoseShadow(shadowIn, m, 0.3f); CHECK(v <= last + 1e-6f); last = v; }   // more mask -> more released
        // out-of-range inputs stay in [0, 1]
        CHECK(face::lightenNoseShadow(-5.f, 2.f, 2.f) >= 0.f);
        CHECK(face::lightenNoseShadow(5.f, 2.f, -2.f) <= 1.f);
    }

    // ------------------------------------------------------------------ normal flattening (a real normal change)
    {
        glm::vec3 n = glm::normalize(glm::vec3(0.f, 0.f, 1.f));
        glm::vec3 v = glm::normalize(glm::vec3(1.f, 0.f, 0.25f));
        CHECK_NEAR(glm::length(face::flattenNormal(n, v, 0.f) - n), 0.f, 1e-6f);              // amount 0: unchanged
        CHECK_NEAR(glm::length(face::flattenNormal(n, v, 1.f) - v), 0.f, 1e-5f);              // amount 1: fully toward the view
        CHECK_NEAR(glm::length(face::flattenNormal(n, v, 0.85f) - glm::normalize(glm::mix(n, v, 0.85f))), 0.f, 1e-6f);

        float prevDot = glm::dot(n, v);
        for (float a = 0.f; a <= 1.f; a += 0.05f) {
            glm::vec3 f = face::flattenNormal(n, v, a);
            CHECK_NEAR(glm::length(f), 1.f, 1e-5f);                                          // always unit length
            float d = glm::dot(f, v);
            CHECK(d >= prevDot - 1e-6f);                                                     // flattening only turns toward the view
            prevDot = d;
        }
        // degenerate inputs must not produce NaN
        glm::vec3 zero(0.f);
        CHECK_FINITE(face::flattenNormal(zero, v, 0.5f).z);
        CHECK_FINITE(face::flattenNormal(n, zero, 0.5f).z);
        CHECK(face::flattenNormal(n, v, -3.f).z != std::numeric_limits<float>::quiet_NaN());
    }

    // ------------------------------------------------------------------ silhouette-only complementary aura
    {
        CHECK_NEAR(face::silhouetteAura(1.f, 0.12f, 2.f), 0.f, 1e-9f);        // facing the camera: nothing
        CHECK_NEAR(face::silhouetteAura(-1.f, 0.12f, 2.f), 0.12f, 1e-6f);     // grazing: full strength
        CHECK_NEAR(face::silhouetteAura(0.f, 0.12f, 1.f), 0.12f, 1e-6f);
        CHECK_NEAR(face::silhouetteAura(0.5f, 0.12f, 0.f), 0.12f, 1e-3f);     // power 0 is clamped, not NaN
        CHECK_NEAR(face::silhouetteAura(0.5f, 0.f, 2.f), 0.f, 1e-9f);         // disabled
        float prev = 2.f;
        for (float d = -1.f; d <= 1.f; d += 0.05f) {                          // dims as the surface turns toward the camera
            float a = face::silhouetteAura(d, 0.2f, 2.f);
            CHECK(a <= prev + 1e-6f);
            CHECK(a >= -1e-6f && a <= 0.2f + 1e-6f);
            prev = a;
        }
        // complementary of a warm white key light is cold, and the tint stays inside the rim colour's control
        glm::vec3 tint = face::complementaryTint(glm::vec3(1.f, 0.92f, 0.82f), glm::vec3(1.f), 0.5f);
        CHECK(tint.b >= tint.r);
        glm::vec3 pure = face::complementaryTint(glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f), 1.f);
        CHECK_NEAR(pure.r, 0.f, 1e-6f);
        CHECK_NEAR(pure.g, 1.f, 1e-6f);
    }

    // ------------------------------------------------------------------ spring tuning is not cumulative
    {
        SpringSettings base;
        base.stiffnessScale = 0.8f; base.dragScale = 1.2f; base.gravityScale = 0.9f;
        SpringTuning tune;
        tune.stiffness = 2.f; tune.drag = 0.5f; tune.gravity = 1.5f; tune.colliderRadiusPad = 0.01f;

        SpringSettings once = applyTuning(base, tune);
        CHECK_NEAR(once.stiffnessScale, 1.6f, 1e-6f);
        CHECK_NEAR(once.dragScale, 0.6f, 1e-6f);
        CHECK_NEAR(once.gravityScale, 1.35f, 1e-6f);
        CHECK_NEAR(once.colliderRadiusPad, 0.01f, 1e-6f);

        // the base is untouched (the old code mutated the entity's persistent settings)
        CHECK_NEAR(base.stiffnessScale, 0.8f, 1e-9f);
        CHECK_NEAR(base.dragScale, 1.2f, 1e-9f);
        CHECK_NEAR(base.gravityScale, 0.9f, 1e-9f);

        // applying it in a loop 1000 times (i.e. 1000 frames) cannot grow: idempotent by construction
        SpringSettings cur = base;
        for (int i = 0; i < 1000; ++i) cur = applyTuning(base, tune);
        CHECK_NEAR(cur.stiffnessScale, once.stiffnessScale, 1e-6f);
        CHECK_NEAR(cur.dragScale, once.dragScale, 1e-6f);
        CHECK_NEAR(cur.gravityScale, once.gravityScale, 1e-6f);
        CHECK_NEAR(cur.colliderRadiusPad, once.colliderRadiusPad, 1e-6f);

        // identity tuning is an exact no-op
        SpringSettings same = applyTuning(base, SpringTuning{});
        CHECK_NEAR(same.stiffnessScale, base.stiffnessScale, 1e-9f);
        CHECK_NEAR(same.gravityScale, base.gravityScale, 1e-9f);
        CHECK_NEAR(same.colliderRadiusPad, 0.f, 1e-9f);

        // negative stiffness would invert the spring: clamped away
        SpringTuning neg; neg.stiffness = -5.f;
        CHECK(applyTuning(base, neg).stiffnessScale >= 0.f);
    }

    // Legal zero vectors and corrupt directions cannot poison the spring solver with NaNs.
    CHECK_NEAR(glm::length(springUnitDirection(glm::vec3(0.f))), 0.f, 1e-9f);
    CHECK_NEAR(glm::length(springUnitDirection(glm::vec3(0.f, -2.f, 0.f))), 1.f, 1e-6f);
    CHECK_FINITE(springUnitDirection(glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f)).x);

    // ------------------------------------------------------------------ verlet step keeps the bone length
    {
        glm::vec3 head(0.f, 2.f, 0.f);
        glm::vec3 restDir(0.f, -1.f, 0.f);
        float len = 0.25f, h = 1.f / 60.f;
        glm::vec3 cur = head + restDir * len, prev = cur;
        for (int i = 0; i < 600; ++i) {
            glm::vec3 next = springTailStep(cur, prev, head, restDir, 0.2f, 4.f, glm::vec3(0, -9.8f, 0), len, h);
            CHECK_NEAR(glm::length(next - head), len, 1e-5f);                  // the length constraint always holds
            CHECK_FINITE(next.x); CHECK_FINITE(next.y); CHECK_FINITE(next.z);
            prev = cur; cur = next;
        }
        // ... and a stretched start is corrected in one step
        glm::vec3 stretched = head + restDir * (len * 5.f);
        CHECK_NEAR(glm::length(springTailStep(stretched, stretched, head, restDir, 0.f, 0.f, glm::vec3(0), len, h) - head), len, 1e-4f);
        // a collapsed tail does not divide by zero
        glm::vec3 atHead = springTailStep(head, head, head, restDir, 0.f, 0.f, glm::vec3(0), len, h);
        CHECK_FINITE(atHead.x);
        CHECK_NEAR(glm::length(atHead - head), len, 1e-4f);

        // with no velocity and no external force the tail settles on the rest direction
        glm::vec3 c = head + glm::vec3(0.4f, 0.f, 0.3f), p = c;
        for (int i = 0; i < 2000; ++i) {
            glm::vec3 n2 = springTailStep(c, p, head, restDir, 0.5f, 1.f, glm::vec3(0), len, h);
            p = c; c = n2;
        }
        float cosAngle = glm::dot(glm::normalize(c - head), restDir);
        CHECK_MSG(cosAngle > 0.99f, "the tail converges to the rest direction");
    }

    // ------------------------------------------------------------------ collider push-out
    {
        glm::vec3 head(0.f, 0.f, 0.f), radius(1.f, 0.f, 0.f);
        float len = 0.5f;
        // A collider whose sphere and the bone length cannot both be satisfied (it sits exactly on the tail): the bone
        // length wins, but the tail is pushed away from the collider and the result stays finite.
        glm::vec3 tail = head + radius * len;
        glm::vec3 volumeCentre = tail;
        glm::vec3 out = springPushOut(tail, volumeCentre, volumeCentre, 0.2f, head, len);
        CHECK_NEAR(glm::length(out - head), len, 1e-5f);
        CHECK_MSG(glm::length(out - volumeCentre) > 0.f, "the tail leaves the collider centre");

        // A collider that only grazes the tail: both constraints are satisfiable, so both must hold.
        glm::vec3 graze = head + glm::vec3(len, 0.19f, 0.f);        // 0.19 m from the tail, radius 0.2
        glm::vec3 out3 = springPushOut(tail, graze, graze, 0.2f, head, len);
        CHECK_NEAR(glm::length(out3 - head), len, 1e-5f);
        CHECK_MSG(glm::length(out3 - graze) >= 0.2f - 2e-3f, "the tail ends up outside the collider");
        // a capsule behaves like the closest point on its segment
        glm::vec3 a = head + glm::vec3(0, 0.1f, 0), b = a + glm::vec3(0, 0.6f, 0);
        glm::vec3 capTail = head + radius * len;
        glm::vec3 out2 = springPushOut(capTail, a, b, 0.15f, head, len);
        CHECK(glm::length(out2 - closestOnSegment(out2, a, b)) >= 0.15f - 1e-4f);
        CHECK_NEAR(glm::length(out2 - head), len, 1e-5f);
        // no overlap -> untouched
        glm::vec3 far = head + radius * len;
        CHECK_NEAR(glm::length(springPushOut(far, tail + glm::vec3(5, 0, 0), tail + glm::vec3(5, 0, 0), 0.1f, head, len) - far), 0.f, 1e-9f);
        // zero radius is a no-op, not a division by zero
        CHECK_NEAR(glm::length(springPushOut(far, tail, tail, 0.f, head, len) - far), 0.f, 1e-9f);
    }

    // ------------------------------------------------------------------ state reset clears the simulation
    {
        SpringState st;
        st.chains.resize(1);
        st.chains[0].resize(2);
        st.chains[0][0].init = true; st.chains[0][1].init = true;
        st.inited = true;
        st.reset();
        CHECK(!st.inited);
        CHECK(!st.chains[0][0].init);
        CHECK(!st.chains[0][1].init);
    }

    return t_.finish();
}
