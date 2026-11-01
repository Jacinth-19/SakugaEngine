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

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_program(GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

# ---------------------------------------------------------------- shaders
file(GLOB SHADER_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.vert ${CMAKE_SOURCE_DIR}/shaders/*.frag)
file(GLOB SHADER_INC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.glsl)
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

# ---------------------------------------------------------------- imgui
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
# Kept out of the default build when the engine is configured without a compiler that supports C++20, and always
# configureable on its own: cmake -S tests -B build-tests
if(SAKUGA_BUILD_TESTS)
  enable_testing()          # so `ctest --test-dir build` works from the top-level build tree
  add_subdirectory(tests)
endif()
