# Android port: status and remaining work

The experimental `Sakuga-Android-animation-preview-debug.apk` builds the shared desktop sandbox and MotionLab against the Android Vulkan surface and window layer. It packages the engine shaders, `AvatarSample.vrm`, Fox and CesiumMan glTF props, configuration, LUTs and two BVH clips. It is not a production release: NPC models and the optional 77-clip library are not packaged, and mobile defaults disable NPCs, spring bones and point shadows, cap imported textures at 1024 pixels and use smaller shadow atlases. `AvatarSample.vrm` metadata identifies pixiv Inc. as the author and allows redistribution under the VRM public license; review all asset licences before distribution.

Android owns a `SurfaceView` and an acquired `ANativeWindow`; native Vulkan surface, swapchain and renderer resources are recreated with the surface. Surface destruction waits for the render worker. A Vulkan capability probe is only a baseline, and the renderer checks additional limits. If setup or rendering fails, the Activity falls back to the separate synthetic Canvas preview. The native touch controls route pointer IDs independently to movement, look and action buttons, while the upper panel and sandbox editor receive ImGui events; cancellation and pause clear input.

CI requires a Vulkan frame on an x86_64 SwiftShader emulator, opens the ImGui editor, toggles a setting, moves the sandbox avatar, selects a clip, and verifies a new Vulkan frame after background/resume. GPU-free unit tests are separate. The historical screenshot/video in the README show the earlier Canvas-only build, not the current sandbox. Emulator success is not physical-device validation.

## Remaining work

1. Add mobile configurations for the omitted desktop NPCs and any assets needed for them, and evaluate full desktop feature parity.
2. Harden surface loss, device errors, formats, memory and lifecycle handling across Android devices; profile and tune mobile settings. Surface recreation currently rebuilds the session rather than preserving all in-memory editor state.
3. Complete asset, storage and licensing review. The optional 77-clip library has its own licence and is not packaged.
4. Test gameplay, orientation changes, background/resume, failure recovery and sustained performance on representative physical phones. Owner-controlled signing is supported, but it does not make the app production-ready.
