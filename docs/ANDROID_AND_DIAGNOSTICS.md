# Android, touch controls and diagnostics

## Current status

**The full desktop sandbox is not yet ported to Android.** GitHub Actions now builds a separate, experimental **animation-only debug APK** using a small Android Activity and the native C++ motion matcher. It packages the two repository BVH clips, retargets them to a synthetic debug rig and draws a skeleton on Android Canvas. The on-screen stick (using the bundled joystick PNGs) drives the matcher; **NEXT CLIP** browses the two clips. A GPU-free native touch router tracks pointer IDs independently so the stick and button can be used together, and clears ownership on cancellation or pause. It is not the Vulkan renderer, a VRM character viewer, or the full 77-clip library. It needs Android 8 (API 26) or newer and supports arm64-v8a and x86_64.

The `Android animation preview` workflow builds, validates and uploads `sakuga-android-animation-preview-debug`, then installs the x86_64 build in an Android emulator and checks that both native clips load, exercises the NEXT CLIP touch and captures a screenshot and short video. Download the `.apk` from a successful workflow run's artifacts; GitHub may require sign-in. The APK is signed with an ephemeral debug key: it is **not** a Play Store build and builds from different runs may not upgrade one another. Uninstall the previous preview before installing a new one if signing keys differ. No real Android device has been validated yet.

To build locally, install Android SDK platform 35, build-tools 35.0.0, NDK 27.2.12479018, JDK, CMake, Ninja and zip; set `ANDROID_HOME`, then run `bash android/build-apk.sh`. Android SDK/NDK and Java are platform build tools, not bundled runtime libraries. Output is in `android/output/`.

The preview also probes a temporary Android Vulkan surface and reports basic presentation requirements; it does not render with Vulkan or establish full renderer compatibility. See the [port plan](ANDROID_PORT_PLAN.md) for details. A full Android port still needs renderer-integrated native Vulkan surface/window lifecycle, Android input and ImGui backends, packaged desktop assets, device compatibility and real-device testing. The [port plan](ANDROID_PORT_PLAN.md) tracks these gaps and describes the NDK desktop-source compile check (which is not a runnable port). The desktop `TouchControls` overlay and other touch PNGs are not yet integrated into this preview. Linux/macOS packages remain future work.

The AI-generated files in `assets/branding/` are **concept artwork**, not screenshots or the actual in-app character. The 512px PNG is used as the Android preview launcher icon, but it is not yet wired into the Windows executable or an Android adaptive icon. The wide banner should be labelled concept art when shown to users.

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
