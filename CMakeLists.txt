cmake_minimum_required(VERSION 3.20)
project(Sakuga VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAKUGA_BUILD_TESTS "Build the GPU-free test suite (tests/)" ON)

# ================================================================ dependencies
# Every third-party library the engine links against is vendored in third_party/ (glm, GLFW, Vulkan headers,
# Dear ImGui, Recast/Detour, cgltf, stb), so a fresh clone builds without downloading anything. A system
# installation is preferred when CMake can find one; otherwise the vendored copy is used automatically.
# Still required from the machine: a Vulkan *loader* + GPU driver at runtime, and - only if you want to
# recompile the shaders - glslangValidator (the precompiled SPIR-V in shaders/spv/ is used otherwise).

# ---------------------------------------------------------------- Vulkan
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  message(STATUS "Sakuga: system Vulkan ${Vulkan_VERSION} (${Vulkan_INCLUDE_DIR})")
else()
  set(SAKUGA_VULKAN_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include)
  if(NOT EXISTS ${SAKUGA_VULKAN_INCLUDE}/vulkan/vulkan.h)
    message(FATAL_ERROR "Vulkan headers not found. Install the Vulkan SDK/headers, or vendor them in third_party/vulkan/include.")
  endif()
  # Headers are vendored; the loader is a system component. Look for it under its usual names (the versioned
  # .so.1 ships with the driver even when the -dev symlink does not).
  find_library(SAKUGA_VULKAN_LOADER
    NAMES vulkan vulkan-1 libvulkan.so.1 libvulkan.so
    HINTS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib)
  add_library(sakuga_vulkan INTERFACE)
  target_include_directories(sakuga_vulkan INTERFACE ${SAKUGA_VULKAN_INCLUDE})
  if(SAKUGA_VULKAN_LOADER)
    target_link_libraries(sakuga_vulkan INTERFACE ${SAKUGA_VULKAN_LOADER})
    message(STATUS "Sakuga: vendored Vulkan headers, loader ${SAKUGA_VULKAN_LOADER}")
  else()
    message(FATAL_ERROR "Vulkan loader not found (headers are vendored in third_party/vulkan). "
                        "Install your GPU driver's Vulkan runtime / the Vulkan SDK, e.g. 'vulkan-icd-loader' or vendor 'vulkan-1.dll'.")
  endif()
  add_library(Vulkan::Vulkan ALIAS sakuga_vulkan)
endif()

# ---------------------------------------------------------------- GLM (header only)
find_package(glm QUIET)
if(NOT TARGET glm::glm)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm/glm/glm.hpp)
    add_library(sakuga_glm INTERFACE)
    target_include_directories(sakuga_glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm)
    add_library(glm::glm ALIAS sakuga_glm)
    message(STATUS "Sakuga: vendored glm (third_party/glm)")
  else()
    message(FATAL_ERROR "glm not found (install glm or vendor it in third_party/glm).")
  endif()
endif()

# ---------------------------------------------------------------- GLFW
find_package(glfw3 QUIET)
if(NOT TARGET glfw)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
    message(STATUS "Sakuga: vendored GLFW (third_party/glfw, built from source)")
  else()
    message(FATAL_ERROR "GLFW not found (install glfw3 or vendor it in third_party/glfw).")
  endif()
endif()

find_package(Threads REQUIRED)

