# Sakuga Engine — Windows and Android preview

Early Windows x64 and Android builds of the animation and rendering sandbox. This is a **pre-release**, not a finished or stable product.

## Updated Windows preview (5 November 2026)

- **Installer:** `Sakuga-Engine-0.1.0-win64-23ced19-setup.exe`
- **Portable:** `Sakuga-Engine-0.1.0-win64-23ced19-portable.zip` — extract the entire archive and run `sandbox.exe`.
- **Checksums:** `SHA256SUMS-23ced19.txt`

The installer includes a preview page and sidebar featuring two poses from earlier in-engine VRM screenshots. Their original capture environment is unknown; appearance varies by device and settings. Both updated downloads include all **77 LAFAN1 clips**. Expect substantial storage and memory use and a longer first launch while the motion cache is built.

The updated binaries were built from branch commit [`23ced193d1cb794cf6cd89e6da59a564cce79234`](https://github.com/Jacinth-19/SakugaEngine/commit/23ced193d1cb794cf6cd89e6da59a564cce79234) in [Windows workflow run 42056888085](https://github.com/Jacinth-19/SakugaEngine/actions/runs/42056888085). The build, GPU-free executable checks, and silent installer smoke test passed. The installer interface, rendering and gameplay have **not** been visually validated on a real Windows GPU.

## Android preview

- **Debug APK:** `Sakuga-Android-0.1.0-23ced19-debug.apk`
- **Checksum:** `SHA256SUMS-android-23ced19.txt`

This experimental build contains the shared Vulkan sandbox, VRM models and all 77 motion clips. It supports Android 8 (API 26) or newer on arm64-v8a and x86_64. The full 3D sandbox additionally requires Vulkan 1.3, dynamic rendering and synchronization2. If Vulkan initialization or rendering fails, the app uses a separate Canvas synthetic-rig preview, **not** the full 3D sandbox. Plenty of storage and memory are needed; the first start builds motion caches.

The APK was built from branch commit [`23ced193d1cb794cf6cd89e6da59a564cce79234`](https://github.com/Jacinth-19/SakugaEngine/commit/23ced193d1cb794cf6cd89e6da59a564cce79234) in [Android workflow run 42056888330](https://github.com/Jacinth-19/SakugaEngine/actions/runs/42056888330). Its build and Vulkan/Canvas interaction and render smoke tests passed on an x86_64 emulator. **No physical Android phone has been tested**, and device compatibility and performance are not guaranteed.

The APK is signed with an ephemeral debug key, not a production or Play Store identity. Builds signed with different keys cannot upgrade one another; uninstalling the previous app may be required and will remove its app data. Install APKs only if you trust the source.

## Requirements, license and other platforms

Windows x64 requires a Vulkan 1.3-capable GPU and current graphics driver. The Vulkan loader/driver is not bundled. The Windows installer and executable are not code-signed; Windows may display an unknown-publisher warning.

**License:** LAFAN1 is by Ubisoft La Forge and is CC BY-NC-ND 4.0: non-commercial only, with no permission to share adapted material. Review the dataset and motion-derived media terms before redistribution. See the packaged `assets/licenses/LAFAN1-NOTICE.txt`. Linux and macOS release packages are not provided.

## Original November 1 release files

The existing `Sakuga-Engine-0.1.0-win64.exe`, `Sakuga-Engine-0.1.0-win64-portable.zip`, and `SHA256SUMS.txt` remain unchanged. They contain only two preview clips and were built from commit `fc40181bc6ae66d5c0e852ccf20740dfe2023203` in [Windows workflow run 41299861039](https://github.com/Jacinth-19/SakugaEngine/actions/runs/41299861039). The `v0.1.0-preview` tag remains at the original commit; the updated files above come from the explicitly linked branch commit, not that tag.
