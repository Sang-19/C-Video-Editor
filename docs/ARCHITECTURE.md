# Architecture Overview

## Design Principles
1. **C++20 Owns All Video-Editing State & Processing**: Kotlin/Compose contains zero timeline math, zero audio mixing logic, and zero filter/export configuration logic. All editing mutations flow through `VideoEditorBridge` -> `jni_bridge.cpp` -> `trisvax::VideoEditorEngine`.
2. **Microsecond Precision**: All clip in/out points, timeline positions, transitions, text windows, music trims, and fades are stored and computed in `int64_t` microseconds (`TimeUs`).
3. **Non-Destructive Timeline + Command Pattern Undo/Redo**: Every edit records a reversible command in `CommandHistory` (`max 64` snapshots) supporting instant `undo()` and `redo()` across trim, split, crop, rotate, speed, volume, filters, transitions, music, voiceover, text overlays, and clip reordering/deletion.
4. **Zero Unnecessary Media Duplication**: Picked videos are referenced by `content://` URI with persistable read permissions + small JPEG thumbnails, avoiding copying gigabyte-scale source videos into private app storage.

## Module Breakdown

### Portable C++20 Engine (`/native/`)
- `engine/`: `VideoEditorEngine`, `CommandHistory`, `types.hpp`
- `timeline/`: `TimelineEngine` (`trimClip`, `splitAtPlayhead`, `reorderClip`, `deleteClip`, `recomputePositions`)
- `filters/`: `FilterPipeline` (`brightness`, `contrast`, `saturation`, `temperature`, `exposure`, `sharpness` + generated GLSL ES 3.0 fragment shader)
- `rendering/`: `CompositorMath` (aspect ratio targets `9:16`, `16:9`, `1:1`, `Free`, cover-fill crop, zoom, pan, rotation matrices)
- `audio/`: `AudioMixer` (clip gain/mute, `RateCoupled` vs `PitchPreserved` speed metadata, music trim/loop/fade-in/fade-out, timeline-positioned voiceovers, soft-clipping mixer)
- `storage/`: `JsonCodec` + `ProjectStore` (atomic JSON project persistence + missing-media validation)
- `export/`: `ExportEngine` (normalizes output to 1080p, 30 FPS, 12 Mbps H.264 / 160 kbps AAC, watermark strictly disabled)

### Android NDK Native Layer (`/android/app/src/main/cpp/`)
- `decoder/ndk_media_decoder.*`: `AMediaExtractor` + `AMediaCodec` hardware video/audio decode + `AAssetManager`/file descriptor support for `content://` URIs
- `renderer/gles_renderer.*`: `EGL` + `OpenGL ES 3.0` offscreen & `ANativeWindow` surface rendering with crop/rotate/filter shaders
- `audio/aaudio_player.*`: Low-latency `AAudio` stereo PCM playback callback
- `encoder/ndk_media_encoder.*`: `AMediaCodec` (`video/avc` + `audio/mp4a-latm`) + `AMediaMuxer` MP4 writer with progress callbacks and atomic cancellation flag
- `jni/jni_bridge.cpp`: Thread-safe JNI wrapper connecting `VideoEditorBridge.kt` to `trisvax::VideoEditorEngine`

### Library Licensing & Compatibility
- **Android NDK APIs** (`mediandk`, `EGL`, `GLESv3`, `AAudio`, `android`, `log`): Part of Android NDK (Apache 2.0 / BSD), zero extra binary bloat, native `arm64-v8a` hardware acceleration.
- **Optional FFmpeg Hook**: `android/app/src/main/cpp/CMakeLists.txt` exposes `-DTRISVAX_ENABLE_FFMPEG=OFF` by default for prebuilt `third_party/ffmpeg/<ABI>` static/shared binaries when LGPL/GPL packaging is explicitly desired.