# ================================================================ shaders
# glslangValidator is only needed to (re)compile shaders. Without it the SPIR-V committed in shaders/spv/ is used,
# so a clone can build and run with no Vulkan SDK present.
file(GLOB SHADER_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.vert ${CMAKE_SOURCE_DIR}/shaders/*.frag)
file(GLOB SHADER_INC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.glsl)
find_program(GLSLANG glslangValidator
  HINTS $ENV{VULKAN_SDK}/bin ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang/bin)

if(GLSLANG)
  set(SPV_OUT "")
  foreach(S ${SHADER_SRC})
    get_filename_component(N ${S} NAME)
    set(O ${CMAKE_BINARY_DIR}/shaders/${N}.spv)
    add_custom_command(OUTPUT ${O}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
      COMMAND ${GLSLANG} -V --target-env vulkan1.3 -I${CMAKE_SOURCE_DIR}/shaders ${S} -o ${O}
      DEPENDS ${S} ${SHADER_INC}
      COMMENT "glslang ${N}" VERBATIM)
    list(APPEND SPV_OUT ${O})
  endforeach()
  add_custom_target(SakugaShaders ALL DEPENDS ${SPV_OUT})
  message(STATUS "Sakuga: glslangValidator ${GLSLANG} (shaders compiled from source)")
else()
  message(WARNING "glslangValidator not found: using the precompiled SPIR-V in shaders/spv/. "
                  "Install the Vulkan SDK (or run tools/build_shaders.sh) after editing any shader.")
  file(GLOB PREBUILT_SPV CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/spv/*.spv)
  if(NOT PREBUILT_SPV)
    message(FATAL_ERROR "No glslangValidator and no precompiled shaders in shaders/spv/.")
  endif()
  # Warn when a shader source is newer than its committed SPIR-V (i.e. the prebuilt file is stale).
  foreach(S ${SHADER_SRC})
    get_filename_component(N ${S} NAME)
    set(O ${CMAKE_SOURCE_DIR}/shaders/spv/${N}.spv)
    if(EXISTS ${O} AND ${S} IS_NEWER_THAN ${O})
      message(WARNING "shaders/spv/${N}.spv is older than shaders/${N}: run tools/build_shaders.sh")
    endif()
  endforeach()
  add_custom_target(SakugaShaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders/spv ${CMAKE_BINARY_DIR}/shaders
    COMMENT "Sakuga: copying precompiled SPIR-V")
endif()

# ================================================================ imgui
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
# The engine includes <GLFW/glfw3.h> next to Vulkan; without this GLFW pulls in the OpenGL headers, which a Vulkan-only
# (headless / CI) toolchain may not ship.
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

# ---------------------------------------------------------------- recast / detour
set(RN_DIR ${CMAKE_SOURCE_DIR}/third_party/recastnavigation)
file(GLOB RN_SRC CONFIGURE_DEPENDS ${RN_DIR}/Recast/Source/*.cpp ${RN_DIR}/Detour/Source/*.cpp ${RN_DIR}/DetourCrowd/Source/*.cpp)
add_library(recastdetour STATIC ${RN_SRC})
target_include_directories(recastdetour PUBLIC ${RN_DIR}/Recast/Include ${RN_DIR}/Detour/Include ${RN_DIR}/DetourCrowd/Include)
target_compile_options(recastdetour PRIVATE -w)

# ---------------------------------------------------------------- engine
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/*.cpp)
add_library(sakuga STATIC ${ENGINE_SRC})
target_include_directories(sakuga PUBLIC ${CMAKE_SOURCE_DIR}/engine ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(sakuga PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(sakuga PUBLIC Vulkan::Vulkan glfw glm::glm imgui recastdetour Threads::Threads)
if(MSVC)
  target_compile_options(sakuga PRIVATE /W3)
else()
  target_compile_options(sakuga PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
add_dependencies(sakuga SakugaShaders)

# ---------------------------------------------------------------- apps
file(GLOB SANDBOX_SRC CONFIGURE_DEPENDS apps/sandbox/*.cpp)
add_executable(sandbox ${SANDBOX_SRC})
target_link_libraries(sandbox PRIVATE sakuga)
add_custom_target(SakugaCopyShaders ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/shaders $<TARGET_FILE_DIR:sandbox>/shaders
  DEPENDS SakugaShaders VERBATIM)
add_dependencies(SakugaCopyShaders sandbox)

# ---------------------------------------------------------------- tests (GPU-free, CTest)
# Always configureable on its own as well: cmake -S tests -B build-tests
if(SAKUGA_BUILD_TESTS)
  enable_testing()          # so `ctest --test-dir build` works from the top-level build tree
  add_subdirectory(tests)
endif()
