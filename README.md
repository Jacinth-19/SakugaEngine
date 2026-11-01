<div align="center">
  <img src="assets/branding/sakuga_banner.png" alt="Sakuga Engine concept art: a character in motion" width="100%">

  <sub>Illustrative concept art — not an in-engine screenshot.</sub>

  # Sakuga Engine

  **An animation and rendering sandbox for expressive, anime-inspired characters.**

  Vulkan · C++20 · Motion matching · VRM · ImGui

  [Download the Windows preview](https://github.com/Jacinth-19/SakugaEngine/releases/tag/v0.1.0-preview) · [Getting started](#get-started) · [Developer guide](docs/ENGINE_GUIDE.md) · [Platform status](#platform-status)
</div>

---

## Get started

### Windows x64

1. Open the **[Windows preview release](https://github.com/Jacinth-19/SakugaEngine/releases/tag/v0.1.0-preview)** and download either the installer (`.exe`) or the portable `.zip`.
2. Run the installer, or extract the **whole** portable archive to a folder you can write to. Launch **Sakuga Engine** or `sandbox.exe`.
3. You need a **Vulkan 1.3-capable GPU and a current graphics driver**. The installer does not include the Vulkan runtime or driver.

> **Preview:** the Windows executable is built and smoke-tested in GitHub Actions, but actual rendering and the installer UI still need validation on a real Windows GPU. The build is not an Android release.

### Find your way around

| Control | Action |
| :--- | :--- |
| **Tab** | Release or capture the mouse |
| **F1 / F2 / F3** | Hide the UI / take a screenshot / toggle the FPS counter |
| **F11 / F12** | Open the ImGui demo / open the theme editor |
| **Esc** | Quit |

Open **Motion matching** to see the current animation and loaded clip count. Open **Device and performance** for CPU, RAM, storage space, GPU and frame statistics. Some values may be unavailable; HDD versus SSD is not currently detected. Press **F12** to explore the live theme editor. Saving a theme beside an installed app may require a writable location; the portable build is convenient for experiments.

## See the sandbox

These are captures from the sandbox, not the illustrative banner above.

| Day | Night |
| :---: | :---: |
| ![Motion lab in daylight](docs/screenshots/lab_day.png) | ![Motion lab at night](docs/screenshots/lab_night.png) |
| **Character** | **Editor** |
| ![VRM character in the sandbox](docs/screenshots/vrm_standing.png) | ![Sandbox editor interface](docs/screenshots/editor_ui.png) |

## Explore

- **Animation** — BVH motion matching, inertialized transitions, IK, spring bones and a motion lab with NPCs.
- **Rendering** — anime-inspired shading, outlines, face lighting, shadows, HDR lighting and post-processing.
- **Authoring** — live ImGui themes, a material inspector and runtime debug views.
- **Diagnostics** — loaded animation count and device/frame information to help investigate performance.

### About the 77 animations

The Windows preview includes **two sample clips**. The full LAFAN1 library is a separate download and is **CC BY-NC-ND 4.0, non-commercial use only**. From a source checkout, run:

```bash
./tools/fetch_assets.sh --motion-only
```

On Windows, use a Bash environment such as Git Bash. The script belongs to the **source checkout**, not the installed app. It downloads the clips to `~/.cache/lafan1` by default; you can also point the app at a directory with `--motion-dir DIR`. The app shows the number actually loaded. See [animation testing and diagnostics](docs/ANDROID_AND_DIAGNOSTICS.md) for how the 77-clip test and skeletal debug video work.

## Platform status

| Platform | Status |
| :--- | :--- |
| **Windows x64** | Preview installer and portable archive; CI build and GPU-free smoke tests pass. Real-device visual testing remains. |
| **Android** | **Not available yet.** The touch PNGs are concepts, not integrated mobile controls; there is no APK. A window/input/Vulkan and packaging port is still needed. |
| **Linux** | Source build exists; no release package is provided yet. |
| **macOS** | No release package or verified build is provided yet. |

See the [Android and diagnostics notes](docs/ANDROID_AND_DIAGNOSTICS.md) for the porting gaps and the limits of the device stats.

## Build & test

Most source dependencies and precompiled shaders are in the repository; building the application still needs the platform toolchain and Vulkan loader/SDK. For the Windows recipe, other prerequisites, shader tools and detailed configuration, see the [developer guide](docs/ENGINE_GUIDE.md).

The test suite runs without a GPU:

```bash
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

The full-motion tests skip if the separately downloaded clip library is absent. Passing GPU-free tests does **not** replace visual testing on a real device.

## More information

- [Developer guide](docs/ENGINE_GUIDE.md) — source builds, themes, animation and test details
- [Installer guide](docs/INSTALLER.md) — Inno Setup styling and iteration
- [Android & diagnostics](docs/ANDROID_AND_DIAGNOSTICS.md) — mobile status, device stats and debug-video reproduction
- [Shader port notes](docs/SHADER_PORT.md) · [Script audit](docs/SCRIPT_AUDIT.md) · [Third-party dependencies](third_party/README.md)

## Licences

LAFAN1 mocap is **CC BY-NC-ND 4.0**; review its terms before use or redistribution. Other included assets and dependencies have their own licences; see the [developer guide](docs/ENGINE_GUIDE.md#licences) and [third-party notes](third_party/README.md).
