cmake_minimum_required(VERSION 3.15)
project(SakugaTests CXX)

# Include engine source
include_directories(${CMAKE_SOURCE_DIR}/engine ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_SOURCE_DIR}/apps/sandbox)

# Add test executables
add_executable(loader_test tests/loader_test.cpp)
target_link_libraries(loader_test sakuga)

add_executable(mm_search_test tests/mm_search_test.cpp)
target_link_libraries(mm_search_test sakuga)

add_executable(lut_parsing_test tests/lut_parsing_test.cpp)
target_link_libraries(lut_parsing_test sakuga)

add_executable(blink_curve_test tests/blink_curve_test.cpp)
target_link_libraries(blink_curve_test sakuga)

add_executable(posture_clamp_test tests/posture_clamp_test.cpp)
target_link_libraries(posture_clamp_test sakuga)

add_executable(shadow_fit_test tests/shadow_fit_test.cpp)
target_link_libraries(shadow_fit_test sakuga)

add_executable(headless_smoke_test tests/headless_smoke_test.cpp)
target_link_libraries(headless_smoke_test sakuga)

add_executable(pixel_diff_test tests/pixel_diff_test.cpp)
target_link_libraries(pixel_diff_test sakuga)

add_executable(debug_lines_test tests/debug_lines_test.cpp)
target_link_libraries(debug_lines_test sakuga)

add_executable(face_hair_test tests/face_hair_test.cpp)
target_link_libraries(face_hair_test sakuga)

# Enable CTest
enable_testing()
add_test(NAME loader_test COMMAND loader_test)
add_test(NAME mm_search_test COMMAND mm_search_test)
add_test(NAME lut_parsing_test COMMAND lut_parsing_test)
add_test(NAME blink_curve_test COMMAND blink_curve_test)
add_test(NAME posture_clamp_test COMMAND posture_clamp_test)
add_test(NAME shadow_fit_test COMMAND shadow_fit_test)
add_test(NAME headless_smoke_test COMMAND headless_smoke_test)
add_test(NAME pixel_diff_test COMMAND pixel_diff_test)
add_test(NAME debug_lines_test COMMAND debug_lines_test)
add_test(NAME face_hair_test COMMAND face_hair_test)
