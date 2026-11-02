# Full Android port: status and remaining work

The experimental `Sakuga-Android-animation-preview-debug.apk` links the engine and vendored dependencies (apart from the GLFW desktop window), the packaged shaders, the bundled `AvatarSample.vrm` and two BVH clips. On devices meeting its Vulkan 1.3 requirements it attempts a small scene using the engine renderer, VRM loader, Entity and motion matcher. If initialization fails, the existing synthetic-rig Canvas preview remains available. This is **not the desktop sandbox** and is **not production-ready**. `AvatarSample.vrm` metadata identifies pixiv Inc. as the author and allows redistribution under the VRM public license; review the metadata and all asset licences before distribution.

Android owns a `SurfaceView` and an acquired `ANativeWindow`; native Vulkan surface, swapchain and renderer resources are recreated with the surface. Surface destruction waits for the render worker before returning. A temporary Vulkan probe provides a baseline, but it does not guarantee all renderer requirements or real-device compatibility. The activity's controls route pointer IDs independently; cancellation and pause clear them. The Vulkan scene uses a reduced texture size and disables point shadows, bloom and spring bones; it has not been profiled on phones.

The emulator integration check distinguishes an engine frame from Canvas fallback, and separately checks clip loading, a NEXT CLIP tap and background/resume. GPU-free unit tests remain separate. Emulator success alone is not hardware validation. The historical screenshot/video in the README were captured from the old Canvas-only version, not the current VRM scene.

## Remaining work

1. Integrate the full desktop sandbox's scenes, camera, action controls and Android ImGui input rather than only the small VRM scene and Java overlay.
2. Harden surface loss, device errors, formats, memory, performance and lifecycle handling across Android devices; make the renderer's mobile settings configurable and profile them.
3. Complete asset, storage and licensing review. The optional 77-clip library has its own licence and is not packaged.
4. Test interactive gameplay, rotation, background/resume, failure recovery and sustained performance on representative physical phones. Owner-controlled signing is supported for the preview, but it does not make the app production-ready.
