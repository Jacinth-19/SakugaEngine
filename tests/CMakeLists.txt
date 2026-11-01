# GPU-free test suite.
#
# The engine's tests only need the parts that do not touch Vulkan (animation, asset parsing, world, shadow/face math), so the
# suite builds and runs in a container with no GPU, no driver and no window server:
#
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
#
# It is also added as a subdirectory by the top-level CMakeLists.txt (option SAKUGA_BUILD_TESTS).
# For a machine without an installed glm, pass -DSAKUGA_GLM_INCLUDE_DIR=/path/to/glm.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(SakugaTests LANGUAGES CXX)
endif()

get_filename_component(SAKUGA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- glm
# Vendored copy first (nothing to download), then a system glm, then -DSAKUGA_GLM_INCLUDE_DIR for odd setups.
add_library(sakuga_test_glm INTERFACE)
if(EXISTS ${SAKUGA_ROOT}/third_party/glm/glm/glm.hpp)
  target_include_directories(sakuga_test_glm INTERFACE ${SAKUGA_ROOT}/third_party/glm)
elseif(TARGET glm::glm)
  target_link_libraries(sakuga_test_glm INTERFACE glm::glm)
elseif(SAKUGA_GLM_INCLUDE_DIR)
  target_include_directories(sakuga_test_glm INTERFACE ${SAKUGA_GLM_INCLUDE_DIR})
else()
  find_package(glm REQUIRED)
  target_link_libraries(sakuga_test_glm INTERFACE glm::glm)
endif()

# ---------------------------------------------------------------- headless engine subset
# Only sources with no Vulkan / GLFW / ImGui dependency may appear here (checked by the compiler).
add_library(sakuga_headless STATIC
  ${SAKUGA_ROOT}/engine/platform/SysInfo.cpp
  ${SAKUGA_ROOT}/engine/core/FileUtil.cpp
  ${SAKUGA_ROOT}/engine/core/Json.cpp
  ${SAKUGA_ROOT}/engine/anim/Bvh.cpp
  ${SAKUGA_ROOT}/engine/anim/Rig.cpp
  ${SAKUGA_ROOT}/engine/anim/MotionMatching.cpp
  ${SAKUGA_ROOT}/engine/asset/Lut.cpp
  ${SAKUGA_ROOT}/engine/world/World.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stb_impl.cpp)
target_include_directories(sakuga_headless PUBLIC
  ${SAKUGA_ROOT}/engine
  ${SAKUGA_ROOT}/third_party
  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sakuga_headless PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(sakuga_headless PUBLIC sakuga_test_glm)
find_package(Threads REQUIRED)
target_link_libraries(sakuga_headless PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(sakuga_headless PUBLIC advapi32)
endif()
if(NOT MSVC)
  target_compile_options(sakuga_headless PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- dear imgui (core only)
# The UI theme is data plus an apply() into ImGuiStyle, so its test needs ImGui's core library but not the GLFW/Vulkan
# backends - which keeps ui_theme_test GPU-free like the rest of the suite.
set(SAKUGA_IMGUI_DIR ${SAKUGA_ROOT}/third_party/imgui)
if(EXISTS ${SAKUGA_IMGUI_DIR}/imgui.cpp)
  add_library(sakuga_imgui_core STATIC
    ${SAKUGA_IMGUI_DIR}/imgui.cpp ${SAKUGA_IMGUI_DIR}/imgui_draw.cpp
    ${SAKUGA_IMGUI_DIR}/imgui_tables.cpp ${SAKUGA_IMGUI_DIR}/imgui_widgets.cpp
    ${SAKUGA_ROOT}/engine/gfx/UiTheme.cpp)
  target_include_directories(sakuga_imgui_core PUBLIC ${SAKUGA_IMGUI_DIR} ${SAKUGA_ROOT}/engine)
  # UiTheme.cpp reads its JSON through engine/core/Json.cpp, which lives in the headless subset.
  target_link_libraries(sakuga_imgui_core PUBLIC sakuga_test_glm sakuga_headless)
  if(NOT MSVC)
    target_compile_options(sakuga_imgui_core PRIVATE -w)     # third-party code: keep the suite's warnings for ours
  endif()
endif()

# ---------------------------------------------------------------- tests
set(SAKUGA_TESTS
  sys_info_test
  presentation_requirements_test
  mobile_touch_test
  loader_test
  bvh_parse_test
  mm_search_test
  motion_library_test
  lut_parsing_test
  blink_curve_test
  posture_clamp_test
  shadow_fit_test
  headless_smoke_test
  pixel_diff_test
  debug_lines_test
  face_hair_test)

enable_testing()
foreach(test_name ${SAKUGA_TESTS})
  add_executable(${test_name} ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE sakuga_headless)
  target_compile_definitions(${test_name} PRIVATE
    SAKUGA_ROOT="${SAKUGA_ROOT}"
    SAKUGA_ASSET_DIR="${SAKUGA_ROOT}/assets")
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The Android preview uses the production motion engine on a simple rig; exercise it on the host without a GPU.
add_executable(mobile_session_test ${CMAKE_CURRENT_SOURCE_DIR}/mobile_session_test.cpp ${SAKUGA_ROOT}/android/native/MobileSession.cpp)
target_include_directories(mobile_session_test PRIVATE ${SAKUGA_ROOT}/android/native)
target_link_libraries(mobile_session_test PRIVATE sakuga_headless)
target_compile_definitions(mobile_session_test PRIVATE SAKUGA_ROOT="${SAKUGA_ROOT}")
add_test(NAME mobile_session_test COMMAND mobile_session_test)

# The UI theme test needs the vendored ImGui core (see above); it is skipped when imgui/ is not there.
if(TARGET sakuga_imgui_core)
  add_executable(ui_theme_test ${CMAKE_CURRENT_SOURCE_DIR}/ui_theme_test.cpp)
  target_link_libraries(ui_theme_test PRIVATE sakuga_headless sakuga_imgui_core)
  target_compile_definitions(ui_theme_test PRIVATE SAKUGA_ROOT="${SAKUGA_ROOT}")
  add_test(NAME ui_theme_test COMMAND ui_theme_test)
endif()

# GPU-free tooling that reuses the same engine subset as the tests: it records what the matcher does frame by frame
# (tools/gen_motion_debug_video.py renders that into the demo video). Not a test - `cmake --build . --target mm_playback_dump`.
add_executable(mm_playback_dump ${SAKUGA_ROOT}/tools/mm_playback_dump.cpp)
target_link_libraries(mm_playback_dump PRIVATE sakuga_headless)
target_compile_definitions(mm_playback_dump PRIVATE SAKUGA_ROOT="${SAKUGA_ROOT}")

# The full-library run is the same binary with --full: it builds all 77 clips (~3 s) and asserts that every LAFAN1 clip
# family is searchable. Both skip cleanly when the clips have not been fetched (tools/fetch_assets.sh --motion-only).
add_test(NAME motion_library_full_test COMMAND motion_library_test --full)

# A single aggregate target: `cmake --build . --target quality` then `ctest`.
add_custom_target(quality COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Sakuga: running the GPU-free test suite"
  USES_TERMINAL)
