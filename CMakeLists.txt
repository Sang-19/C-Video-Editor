cmake_minimum_required(VERSION 3.22)
project(trisvax_video_editor VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(trisvax_core STATIC
  native/engine/types.cpp
  native/engine/video_editor_engine.cpp
  native/timeline/timeline_engine.cpp
  native/filters/filter_pipeline.cpp
  native/rendering/compositor_math.cpp
  native/audio/audio_mixer.cpp
  native/storage/json_codec.cpp
  native/storage/project_store.cpp
  native/export/export_engine.cpp
)

target_include_directories(trisvax_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(trisvax_core PRIVATE /W4)
else()
  target_compile_options(trisvax_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(TRISVAX_BUILD_TESTS "Build standalone C++20 unit tests" ON)
option(TRISVAX_BUILD_CLI "Build TRISVAX native CLI engine bridge" ON)

if(TRISVAX_BUILD_CLI)
  add_executable(trisvax_cli native/cli/trisvax_cli.cpp)
  target_link_libraries(trisvax_cli PRIVATE trisvax_core)
endif()

if(TRISVAX_BUILD_TESTS)
  enable_testing()
  add_executable(trisvax_tests tests/test_main.cpp)
  target_link_libraries(trisvax_tests PRIVATE trisvax_core)
  add_test(NAME trisvax_unit_tests COMMAND trisvax_tests)
endif()
