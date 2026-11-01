# Android, touch controls and diagnostics

## Current status

**Android is not yet a supported build target.** This change adds touch-control PNG concepts in `assets/ui/touch/` (stick base and knob, jump, attack, interact, crouch and sprint). They are not yet wired to the renderer. The existing `TouchControls` overlay is drawn using ImGui primitives and can be tried with a mouse on desktop; it accepts injected touch IDs, but the current GLFW window code does not receive Android multi-touch events. Images alone do not make an APK.

An Android port must replace or adapt desktop window creation and ImGui input, connect Android pointer IDs including cancellation to `injectTouch`, support lifecycle events and surface recreation, access packaged assets, integrate Vulkan surface creation and device requirements, and set up an Android NDK/Gradle package and CI. It needs testing on real devices, including multiple screen sizes and simultaneous touches. The current renderer requests Vulkan 1.3, so device compatibility needs investigation. Linux/macOS are not added by this change.

The AI-generated files in `assets/branding/` are **concept artwork**, not screenshots or the actual in-app character. The 512px PNG is an icon draft; it is not yet wired into the installer, Windows executable or an Android adaptive icon. The wide banner should be labelled concept art when shown to users.

## Device and performance panel

Open **Device and performance** in the main ImGui panel. It reports CPU name and logical threads when available, total and available RAM, free and total space on the app's volume, Vulkan device name and API version, device-local memory heap size, framebuffer size, FPS, averaged frame time, renderer CPU time, draw calls and triangles. A device-local heap may be shared memory and is not necessarily dedicated VRAM. Values come from the local OS or Vulkan device; unavailable values are shown as such. Storage medium is explicitly **unknown**, not guessed as HDD or SSD. No GPU time or utilization is measured. These numbers are diagnostic, not a performance rating.

The **Motion matching** panel shows current clip index and name against loaded total, frame, matcher transitions and matching/browser mode. The number is the count loaded into this session, not necessarily 77.

## Reproduce the GPU-free animation test and debug visualization

The LAFAN1 data must be fetched separately, respecting its license (`tools/fetch_assets.sh --motion-only`). The full-library test skips if insufficient clips are installed. With the full set available:

```sh
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --parallel
build-tests/motion_library_test --full
build-tests/mm_playback_dump --out motion_debug.csv
python3 tools/gen_motion_debug_video.py --csv motion_debug.csv --out motion_debug.mp4
```

The video encoder needs `ffmpeg` (or the optional `imageio-ffmpeg` Python package); alternatively specify `--format gif` with ImageMagick. It visualizes joint positions recorded from the production matcher on a **synthetic rig**, not rendered game footage. It first runs a scripted desired-motion route, then explicitly browses a brief sample of every loaded clip. The HUD's used count and clip-change count include this forced sweep, not just clips automatically selected during matching. It is not proof that every frame, retargeting on every character, or rendering on a phone is visually correct. The full-library test likewise checks a 12-frame browser sample per clip, finite outputs and category reachability, not complete visual playback.

LAFAN1 is by Ubisoft La Forge; review its license before distributing the motion data or derivatives. Generated CSV/video are intentionally not committed to the repository.
