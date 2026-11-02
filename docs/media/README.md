# Media provenance

- `android_preview.png` and `android_preview.mp4` were captured from the x86_64 Android emulator during [GitHub Actions run 41331384579](https://github.com/Jacinth-19/SakugaEngine/actions/runs/41331384579), commit `b793073e3d30b53d7ad8805972e1a7a587f5e91c`, on 1 November 2026. The workflow installed the debug APK, verified the native engine loaded two clips, tapped Next Clip, moved the on-screen stick and captured the screen. This is the **animation-only synthetic-rig preview**, not a capture of the full Vulkan sandbox, a real phone, or proof of full gameplay.
- The desktop screenshots under `docs/screenshots/` and videos `docs/demo_motion_lab.mp4` and `docs/demo_motion_matching.mp4` were already in the repository. Their original capture environment/date were not recorded, and they must not be used as evidence that the current Windows release passed gameplay or hardware testing.
- The banner under `assets/branding/` is AI-generated concept artwork, not an in-engine screenshot.

Motion shown in the preview uses the bundled LAFAN1 samples by Ubisoft La Forge. LAFAN1 is CC BY-NC-ND 4.0; review its terms before redistributing data or footage.

- `android_sandbox_editor.png` was captured on the x86_64 Android emulator using the real Vulkan sandbox (SwiftShader), on 2 November 2026, in [GitHub Actions run 41520145068](https://github.com/Jacinth-19/SakugaEngine/actions/runs/41520145068), commit `b537b6c`. It shows the mobile ImGui editor; it is not a screenshot from a physical phone. The same run submitted four lamp shadow maps (24 faces) at night, but the screenshot is not a quantitative shadow-quality test.
