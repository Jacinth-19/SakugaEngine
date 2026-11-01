cmake_minimum_required(VERSION 3.22)
project(SakugaAndroidPreview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(sakuga_mobile SHARED
  jni.cpp MobileSession.cpp
  ${ROOT}/engine/anim/Bvh.cpp ${ROOT}/engine/anim/Rig.cpp ${ROOT}/engine/anim/MotionMatching.cpp
  ${ROOT}/engine/core/FileUtil.cpp)
target_include_directories(sakuga_mobile PRIVATE ${ROOT}/engine ${ROOT}/third_party/glm)
target_compile_definitions(sakuga_mobile PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(sakuga_mobile PRIVATE log)
