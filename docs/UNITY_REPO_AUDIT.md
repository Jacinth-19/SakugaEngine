# Audit of the reference Unity project

`Jacinth-19/MotionMatchingInUnity` is the Unity prototype this engine's BVH import and motion matching were ported from.
This note records what it contains, what is dead code in it, and where its mocap lives, so the two projects can be compared
without cloning 340 MB.

## Scripts (7 files)

| File | Status |
| --- | --- |
| `Assets/Scripts/BVHAnimator.cs` | **live** - referenced from `Assets/Scenes/SampleScene.unity` (GUID `7c39f982...`); plays the imported clip on the skeleton |
| `Assets/Scripts/BVHAnimationImporter.cs` | **live** - referenced from `SampleScene.unity` (GUID `57b1b34a...`); the `ScriptedImporter` for `.bvh` |
| `Assets/Scripts/BVHAnimationImporterEditor.cs` | **live** - editor for the importer above |
| `Assets/Scripts/BVHAnimationClip.cs` | **live** - the `ScriptableObject` the importer produces |
| `Assets/Scripts/BVHAnimationClipEditor.cs` | **live** - inspector for that asset |
| `Assets/Scripts/BVHParser.cs` | **live** - the BVH parser itself (mirrored in `reference/unity/BVHImporter/` and `engine/anim/Bvh.cpp`) |
| `Assets/Scripts/MotionMatching/TrajectoryPredictor.cs` | **dead** - see below |

### Unused: `Assets/Scripts/MotionMatching/TrajectoryPredictor.cs`

303 bytes, an empty `MonoBehaviour`:

```csharp
public class TrajectoryPredictor : MonoBehaviour { void Start() { } void Update() { } }
```

Its `.cs.meta` GUID `964f98f2e9657bb479589896f4aa7386` does not appear anywhere in `Assets/Scenes/SampleScene.unity`, in any
prefab, or in any other script, and nothing derives from or references the type. Deleting the file (and its `.meta`) removes
the last trace of a "trajectory prediction" feature that the Unity project never implemented - this engine's
`anim::MotionMatcher` does have a real trajectory/future-pose term, so the intent is covered here instead.

Nothing else in the repo is stale: `Assets/BVHAnimation/*.asset` (154 files, ~12 MB each) are generated import output, not
scripts, and `Assets/Model/model_skeleton.fbx` is the single rig everything is retargeted onto.

## Mocap

* `Assets/lafan1/` - the full LAFAN1 set: **77 `.bvh` files**, 6.4 KB … 5.7 MB each (333 MB total).
* The two clips vendored in this repository (`assets/motion/walk1_subject1.bvh`, `run1_subject2.bvh`) are members of that
  set, which is why the same file names appear on both sides.

The dataset is CC BY-NC-ND, so it is not copied into this repository; `tools/fetch_assets.sh --motion-only` downloads the
same 77 files from that project (or from the upstream LAFAN1 mirror) into `~/.cache/lafan1`, validating each one before it
is kept. The clip names in the script were diffed against `Assets/lafan1/` and match exactly.

## What the port changed

* The Unity project imports each BVH into a `.asset` clip and plays one clip at a time; this engine builds one
  `MotionDatabase` over all 77 clips and every character searches it (`README.md`, "Motion matching: the whole library,
  every character").
* The parser was brought in line with `BVHParser.cs` for keyword case, spaced joint names and scientific notation
  (`tests/bvh_parse_test.cpp` covers the same edge cases).
