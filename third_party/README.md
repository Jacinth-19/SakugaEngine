# Vendored dependencies

Everything the engine links against lives here, so a fresh clone builds without downloading a single library.
The build prefers a system installation when CMake finds one and falls back to these copies automatically
(see the "dependencies" section of the root `CMakeLists.txt`).

| Library | Version | Licence | Path | Notes |
|---------|---------|---------|------|-------|
| [glm](https://github.com/g-truc/glm) | 1.0.1 | MIT (`glm/copying.txt`) | `glm/` | header-only; used via the `glm::glm` target |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | zlib (`glfw/LICENSE.md`) | `glfw/` | built from source as a subdirectory; window + input |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | 1.3.290 | Apache-2.0 / MIT (`vulkan/LICENSE.md`) | `vulkan/include/` | C headers only (`vulkan/*.h`, `vk_video/*.h`); the C++ bindings are not used |
| [Dear ImGui](https://github.com/ocornut/imgui) | docking-era snapshot | MIT (`imgui/LICENSE.txt`) | `imgui/` | editor UI |
| [Recast & Detour](https://github.com/recastnavigation/recastnavigation) | 1.6-era snapshot | zlib (`recastnavigation/License.txt`) | `recastnavigation/` | navmesh build + crowd |
| [cgltf](https://github.com/jkuhlmann/cgltf) | 1.x (header) | MIT | `cgltf/cgltf.h` | glTF/VRM parsing |
| [stb](https://github.com/nothings/stb) | `stb_image` / `stb_image_write` | public domain | `stb/` | image decode/encode |

## What is *not* vendored, and why

* **Vulkan loader and GPU driver.** Platform- and driver-specific (`libvulkan.so.1`, `vulkan-1.dll`, MoltenVK on macOS).
  CMake looks for it under the usual names; install your driver's Vulkan runtime or the Vulkan SDK.
* **X11 / Wayland development headers.** Needed by GLFW on Linux (e.g. `libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev`, or the wayland equivalents). These are OS packages, not libraries this repo can carry.
  On a headless server, configure with `-DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=OFF` (GLFW's null platform) to build
  the engine and the tests without a window backend.
* **glslangValidator.** Only needed to *re*compile shaders. The compiled SPIR-V is committed in `shaders/spv/`, which the
  build uses when the tool is absent - so editing a shader requires the Vulkan SDK, running the engine does not.

## Keeping a vendored copy fresh

1. Drop the new release's files over the directory (the copies here are trimmed: no docs, tests, examples or C++
   bindings).
2. Delete files the upstream release removed - these trees are plain files, not submodules.
3. Update the version in the table above, and for `glm` / `GLFW` / `Vulkan-Headers` run:

   ```sh
   cmake -S . -B build && cmake --build build -j     # configure prints which copy is in use
   cmake -S tests -B build-tests && ctest --test-dir build-tests
   ./tools/build_shaders.sh                          # only if shaders/ changed (needs glslangValidator)
   ```
