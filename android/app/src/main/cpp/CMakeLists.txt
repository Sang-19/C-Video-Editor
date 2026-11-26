cmake_minimum_required(VERSION 3.22.1)
project(trisvax_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(TRISVAX_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(TRISVAX_NATIVE_DIR "${TRISVAX_REPO_ROOT}/native")

if(NOT EXISTS "${TRISVAX_NATIVE_DIR}/engine/video_editor_engine.cpp")
  message(FATAL_ERROR "Cannot locate TRISVAX native C++20 engine at ${TRISVAX_NATIVE_DIR}")
endif()

add_library(trisvax_core_android STATIC
  ${TRISVAX_NATIVE_DIR}/engine/types.cpp
  ${TRISVAX_NATIVE_DIR}/engine/video_editor_engine.cpp
  ${TRISVAX_NATIVE_DIR}/timeline/timeline_engine.cpp
  ${TRISVAX_NATIVE_DIR}/filters/filter_pipeline.cpp
  ${TRISVAX_NATIVE_DIR}/rendering/compositor_math.cpp
  ${TRISVAX_NATIVE_DIR}/audio/audio_mixer.cpp
  ${TRISVAX_NATIVE_DIR}/storage/json_codec.cpp
  ${TRISVAX_NATIVE_DIR}/storage/project_store.cpp
  ${TRISVAX_NATIVE_DIR}/export/export_engine.cpp
)

target_include_directories(trisvax_core_android PUBLIC ${TRISVAX_REPO_ROOT})

add_library(trisvax_jni SHARED
  jni/jni_bridge.cpp
  decoder/ndk_media_decoder.cpp
  encoder/ndk_media_encoder.cpp
  renderer/gles_renderer.cpp
  audio/aaudio_player.cpp
)

target_include_directories(trisvax_jni PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TRISVAX_REPO_ROOT}
)

# Optional FFmpeg integration when third_party/ffmpeg/<ABI> is populated
set(TRISVAX_FFMPEG_DIR "${TRISVAX_REPO_ROOT}/third_party/ffmpeg/${ANDROID_ABI}")
if(EXISTS "${TRISVAX_FFMPEG_DIR}/include/libavcodec/avcodec.h")
  message(STATUS "TRISVAX: Enabling FFmpeg prebuilt acceleration for ${ANDROID_ABI}")
  target_compile_definitions(trisvax_jni PRIVATE TRISVAX_HAS_FFMPEG=1)
  target_include_directories(trisvax_jni PRIVATE "${TRISVAX_FFMPEG_DIR}/include")
endif()

find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(egl-lib EGL)
find_library(gles3-lib GLESv3)
find_library(aaudio-lib aaudio)

target_link_libraries(trisvax_jni
  trisvax_core_android
  ${log-lib}
  ${android-lib}
  ${mediandk-lib}
  ${egl-lib}
  ${gles3-lib}
  ${aaudio-lib}
)
