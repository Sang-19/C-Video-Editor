# Feature Parity & Reference Audit

This table tracks every screen and editing workflow inspected in the reference Kotlin/Media3 repository (`https://github.com/Sang-19/Trisvax-App-Kotlin-Video-Editor-`) and maps its rebuilt C++20 implementation.

| Area | Original Kotlin / Media3 Reference | C++20 Native Rebuild | Status |
| :--- | :--- | :--- | :--- |
| Welcome Screen | `TRISVAX STUDIO` badge, "Start creating", `#FF5500` "Open your studio ->", local privacy badge | Preserved identically in Compose (`WelcomeScreen.kt`) & Web (`WelcomeScreen.tsx`) | Rebuilt |
| Projects Screen | Clapper + `Projects` + Search toggle, count/storage subtitle, 2-col grid cards, `+` FAB `[Video, Camera, Gallery]`, Rename/Delete | Preserved cleanly with C++ `ProjectStore` JSON manifests + missing-media indicator | Rebuilt |
| Media Picker | Multi-select grid with ordered badges, System Photo Picker, copies every file into `filesDir` | Ordered multi-select + Photo Picker + persistable `content://` URI & thumbnail extraction (avoids copying large videos) | Improved |
| Camera Capture | CameraX Full HD capture -> project creation | Preserved in `CameraCaptureScreen.kt` + Web `getUserMedia` capture | Rebuilt |
| Video Preview | `ExoPlayer` / `PlayerView` | Android `SurfaceView` -> EGL / OpenGL ES 3.0 `GlesRenderer` + `AAudioPlayer`; Web filtered `<video>` + `<audio>` | Rebuilt |
| Timeline | Millisecond-based `TimelineState`, ruler, clips + mute, music/text/voice lanes, white playhead | C++20 microsecond `TimelineEngine` (`TimeUs`), multi-track lanes, draggable white playhead | Rebuilt |
| Trim & Split | `trimClip` + `splitClipAtPlayhead` | C++20 `TimelineEngine::trimClip` & `splitAtPlayhead` with Undo/Redo snapshots | Rebuilt |
| Crop & Rotate | Aspect presets (`9:16`, `16:9`, `1:1`, `Free`), zoom/pan, +90° rotation | C++20 `CompositorMath` + GLES3 vertex/UV transform + aspect normalization | Rebuilt |
| Adjust / Filters | Button missing / non-functional in original | Full 6-slider grading (`brightness`, `contrast`, `saturation`, `temperature`, `exposure`, `sharpness`) in C++20 & GLSL ES 3.0 + Web | Gap Fixed |
| Transitions | Missing in original | `Cut`, `Fade`, `CrossDissolve` with configurable duration (`200ms..2000ms`) | Gap Fixed |
| Speed | Forced `SonicAudioProcessor` pitch=1.0 silently | Explicit `RateCoupled` vs `PitchPreserved` (`audioSpeedMode`) in C++20 & UI toggle | Gap Fixed |
| Audio & Voiceover | Export ignored music/voice volumes and voiceover `timelineStartMs` | `AudioMixer` honors clip/music/voiceover volumes, music trim/loop/fade-in/fade-out, and `timelineStartUs` offsets | Gap Fixed |
| Text Overlays | Ignored alignment, animation, and explicit timing controls in Kotlin UI | Full color, size, alignment (`Left`/`Center`/`Right`), Y-pos, start/end timing, and animation (`None`/`Fade`/`SlideUp`) | Improved |
| Undo / Redo & Save | No Undo/Redo; `Save` button was unimplemented/conflated with export | 64-step C++20 `CommandHistory` Undo/Redo + explicit `Save` draft vs `Next` 1080p MP4 export | Gap Fixed |
| MP4 Export | `Media3 Transformer` 1080p MP4 + `MediaStore` | Android NDK `AMediaCodec`/`AMediaMuxer` (`1080p`, `30 FPS`, H.264/AAC, zero watermark) + foreground service; Web C++20 validation + FFmpeg 1080p MP4 | Rebuilt |
