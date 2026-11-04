cmake_minimum_required(VERSION 3.22)
project(SakugaAndroidPreview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Link the actual engine (except the desktop GLFW window) and its vendored dependencies into the Android library.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ${ROOT}/engine/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/platform/Window\\.cpp$")
# FBX inspection is a desktop CLI, not part of the Android renderer.
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/asset/FbxInfo\\.cpp$")
set(RN ${ROOT}/third_party/recastnavigation)
file(GLOB RN_SOURCES CONFIGURE_DEPENDS ${RN}/Recast/Source/*.cpp ${RN}/Detour/Source/*.cpp ${RN}/DetourCrowd/Source/*.cpp)
add_library(sakuga_mobile SHARED
  jni.cpp MobileSession.cpp AndroidVulkanSurface.cpp AndroidContext.cpp AndroidRenderSession.cpp WindowAndroid.cpp
  ${ROOT}/apps/sandbox/main.cpp ${ROOT}/apps/sandbox/MotionLab.cpp
  ${ENGINE_SOURCES} ${RN_SOURCES}
  ${ROOT}/third_party/imgui/imgui.cpp ${ROOT}/third_party/imgui/imgui_draw.cpp
  ${ROOT}/third_party/imgui/imgui_tables.cpp ${ROOT}/third_party/imgui/imgui_widgets.cpp ${ROOT}/third_party/imgui/imgui_demo.cpp
  ${ROOT}/third_party/imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(sakuga_mobile PRIVATE ${ROOT}/engine ${ROOT}/third_party ${ROOT}/third_party/glm
  ${ROOT}/third_party/imgui ${ROOT}/third_party/glfw/include ${ROOT}/android/native
  ${RN}/Recast/Include ${RN}/Detour/Include ${RN}/DetourCrowd/Include)
target_compile_definitions(sakuga_mobile PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL
  VK_USE_PLATFORM_ANDROID_KHR GLFW_INCLUDE_NONE)
target_link_libraries(sakuga_mobile PRIVATE log android vulkan)
