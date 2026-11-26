# Windows Build, NDK Setup, and ADB Physical Device Guide

## Container Verification Disclosure
Inside the automated Linux development container:
- **Verified**: All C++20 core engine unit tests (`./.build/trisvax_tests` — 53 assertions), `trisvax_cli`, TypeScript timeline unit tests, `vite build`, `tsc --noEmit`, and headless Chromium browser flows on port 3000.
- **Not Executed In-Container**: Android Gradle APK compilation (`./gradlew assembleDebug`) requires the Android SDK, Android NDK, CMake, and Gradle wrapper dependencies which are not preinstalled in the Linux container. Follow the steps below on Windows (or macOS/Linux with Android Studio) to compile and deploy the APK to a physical Android device.

---

## Prerequisites on Windows 10/11
1. **Android Studio** (Koala / Ladybug or newer) with SDK Manager components:
   - Android SDK Platform 35 (`android-35`)
   - Android SDK Build-Tools `35.0.0`
   - **NDK (Side by side)** `26.3.11579264` or `27.0.12077973`
   - **CMake** `3.22.1`
   - Android SDK Platform-Tools (`adb`)
2. **JDK 17** (bundled with Android Studio under `C:\Program Files\Android\Android Studio\jbr`)
3. **VS Code Extensions** (optional if editing outside Android Studio): *C/C++ Extension Pack*, *CMake Tools*, *Kotlin*.

---

## Build & Install via PowerShell + ADB

Open PowerShell in the `android\` directory of the downloaded/cloned repository:

```powershell
# 1. Verify JAVA_HOME and ANDROID_HOME
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$env:Path = "$env:ANDROID_HOME\platform-tools;$env:Path"

# 2. Ensure local.properties points to your SDK
"sdk.dir=$($env:ANDROID_HOME -replace '\\','\\')" | Set-Content -Encoding ASCII local.properties

# 3. Build the arm64-v8a Debug APK (compiles C++20 trisvax_core + trisvax_jni via CMake/NDK)
.\gradlew.bat assembleDebug

# 4. Connect an Android phone with USB Debugging enabled and verify ADB sees it
adb devices

# 5. Install and launch TRISVAX Studio on the phone
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.trisvax.studio/.MainActivity
```

---

## Physical Device Testing Checklist
1. **Welcome -> Projects**: Confirm dark `#0B0807` surfaces, search bar filtering, and `+` popup (`Video`, `Camera`, `Gallery`).
2. **Media Picker & Camera**: Grant runtime permissions (`READ_MEDIA_VIDEO`/`READ_MEDIA_IMAGES`, `CAMERA`, `RECORD_AUDIO`), pick multiple clips, and record a CameraX clip. Confirm large source videos are NOT duplicated inside `/data/data/com.trisvax.studio/files/`.
3. **Editor Native Preview**: Verify `SurfaceView` renders the decoded clip via EGL/GLES3, scrub playhead, toggle Undo/Redo, Adjust filters (`Brightness`, `Contrast`, `Saturation`), `Transitions`, `Speed` + pitch preservation, `Text` overlay timing, `Music` trim/fade, and `Voice` recording.
4. **1080p MP4 Export**: Tap `Next`, monitor progress notification (`VideoExportService`), verify cancellation works mid-export, and inspect the exported MP4 in `Movies/TRISVAX Studio/` via Files/Gallery (1080p, 30 FPS, no watermark).
