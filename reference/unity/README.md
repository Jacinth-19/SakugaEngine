# Unity reference: BVH import + pose features

Verbatim copies of the BVH side of the original Unity project (the same source `engine/anim/Bvh.cpp` and
`engine/anim/MotionMatching.cpp` were ported from). Kept for provenance, the same way `reference/*.shader` keeps the
Unity URP shader. **Nothing here is compiled by this project.**

| File | Role | Port |
|------|------|------|
| `BVHImporter/BVHParser.cs` | char-by-char BVH text parser: hierarchy → bones, channels, frames | `engine/anim/Bvh.cpp` (`loadBvh`), `engine/anim/Bvh.h` (`BvhClip`) |
| `BVHImporter/BVHAnimationImporter.cs` / `BVHAnimationImporterEditor.cs` | editor UI; BVH text → `BVHAnimationClip` asset (root motion curve + per-bone quaternion keys) | `MotionDatabase::buildFromFiles` (retarget + features), `apps/sandbox`
| `BVHImporter/BVHAnimator.cs` | runtime playback: 30 fps stepping, lerp between the last two samples | `MotionMatcher::update` / `samplePose` |
| `BVHImporter/BVHAnimationClipEditor.cs` | "GeneratePoseFeature": future root-motion / facing deltas at +5…+30 frames | `MotionDatabase` feature vector (`kFeatureDim`, `trajOffsets`) |

`BVHAnimationClip` and `PoseFeature` (the ScriptableObject types these reference) were not part of the uploaded set.

## Behaviour carried over

* **Tokenizer tolerance.** `charMap` upper-cases a–z and turns tab/CR/LF into a space, so keywords and channel names are
  case-insensitive; `getFloat` accepts a comma decimal separator and scientific notation. The C++ parser now matches this
  (it previously required exact `HIERARCHY` / `Xrotation` casing and rejected lowercase files outright).
* **Joint names are the rest of the line**, so `JOINT Left Shoulder` is one bone. The C++ parser now reads tokens up to
  `{` and joins them, instead of taking a single token and silently naming the bone `Left`.
* **End Sites are not bones** (parsed and discarded). 27 LAFAN1 bones − 5 End Sites = the 22 joints in the database.
* **Corrupt input throws** (`assure` with a ±15-character context). The C++ returns `false` plus an error string with the
  file name; a short motion section is now an error instead of being zero-filled to the declared frame count.
* **cm → m** (`/100`) and a reference hips offset that is subtracted from the root motion. The port scales by
  `0.01` and subtracts the reference clip's hips height (`refHips`), then adds the rig's own rest hips height.

## Behaviour deliberately *not* carried over

* **`rotation.y = -rotation.y; rotation.z = -rotation.z;`** (a conjugation by the X axis: `R_y`/`R_z` angles flip sign).
  That is a handedness fix for Unity's left-handed space. The port instead derives each character's facing frame from
  geometry (`yawOf`: left hip→right hip and shoulder axes, forward = `cross(left, up)`) and rebuilds the pose in a
  Y-up right-handed frame, so no quaternion components are mirrored.
* **`fromEulerZYX`** composes `Rz·Ry·Rx` regardless of the file's channel order. For LAFAN1 (`Zrotation Yrotation
  Xrotation`) the two agree; the port composes in the order the file lists the channels, which is what the BVH format
  specifies, so other channel orders stay correct.
* **`wrapAngle` on each Euler angle** — unnecessary with quaternions, which wrap by construction.
* **Root motion = `(-x/100, y/100, z/100)`** — the X negation goes with the handedness flip above.
* **`frameTime` is parsed but never used** (`BVHAnimator` hard-codes 1/30 s). The port uses the clip's own frame time
  (`BvhClip::fps()`), and converts exporter-written milliseconds (`Frame Time: 33.3333`) to seconds.
* **Pose features** are 6 root-motion deltas + 6 facing deltas in the reference. The port uses the Holden-style set
  (foot positions + velocities, hip velocity, 3 trajectory positions + 3 headings at 10/20/30 frames), documented in
  `engine/anim/MotionMatching.h`.

## Defects in this reference code (not reproduced in the port)

* `BVHAnimationImporterEditor`: `boneRotations.Add(...)` only happens for bones with all three rotation channels, but is
  indexed by `boneIndex`. Any bone without rotation channels shifts every later bone's curves, and `BVHAnimator` then
  reads past the end of the list. The port indexes poses by joint.
* `BVHAnimator`: `m_CurrentFrame++` is never clamped, so playback throws `IndexOutOfRangeException` one frame after the
  clip ends; `Quaternion.Lerp` is fed an unclamped `lerpPercent` (no while-loop on the timer, so a frame spike
  extrapolates); bone lookup uses `name.Contains(...)`, which can bind to the wrong transform; `Debug.Log` runs every frame.
* `BVHAnimationClipEditor`: the `frameIndex + 30 >= totalFrame` guard invalidates all six predictions when only the last
  one is out of range, and stores `Vector3.positiveInfinity` for root motion (infinite features poison any distance
  metric) while storing `0` for the angle. The port checks each lookahead separately and marks the frame invalid instead.
* `BVHParser`: `static charMap` is lazily initialised, which contradicts the "safe to use in another thread" comment.
