# Script audit: what we had missed

Two script sets were checked: the **scripts of the reference Unity project** (`Jacinth-19/MotionMatchingInUnity`, where
this engine's BVH + motion-matching code came from) and **this repository's own scripts** (`tools/`, `tests/`). Below is
what each contains, what was missing, and what changed in this pass.

## 1. Reference Unity project

Full tree: 365 blobs — 77 `.bvh` clips, 154 imported `.asset` clips (+154 `.meta`), one FBX rig, one scene, the Unity
project files (Unity 2022.3.4f1; `Packages/manifest.json` only lists stock packages, no third-party motion-matching
plugin), and **7 C# scripts**. Nothing else lives in `Assets/Scripts`.

| Script | Ported as | Missing? |
| --- | --- | --- |
| `BVHImporter/Editor/BVHParser.cs` | `engine/anim/Bvh.cpp` (`loadBvh`) | no |
| `BVHImporter/BVHAnimationImporter.cs` | `MotionDatabase::buildFromFiles` (file *and* directory import) | no |
| `BVHImporter/Editor/BVHAnimationImporterEditor.cs` | same (the "Import animation" / "…in directory" buttons) | no |
| `BVHAnimator/BVHAnimationClip.cs` | `MotionDatabase` (clips, poses) + `BvhClip` (frames, root motion, `refHips`) | no |
| `BVHAnimator/BVHAnimator.cs` | `MotionMatcher::update` / `samplePose` (interpolated playback, root motion) | no |
| `BVHAnimator/Editor/BVHAnimationClipEditor.cs` | `MotionDatabase` feature build (`GeneratePoseFeature` runs automatically) | no |
| `MotionMatching/TrajectoryPredictor.cs` | — | **dead code in the Unity project** (empty `MonoBehaviour`, referenced by nothing) |

Field-by-field, the `BVHAnimationClip` asset has `model`, `totalFrame`, `frameTime`, `boneList`, `boneRotations`,
`rootBoneOffset`, `rootMotionCurve` and `poseFeatureData`; every one of those has a counterpart (`Rig` retarget instead of a
per-clip `model`, `BvhClip::frames`/`fps()`, per-frame `localRot`, `refHips`, `rootPos`/`rootYaw`, `features`). So **no
functionality was missed** — the port is a superset. What *was* missed was provenance:

* `reference/unity/` carried only 5 of the 7 scripts, and `reference/unity/README.md` claimed `BVHAnimationClip` "was not
  part of the uploaded set". It is in the repository; it is now vendored as `reference/unity/BVHImporter/BVHAnimationClip.cs`
  (together with the types it defines: `RotationCurve`, `PoseFeature`).
* `reference/unity/MotionMatching/TrajectoryPredictor.cs` is now vendored too, so the "this is dead code" finding can be
  checked against the actual file instead of a description.

## 2. This repository's scripts

| Script | Role | State |
| --- | --- | --- |
| `tools/fetch_assets.sh` | mocap + models + LUTs + HDRIs | fixed below |
| `tools/gen_anime_luts.py` | writes the four `assets/luts/Anime *.cube` | **could not run** (needed numpy) - fixed |
| `tools/gen_material_glsl.py` | writes `shaders/material_block.glsl` from `MaterialProps.inc` | fine, output matches |
| `tools/build_shaders.sh` | recompiles `shaders/*.vert|frag` to `shaders/spv/*.spv` | fine; needs `glslangValidator` (see gaps) |
| `tests/run_quality.sh` | configure + build + `ctest` | fine |

### Fixed in this pass

1. **`gen_anime_luts.py` required numpy**, which nothing documented and which is not installed on a plain build machine,
   so the generator (and the asset fetch, which calls it under `set -e`) failed outright. It is now pure stdlib Python.
   The four committed LUTs regenerate **byte-identically**, which also makes the generated assets verifiable (below).
2. **The README claimed the suite "runs in CI" but there was no CI.** `.github/workflows/tests.yml` now configures,
   builds and runs the GPU-free suite on every push/PR, and re-runs both generators to fail if the committed
   `shaders/material_block.glsl` or `assets/luts/Anime *.cube` are stale.
3. **`motion_library_test` failed on a fresh clone.** It treated "no `.bvh` files" as the only skip condition, but
   `assets/motion/` always carries the two preview clips - so on a clean checkout it built a 2-clip database and the
   "several families are reachable" check failed (reproduced with an empty `$HOME`: 1/418 checks failed). It now skips
   below 12 clips (40 for `--full`), which is what the README promised.
4. **`fetch_assets.sh` only used raw.githubusercontent.com**, which is blocked on some networks (including the one this was
   developed on), and only the mocap section had a `gh api` fallback. All GitHub-hosted files (glTF samples, the three VRM
   avatars, the three.js LUTs) now go through a shared `fetch_gh` helper with the same fallback, and a failed download is
   reported and makes the script exit non-zero instead of silently leaving a hole.
5. **`assets/env/qwantani_dusk_2_puresky.hdr` was committed but never fetched** - a re-fetch on a clean clone would have
   left the second HDRI missing. Both skies are in the script now.
6. New `--assets-only` mode (models/LUTs/HDRIs, no 333 MB mocap), and `--motion-only` no longer runs the asset section.

### Verified

* `bash -n` on all three shell scripts; `--assets-only` and `--motion-only` both complete against the vendored tree.
* The `gh api` fallback was exercised for real: `fetch_gh mrdoob/three.js/dev examples/luts/NeutralLUT.png /tmp/…` fetched a
  valid PNG through `gh` after the raw host refused, and a bogus repo increments the missing counter and warns.
* All four regenerated LUTs are byte-identical to the committed files; `gen_material_glsl.py` produces no diff.
* `motion_library_test` SKIPs with 2 clips and passes with 77 (`OK (11304 checks)`); `ctest` 13/13.

### Gaps that remain (not fixable here)

* `build_shaders.sh` needs `glslangValidator`; none exists in this sandbox, so the committed SPIR-V cannot be re-verified
  here - only the source/shader count (15 `.vert`/`.frag`, 15 `.spv`) can be checked.
* The Poly Haven HDRI URLs are plain `curl` (not GitHub) and were not exercised - the files are vendored.
