# Full Android port: status and remaining work

The existing `Sakuga-Android-animation-preview-debug.apk` is a **separate skeleton animation preview**, not the desktop sandbox. Adding Android libraries to its build does not automatically port GLFW window management, Vulkan surface creation, ImGui input, asset handling or Android's activity lifecycle.

As a concrete porting check, the Android build can compile the existing desktop engine C++ source files (except desktop `Window.cpp`) for arm64 with the NDK. It produces a **static compile-check archive only**: it is not linked into the preview APK, and it still depends on desktop GLFW calls. Successful compilation is not a working renderer, linkage test or gameplay test.

## Surface foundation (in development)

The animation preview now owns an Android `SurfaceView` and probes its `ANativeWindow` from a worker thread. For each live surface it creates and destroys a temporary Vulkan instance and Android surface, then reports a baseline check for a Vulkan 1.3 graphics/present queue, swapchain extension, surface formats and image usage, dynamic rendering and synchronization2. Surface-loss callbacks wait for any in-flight probe and invalidate stale UI results; native window, surface and instance references are released after the probe. The Canvas preview remains the only drawing path. A positive probe does **not** create a device or swapchain, check all renderer requirements, render a frame, or demonstrate that the full app works; an unsupported device remains usable for the preview. The emulator checks that the probe finishes, not GPU rendering. No physical device has been tested.

## Work needed before calling it the full app

1. **Window and lifecycle:** replace or abstract GLFW with an Android native window backend. Handle activity pause/resume, screen rotation, loss/recreation of the surface and correct shutdown of all Vulkan resources.
2. **Vulkan and portability:** integrate an Android Vulkan surface with the actual renderer (the standalone probe is not that integration), select a queue that can present to it, check the actual device's formats, limits and memory in addition to the probed baseline; choose supported render settings and report clear errors. Vulkan 1.3 is not universally available on phones.
3. **UI and touch:** use an Android-capable ImGui input backend and connect pointer IDs, release and cancellation to the engine's on-screen stick, look and action controls. Integrate the PNGs if they are intended as the visual controls. Handle orientation, insets, density and simultaneous touches.
4. **Assets and storage:** package and access models, compiled shaders, config and sample clips, and write caches/saves to Android-owned writable storage. The optional 77-clip library is subject to its own licence and memory/storage costs.
5. **Application and distribution:** integrate the actual sandbox scene, renderer and animation code in the APK, provide clear loading and errors, profile and tune on devices, then sign releases using a persistent owner-controlled Android key.
6. **Verification:** keep unit tests GPU-free. Separately perform and record actual interactive gameplay, lifecycle and performance checks on representative real Android phones; emulator success alone is not hardware validation. Only publish a mobile release when these checks pass.

The small NDK compile check and emulator preview are useful intermediate steps; neither should be advertised as the finished mobile game.
