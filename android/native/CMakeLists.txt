cmake_minimum_required(VERSION 3.22)
project(SakugaAndroidPreview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(sakuga_mobile SHARED
  jni.cpp MobileSession.cpp AndroidVulkanSurface.cpp
  ${ROOT}/engine/anim/Bvh.cpp ${ROOT}/engine/anim/Rig.cpp ${ROOT}/engine/anim/MotionMatching.cpp
  ${ROOT}/engine/core/FileUtil.cpp)
target_include_directories(sakuga_mobile PRIVATE ${ROOT}/engine ${ROOT}/third_party/glm)
target_compile_definitions(sakuga_mobile PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_compile_definitions(sakuga_mobile PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
target_link_libraries(sakuga_mobile PRIVATE log android vulkan)

# Porting aid only: compile the existing desktop engine translation units with the NDK. This archive is deliberately
# NOT linked into the preview APK; it still references GLFW and has no Android surface or ImGui platform backend.
# A successful build means C++ compilation only, not that Vulkan rendering or the desktop sandbox works on Android.
option(SAKUGA_ANDROID_COMPILE_DESKTOP_SOURCES "Compile desktop engine sources with the Android NDK (not a port)" OFF)
if(SAKUGA_ANDROID_COMPILE_DESKTOP_SOURCES)
  file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ${ROOT}/engine/*.cpp)
  list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/platform/Window\\.cpp$")
  add_library(sakuga_android_desktop_compilecheck STATIC ${ENGINE_SOURCES})
  target_include_directories(sakuga_android_desktop_compilecheck PRIVATE
    ${ROOT}/engine ${ROOT}/third_party ${ROOT}/third_party/glm ${ROOT}/third_party/glfw/include
    ${ROOT}/third_party/imgui ${ROOT}/third_party/imgui/backends
    ${ROOT}/third_party/recastnavigation/Recast/Include
    ${ROOT}/third_party/recastnavigation/Detour/Include
    ${ROOT}/third_party/recastnavigation/DetourCrowd/Include)
  target_compile_definitions(sakuga_android_desktop_compilecheck PRIVATE
    GLFW_INCLUDE_NONE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
endif()
