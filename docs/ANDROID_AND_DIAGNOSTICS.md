# Android, touch controls and diagnostics

## Current status

The experimental Android APK runs the **shared C++ sandbox and MotionLab** with the engine's Vulkan renderer, bundled `AvatarSample.vrm`, two skinned glTF props, two BVH clips and the ImGui editor. On-screen native controls provide a floating movement stick, camera drag, and jump, attack, use, crouch and sprint; the compact upper panel opens the editor, advances clips and adjusts rendering settings. Pointers are tracked independently and cancelled on pause. Touch beginning in the upper editor regions is reserved for ImGui. The Activity is landscape-only.

Mobile starts with NPCs, spring bones and point shadows disabled; **NPCs** in the mobile panel loads the two packaged NPC models and adds the MotionLab crowd. It limits imported textures to 1024 pixels and uses smaller shadow atlases than desktop. The other desktop scenes and editor are available, but this is **not feature or asset parity** with the desktop configuration. The optional full 77-clip library is not packaged. If Vulkan initialization or rendering fails, the app displays a **separate Canvas synthetic-rig preview**, not the 3D sandbox. It supports Android 8 (API 26) or newer on arm64-v8a and x86_64, with Vulkan 1.3 and additional renderer capability requirements for the 3D scene; not every Android 8+ device qualifies.

The `Android sandbox` workflow builds and validates the APK, and requires a Vulkan sandbox frame on an x86_64 SwiftShader emulator. It exercises the ImGui checkbox and editor, avatar movement, clip selection, and background/resume, and uploads a screenshot and short video. These interaction/render checks are separate from the GPU-free unit tests. Download `sakuga-android-animation-preview-debug` from a successful workflow run's artifacts; GitHub may require sign-in. The APK uses an ephemeral debug key: it is **not** a Play Store build and builds from different runs may not upgrade one another. Uninstall the previous build if signing keys differ. No physical Android phone has been validated, and the emulator is not evidence of phone compatibility or performance.

To build locally, install Android SDK platform 35, build-tools 35.0.0, NDK 27.2.12479018, JDK, CMake, Ninja and zip; set `ANDROID_HOME`, then run `bash android/build-apk.sh`. Android SDK/NDK and Java are platform build tools, not bundled runtime libraries. Output is in `android/output/`.

The [port plan](ANDROID_PORT_PLAN.md) tracks implemented work and remaining limitations. Linux/macOS packages remain future work.

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
