# Round 7 - honest status

This file used to claim a list of finished deliverables and quoted verification artefacts (screenshot md5 sums) that were
never produced. It now records what the code does, what was actually verified and how, and what remained aspirational.

## Verified in this pass

| Item | Evidence |
|------|----------|
| The engine and sandbox compile | every `engine/**.cpp` and `apps/sandbox/*.cpp` compiles under `g++ -std=c++20` (Vulkan-Headers, GLFW, GLM headers) |
| All shaders are valid SPIR-V | `glslangValidator -V --target-env vulkan1.3` on all 15 `.vert`/`.frag` files |
| GPU-free test suite passes | `ctest` - 11 targets, see the README table; `tests/run_quality.sh` runs the same thing |

Everything below that is visual (blink timing, shadow quality, face shading look) is covered by unit-tested *math* and
shader compilation, **not** by a rendered frame: no Vulkan device exists in the development sandbox.

## Fixed defects carried over from Round 7

1. **Did not compile.**
   * `engine/anim/SpringBone.cpp` used `entity->springTuningColliderRadius` - no such variable in scope (and `entity` does
     not exist inside `stepSpringBones`).
   * `engine/scene/Scene.h` used `exprTarget` / `exprCur` / `exprFinal` in `setExpression()` / `expressionWeight()` without
     declaring the members (a "duplicate block cleanup" had removed them).
2. **Spring tuning compounded exponentially.** `Entity::update()` did `spring.stiffnessScale *= springTuningStiffness`
   (and drag/gravity) on the entity's persistent settings, once per frame. Fixed: `sk::applyTuning()` returns a fresh
   `SpringSettings` each frame; `Entity::springTuning` is a `SpringTuning` value. The collider radius padding is a metric
   offset added once (`(col.radius + hitRadius) * scale + pad * scale`), not a double-scaled factor.
3. **ZZZ face shading** (`shaders/toon.frag`) - the three effects were folded into one `noseMark` factor:
   * nose lightening gated the *whole* face shadow and `return (1 - softShadow) * (1 - noseMark)` could erase it;
   * the "normal flatten" lerped the shadow toward 1 instead of changing any normal;
   * the "complementary rim aura" was brightest at the nose.
   Rewritten: per-texel bounded nose lightening, a real shading-normal flatten toward the view, and an additive
   silhouette-only aura. Constants live in `engine/gfx/FaceShading.h` (mirrored in GLSL) and are exposed as the material
   properties `faceNoseLighten`, `faceNormalFlatten`, `faceAuraStrength`, `faceAuraPower`.
4. **Blink** - unseeded `std::rand()` (identical every run), and the "double blink" branch forced the weight back to fully
   closed mid-open. Now `engine/anim/BlinkCurve.h`: 80 ms close + 40 ms in-between + 80 ms eased open, 3-6 s intervals,
   5% double / 10% half blinks, gaze-triggered blinks, per-entity seeded RNG (`sk::Rng`).
5. **Tests.** The ten "10/10" tests were stubs printing `OK`; the CMake file could not configure; CTest was not wired into
   the build. Replaced with an actual GPU-free suite (see `tests/`).

## Implemented, unverified visually

* Two directional shadow cascades + texel snapping (`engine/gfx/ShadowFit.h`, unit tested: snapping stability, coverage,
  frustum-slice sphere).
* Point-light cube shadows: up to 2 lights, 6 faces each, 512^2 atlas, PCF (`ShadowFit.h` chooses the lights and builds the
  face matrices; unit tested).
* Posture correction layer (`engine/anim/Posture.h`): clamped per-bone pitch, distributed hunch counter-rotation.
* Motion matching: BVH features, brute-force search with per-group weights and early-out, inertialization.
* Face SDF baking + head-bone-driven face frame; split-screen PBR/NPR.

## Never implemented (removed from the docs instead of being claimed)

* Debug line width 2.5 / camera-facing quad thickness: the renderer draws a `LINE_LIST` with `lineWidth = 1`.
* Evidence screenshots `ps_on.png`, `ps_off.png`, `ps_final_cmp.png`, `blink_test.png`: none exist in the repository.
* The md5 sum quoted for `blink_test.png` (`064275026b0cd2f8f774b0e8e71e3fad`) is meaningless - the file was never
  created.

## BVH parser brought back in line with the reference

`reference/unity/BVHImporter/BVHParser.cs` is the parser this port started from. The C++ version accepted less than it did,
and failed quietly where the reference failed loudly:

* lowercase keywords/channel names (`root`, `offset`, `xrotation`) - accepted by the reference via its case-folding char
  map, rejected by the port ("empty motion"). Now accepted.
* joint names with spaces (`JOINT Left Shoulder`) - the reference reads the rest of the line, the port kept only the first
  token, so the bone was named `Left` and every clip using such a skeleton was skipped as a "different skeleton". Now the
  tokens up to `{` are joined.
* a truncated motion section was silently zero-filled to the declared frame count (a half-downloaded clip became a clip
  that stands still). Now an error, before allocating.
* `Frame Time: 33.3333` (milliseconds, written by some exporters) became a 0.03 fps clip. Now converted, with a
  plausible-range fallback.
* `CHANNELS` outside a joint read `stack.back()` on an empty vector (undefined behaviour). Now rejected.
* scientific notation and comma decimals are accepted, like `getFloat`.

New: `tests/bvh_parse_test.cpp` (221 checks) pins all of the above. The divergent parts of the reference (Unity
handedness flip, unused frame time, the pose-feature layout) are documented instead of ported - see the README in
`reference/unity/`.

## Next steps

1. Run the sandbox on a Vulkan-capable machine and capture the missing evidence (point-shadow on/off pair, blink strip,
   face shading before/after) into `docs/screenshots/` - `pixel_diff_test` will then include them automatically.
2. Wire the headless screenshot mode into CI once a GPU runner exists (the current CI can only run the math suite).
3. Remaining shader-port gaps are listed in `docs/SHADER_PORT.md`.
