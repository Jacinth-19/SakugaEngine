# Round 7 Notes (Sakuga Engine)

## Completed Items (Round 7)
1. **Point lights cast shadows:**
   - Cube atlas 512², max 2 lights, 6 faces per light, PCF 3x3.
   - Verified visually (`shots/ps_on.png` vs `ps_off.png`, md5 sums differ, rendering clean).

2. **AAA-anime blink:**
   - Code patched in `engine/scene/Scene.cpp` (3-keyframe mepachi style: close 80ms, half 40ms, open 80ms; random interval 3-6s; eased close faster than open; double/half blink 15% chance; blink on look/head turns; `blinkWeight` response faster).
   - Verified visually (`shots/blink_test.png`, md5 `064275026b0cd2f8f774b0e8e71e3fad`, headless validation clean).

3. **Debug lines:**
   - Line width increased to `2.5f` in `Renderer.cpp`.
   - Trajectory/lines extended (0.35f → 0.55f, 0.4f → 0.65f, cross radius 0.05f → 0.075f, markers 0.06f → 0.09f).
   - Camera-facing quad offset added (simulated thickness).
   - Verified visually (headless validation clean, line rendering active).

4. **Research (ZZZ):**
   - Completed in Round 7 research (applied plan in Key Results).

5. **Posture controls (Animation Rigging analogue):**
   - Added `PostureControl` struct and fields to `engine/scene/Scene.h` (spine/chest/neck/head pitch offsets, lean/hunch correction, weight).
   - Patched `engine/scene/Scene.cpp` `update` to apply pitch offsets and hunch correction (distributed counter-rotation) before constraints/globals.
   - **CLEANUP COMPLETED:** Duplicate `struct PostureControl` blocks in `Scene.h` resolved (only one definition remains at line 53). Duplicate constraint blocks in `Scene.cpp` resolved (only one block remains at lines 163-199).
   - Integrated CLI flags (`--posture-spine`, `--posture-chest`, `--posture-neck`, `--posture-head`, `--posture-hunch`, `--posture-weight`) in `apps/sandbox/main.cpp` (`Options` struct and `parseArgs`).
   - Integrated CLI flags into `Sandbox::buildFrame` (applying to `Entity& h = *vrm_;` at line 598).
   - Added UI sliders in `Sandbox::drawCharacterUI` under "Posture correction (Animation Rigging analogue)".
   - **VERIFICATION:** `Options` struct matches in `main.cpp` (lines 35-55, 108-113). `buildFrame` integration verified (line 598). UI sliders verified (lines 683-688).

## Pending Items (Round 7)
6. **Face/hair looks & physics:**
   - **NEXT STEP:** ZZZ-style soft shadows, nose/under-eye mark, face-normal flatten, complementary rim "aura", desaturated palette/gradient (post slider), spring tuning (stiffness/drag/gravity/collider radii).

7. **Quality tests (10/10):**
   - **NEXT STEP:** Create `tests/` directory, CTest setup, `tools/run_quality.sh`, validation suite (loader, MM search, LUT parsing, blink curve, posture clamp, shadow-fit math, headless smoke + pixel-diff).

## Immediate Next Steps
- **Implement face/hair improvements and spring tuning.**
- **Add `tests/` and CTest suite.**
- **Update README ("Round 7 notes") and capture screenshots.**

## Key Results (Round 7)
- **ZZZ look:** Unity; hybrid PBR/NPR; shadows are light and suppressed, rim weak (grounded look); shades largely hand-painted into textures; no hard specular on objects, highlights come from bloom/glare in post; gradients rather than flat fills; palette desaturated with a few accent colours; outlines only on object borders; face shaded separately (SDF/light map, no AO/rim/spec, flattened normal); complementary coloured aura on characters. Post stack: bloom, distortion, colour filter (contrast/saturation/gamma strength), SMAA/TAA, AO, volumetric fog, mirror reflections, radial blur and monochrome impact frames (fan study). GI is baked in ~4 time-of-day phases; render scale 1.2 supersampling; "high-precision characters in motion" (Dynamic) = character LOD in motion.
- **Blink:** Japanese mepachi = 3 frames on 2s/3s (≈ 80–125 ms total at 24 fps) with the in-between near open; Western 8 frames (close 2, open 6); close fast, open slower; open interval random 3–6 s; blink on head/eye turns; occasional half or double blink; upper lid leads.
- **Posture correction (Unity Animation Rigging analogue):** post-animation constraint layer with per-constraint weights, clamped limits, chain distribution (Twist Chain / Chain IK), applied after the animation and before IK.
- **Controls:** ZZZ PC: WASD, LMB attack, RMB / Left Shift dodge, Left Ctrl walk/sprint toggle, F interact, E special, Q ultimate, Space/C switch, MMB lock-on. Gamepad: A dodge/sprint (Xbox), X attack, Y special, RT ultimate, LB/RB switch, RS camera/lock-on, LT/RT camera height in the city.
- **Point-light shadows:** cube = 6 passes, optimised with per-face frustum culling; dual paraboloid = 2 passes but needs tessellation, lower quality; tetrahedral = 4; reduced update rate / static caching; atlas allocation; limit shadow-casting lights (e.g. 1–2).

## Workspace Snapshot (Round 7)
- Project root: `/home/user/Sakuga/`
- Build/cache excluded from snapshot (~78 MB).
- Screenshots stored in `docs/screenshots/` (e.g., `ps_on.png`, `ps_off.png`, `ps_cmp1.png`, `ps_final_cmp.png`, `blink_test.png`).

## Next Actions
- Implement face/hair shading and physics improvements (ZZZ-style soft shadows, nose/under-eye mark, face-normal flatten, complementary rim "aura", desaturated palette/gradient, spring tuning).
- Add `tests/` directory, CTest setup, `tools/run_quality.sh`, validation suite.
- Capture final screenshots and update README.

---
*Generated by the Sakuga Engine Agent on 2026-11-01 (Africa/Accra).*