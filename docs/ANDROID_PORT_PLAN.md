# Android port: status and remaining work

The experimental `Sakuga-Android-animation-preview-debug.apk` builds the shared desktop sandbox and MotionLab against the Android Vulkan surface and window layer. It packages the engine shaders, `AvatarSample.vrm`, Fox and CesiumMan glTF props, Seed-san and three-vrm-girl NPCs, configuration, LUTs and two BVH clips. It is not a production release: the optional 77-clip library is not packaged by default (a separately obtained BVH directory can be included using `SAKUGA_ANDROID_MOTION_DIR`), and mobile defaults disable NPCs and spring bones, cap imported textures at 1024 pixels and use smaller shadow atlases. The mobile panel can load the MotionLab NPC crowd on demand. `AvatarSample.vrm` is by pixiv Inc. and `Seed-san.vrm` is by VirtualCast, Inc.; both allow redistribution under the VRM public license, and Seed-san requires credit. `three-vrm-girl.vrm` is by pixiv Inc. and its embedded license URL permits redistribution. Review all asset licences before distribution.

Android owns a `SurfaceView` and an acquired `ANativeWindow`; native Vulkan surface, swapchain and renderer resources are recreated with the surface. Surface destruction waits for the render worker. A Vulkan capability probe is only a baseline, and the renderer checks additional limits. If setup or rendering fails, the Activity falls back to the separate synthetic Canvas preview. The native touch controls route pointer IDs independently to movement, look and action buttons, while the upper panel and sandbox editor receive ImGui events; cancellation and pause clear input.

CI requires a Vulkan frame on an x86_64 SwiftShader emulator, opens the ImGui editor, toggles a setting, moves the sandbox avatar, selects a clip, loads four NPCs, and verifies a new Vulkan frame after background/resume. GPU-free unit tests are separate. The historical screenshot/video in the README show the earlier Canvas-only build, not the current sandbox. Emulator success is not physical-device validation.

## Remaining work

1. Evaluate remaining desktop feature parity and improve mobile controls and configuration.
2. Harden surface loss, device errors, formats, memory and lifecycle handling across Android devices; profile and tune mobile settings. Surface recreation currently rebuilds the session rather than preserving all in-memory editor state.
3. Complete asset, storage and licensing review. The optional 77-clip library has its own licence and is not packaged by default.
4. Test gameplay, orientation changes, background/resume, failure recovery and sustained performance on representative physical phones. Owner-controlled signing is supported, but it does not make the app production-ready.
