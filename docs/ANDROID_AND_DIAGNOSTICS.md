# Android, touch controls and diagnostics

## Current status

**The full desktop sandbox is not yet ported to Android.** The experimental debug APK links the C++ engine, shaders and a bundled VRM. On a compatible Vulkan 1.3 device it attempts to render a small VRM scene using the engine renderer, `Entity` and motion matcher with two packaged BVH clips. If setup fails, it displays the separate Canvas skeleton preview instead. The on-screen stick drives motion and **NEXT CLIP** browses the two clips; the GPU-free touch router tracks independent pointers and clears them on cancellation or pause. This is not the full desktop sandbox, the full 77-clip library or a production release. The app supports Android 8 (API 26) or newer on arm64-v8a and x86_64, but the Vulkan scene has additional device requirements and is not guaranteed to run on every Android 8+ device.

The `Android animation preview` workflow builds, validates and uploads `sakuga-android-animation-preview-debug`, installs it on an x86_64 emulator, checks native clip loading and touch, and reports whether a Vulkan frame was presented or the Canvas fallback was used. It captures a screenshot and short video. Download the `.apk` from a successful workflow run's artifacts; GitHub may require sign-in. The APK is signed with an ephemeral debug key: it is **not** a Play Store build and builds from different runs may not upgrade one another. Uninstall the previous preview before installing a new one if signing keys differ. No real Android device has been validated yet.

To build locally, install Android SDK platform 35, build-tools 35.0.0, NDK 27.2.12479018, JDK, CMake, Ninja and zip; set `ANDROID_HOME`, then run `bash android/build-apk.sh`. Android SDK/NDK and Java are platform build tools, not bundled runtime libraries. Output is in `android/output/`.

The [port plan](ANDROID_PORT_PLAN.md) tracks what is implemented and what remains. The desktop ImGui controls, on-screen action controls and full sandbox scene are not yet wired to the Android Activity. Linux/macOS packages remain future work.

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

### Owner-controlled signing (preview only)

`android/build-apk.sh` defaults to an ephemeral debug key. To produce a consistently owner-signed **preview**, use an existing owner-controlled Android keystore and set `SAKUGA_ANDROID_SIGNING_MODE=owner`, `SAKUGA_ANDROID_KEYSTORE` (path), `SAKUGA_ANDROID_KEY_ALIAS`, `SAKUGA_ANDROID_STORE_PASSWORD` and `SAKUGA_ANDROID_KEY_PASSWORD` in the build environment before running the script. Password values are passed to `apksigner` via environment variable names, not command-line values. Do not commit the keystore or passwords, enable shell tracing, or upload them to a build log. Protect and back up the key: future updates must use the same signing identity (or follow the platform's supported key-rotation procedures). The output is named `Sakuga-Android-animation-preview-owner-signed.apk`; it is still only the experimental preview, not a production app. For Play distribution, additional Play App Signing, publishing, policy, privacy and device-validation work remains. CI tests the signing path with a disposable key that is never used for releases.
