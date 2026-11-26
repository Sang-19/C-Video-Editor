# TRISVAX Studio — C++20 Native Video Editor

A full technology migration of the **TRISVAX Video Editor** Android application from Kotlin/Media3 to a modular **C++20 native video-editing architecture**, preserving every existing TRISVAX screen, workflow, and dark visual identity (`#0B0807`, `#18110D`, `#221711`, `#FF5500`, `#FF8232`) while resolving editing, audio-mixing, undo/redo, color-grading, and persistence gaps.

## Repository Layout

```text
TRISVAX-VIDEO-EDITOR/
├── CMakeLists.txt                  # Standalone C++20 desktop/CLI/test build
├── native/                         # Pure C++20 portable video-editing engine
│   ├── engine/                     # Types, Undo/Redo command stack, VideoEditorEngine facade
│   ├── timeline/                   # Non-destructive microsecond-precision timeline math
│   ├── filters/                    # Brightness, contrast, saturation, temp, exposure, sharpness + GLSL ES 3.0
│   ├── rendering/                  # Aspect ratio, cover-crop, pan/zoom, rotation compositor math
│   ├── audio/                      # Multi-track float PCM mixer (clips + music trim/loop/fade + voiceovers)
│   ├── storage/                    # Zero-dependency JSON codec & disk project manifest store
│   ├── media/                      # Abstract Decoder/Encoder/Preview/Audio interfaces
│   ├── export/                     # Export normalization (1080p, 30 FPS, no watermark) & validation
│   └── cli/                        # trisvax_cli headless C++20 engine executable
├── tests/                          # C++20 unit test suite (53 assertions)
├── android/                        # Android NDK + CMake + JNI + thin Jetpack Compose UI
│   └── app/src/main/
│       ├── cpp/                    # JNI bridge, NDK AMediaCodec/AMediaExtractor/AMediaMuxer, GLES3, AAudio
│       ├── java/com/trisvax/studio/ # Thin Android platform bridge & Compose screens
│       └── assets/music/           # Bundled studio music tracks
├── server.ts                       # Express backend bridging C++20 trisvax_cli, FFmpeg MP4 export & Android ZIP
├── src/                            # Web studio companion preserving TRISVAX UI on port 3000
└── docs/                           # Architecture, feature parity audit, and Windows NDK/ADB build guide
```

## Quick Start

### 1. Run C++20 Core Engine Tests (Linux/macOS/Windows)
```bash
g++ -std=c++20 -O2 -I. \
  native/engine/types.cpp \
  native/timeline/timeline_engine.cpp \
  native/filters/filter_pipeline.cpp \
  native/rendering/compositor_math.cpp \
  native/audio/audio_mixer.cpp \
  native/storage/json_codec.cpp \
  native/storage/project_store.cpp \
  native/export/export_engine.cpp \
  native/engine/video_editor_engine.cpp \
  tests/test_main.cpp -o .build/trisvax_tests
./.build/trisvax_tests
```

### 2. Run the Web Studio Companion (Port 3000)
```bash
npm install
npm run dev
```

### 3. Build & Install the Android NDK APK
See [docs/WINDOWS_BUILD_AND_ADB.md](docs/WINDOWS_BUILD_AND_ADB.md) for full Windows PowerShell, Android SDK/NDK, CMake, Gradle, and ADB physical-device instructions.
