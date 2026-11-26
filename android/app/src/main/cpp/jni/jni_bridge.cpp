#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#include "audio/aaudio_player.hpp"
#include "encoder/ndk_media_encoder.hpp"
#include "native/engine/video_editor_engine.hpp"
#include "native/storage/json_codec.hpp"
#include "renderer/gles_renderer.hpp"

#include <memory>
#include <mutex>
#include <string>

#define LOG_TAG "TrisvaxJNI"

namespace {

JavaVM* gJvm = nullptr;

class ScopedJniEnv {
public:
  explicit ScopedJniEnv(JavaVM* vm) : vm_(vm) {
    if (!vm_) return;
    jint res = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
      if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
        attached_ = true;
      }
    }
  }
  ~ScopedJniEnv() {
    if (attached_ && vm_) {
      vm_->DetachCurrentThread();
    }
  }
  JNIEnv* get() const noexcept { return env_; }

private:
  JavaVM* vm_ = nullptr;
  JNIEnv* env_ = nullptr;
  bool attached_ = false;
};

struct NativeEngineContext {
  std::unique_ptr<trisvax::VideoEditorEngine> engine;
  std::unique_ptr<trisvax::android::GlesRenderer> renderer;
  std::unique_ptr<trisvax::android::AAudioPreviewPlayer> audioPlayer;
};

std::string jstringToStd(JNIEnv* env, jstring s) {
  if (!s) return "";
  const char* chars = env->GetStringUTFChars(s, nullptr);
  std::string out(chars ? chars : "");
  if (chars) env->ReleaseStringUTFChars(s, chars);
  return out;
}

NativeEngineContext* fromHandle(jlong handle) {
  return reinterpret_cast<NativeEngineContext*>(handle);
}

} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gJvm = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeCreate(
    JNIEnv* env, jobject, jstring workspaceDir) {
  try {
    auto ctx = std::make_unique<NativeEngineContext>();
    auto backend = std::make_shared<trisvax::android::NdkMediaBackendFactory>();
    ctx->engine = std::make_unique<trisvax::VideoEditorEngine>(jstringToStd(env, workspaceDir), backend);
    ctx->engine->initialize();
    ctx->renderer = std::make_unique<trisvax::android::GlesRenderer>();
    ctx->audioPlayer = std::make_unique<trisvax::android::AAudioPreviewPlayer>();
    return reinterpret_cast<jlong>(ctx.release());
  } catch (const std::exception& e) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "nativeCreate failed: %s", e.what());
    return 0;
  } catch (...) {
    return 0;
  }
}

JNIEXPORT void JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeDestroy(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return;
    if (ctx->audioPlayer) ctx->audioPlayer->stop();
    if (ctx->renderer) ctx->renderer->detach();
    if (ctx->engine) ctx->engine->release();
    delete ctx;
  } catch (...) {
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeListProjectsJson(
    JNIEnv* env, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("[]");
    const auto list = ctx->engine->listProjects();
    trisvax::JsonArray arr;
    for (const auto& m : list) {
      arr.emplace_back(trisvax::JsonObject{
          {"id", m.id.value},
          {"name", m.name},
          {"category", trisvax::categoryToString(m.category)},
          {"thumbnailPath", m.thumbnailPath},
          {"durationMs", trisvax::usToMs(m.durationUs)},
          {"storageSizeBytes", m.storageSizeBytes},
          {"createdAtMs", m.createdAtMs},
          {"lastModifiedAtMs", m.lastModifiedAtMs},
          {"exportedPath", m.exportedPath},
          {"missingMediaCount", m.missingMediaCount},
      });
    }
    return env->NewStringUTF(trisvax::JsonValue(std::move(arr)).dump(0).c_str());
  } catch (...) {
    return env->NewStringUTF("[]");
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeCreateProject(
    JNIEnv* env, jobject, jlong handle, jstring name) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("");
    auto res = ctx->engine->createProject(jstringToStd(env, name));
    return env->NewStringUTF(res.ok ? res.value.value.c_str() : "");
  } catch (...) {
    return env->NewStringUTF("");
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeOpenProject(
    JNIEnv* env, jobject, jlong handle, jstring projectId) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->openProject(trisvax::ProjectId{jstringToStd(env, projectId)}).ok()
               ? JNI_TRUE
               : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSaveProject(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->saveProject().ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeRenameProject(
    JNIEnv* env, jobject, jlong handle, jstring projectId, jstring newName) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->renameProject(
               trisvax::ProjectId{jstringToStd(env, projectId)},
               jstringToStd(env, newName)).ok()
               ? JNI_TRUE
               : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeDeleteProject(
    JNIEnv* env, jobject, jlong handle, jstring projectId) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->deleteProject(trisvax::ProjectId{jstringToStd(env, projectId)}).ok()
               ? JNI_TRUE
               : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeImportMedia(
    JNIEnv* env, jobject, jlong handle, jstring uri, jstring displayName, jboolean isImage,
    jlong durationUs, jint width, jint height, jlong fileSizeBytes, jstring thumbnailPath) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("");
    trisvax::MediaSource src;
    src.uri = jstringToStd(env, uri);
    src.displayName = jstringToStd(env, displayName);
    src.isImage = (isImage == JNI_TRUE);
    src.durationUs = static_cast<trisvax::TimeUs>(durationUs);
    src.width = width;
    src.height = height;
    src.fileSizeBytes = fileSizeBytes;
    src.thumbnailPath = jstringToStd(env, thumbnailPath);
    auto res = ctx->engine->importMedia(src);
    return env->NewStringUTF(res.ok ? res.value.value.c_str() : "");
  } catch (...) {
    return env->NewStringUTF("");
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeGetActiveProjectJson(
    JNIEnv* env, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("{}");
    return env->NewStringUTF(ctx->engine->activeProjectJson().c_str());
  } catch (...) {
    return env->NewStringUTF("{}");
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeTrimClip(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jlong startUs, jlong endUs) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->trimClip(trisvax::ClipId{jstringToStd(env, clipId)}, startUs, endUs).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSplitAtPlayhead(
    JNIEnv*, jobject, jlong handle, jlong playheadUs) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->splitClipAtPlayhead(playheadUs).ok ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetSpeed(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jdouble speed, jint audioSpeedMode) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->setPlaybackSpeed(
               trisvax::ClipId{jstringToStd(env, clipId)},
               speed,
               static_cast<trisvax::AudioSpeedMode>(audioSpeedMode)).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetVolume(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jfloat volume, jboolean muted) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->setVolume(
               trisvax::ClipId{jstringToStd(env, clipId)}, volume, muted == JNI_TRUE).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetCrop(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jstring preset, jint aspectW, jint aspectH,
    jfloat offsetX, jfloat offsetY, jfloat zoom, jint rotationDeg) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    trisvax::CropSettings crop;
    crop.preset = trisvax::aspectPresetFromString(jstringToStd(env, preset));
    crop.aspectWidth = aspectW;
    crop.aspectHeight = aspectH;
    crop.offsetX = offsetX;
    crop.offsetY = offsetY;
    crop.zoom = zoom;
    crop.rotationDegrees = rotationDeg;
    ctx->engine->setAspectRatio(crop.preset, aspectW, aspectH);
    return ctx->engine->setCrop(trisvax::ClipId{jstringToStd(env, clipId)}, crop).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetFilter(
    JNIEnv* env, jobject, jlong handle, jstring clipId,
    jfloat brightness, jfloat contrast, jfloat saturation,
    jfloat temperature, jfloat exposure, jfloat sharpness) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    trisvax::FilterSettings f{brightness, contrast, saturation, temperature, exposure, sharpness};
    return ctx->engine->addFilter(trisvax::ClipId{jstringToStd(env, clipId)}, f).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetTransition(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jint transitionType, jlong durationUs) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    trisvax::TransitionSpec spec{static_cast<trisvax::TransitionType>(transitionType), durationUs};
    return ctx->engine->setTransition(trisvax::ClipId{jstringToStd(env, clipId)}, spec).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeReorderClip(
    JNIEnv*, jobject, jlong handle, jint fromIdx, jint toIdx) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx || fromIdx < 0 || toIdx < 0) return JNI_FALSE;
    return ctx->engine->reorderClip(static_cast<size_t>(fromIdx), static_cast<size_t>(toIdx)).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeDeleteClip(
    JNIEnv* env, jobject, jlong handle, jstring clipId) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->removeClip(trisvax::ClipId{jstringToStd(env, clipId)}).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeUndo(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    return (ctx && ctx->engine->undo()) ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeRedo(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    return (ctx && ctx->engine->redo()) ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeCanUndo(
    JNIEnv*, jobject, jlong handle) {
  auto* ctx = fromHandle(handle);
  return (ctx && ctx->engine->canUndo()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeCanRedo(
    JNIEnv*, jobject, jlong handle) {
  auto* ctx = fromHandle(handle);
  return (ctx && ctx->engine->canRedo()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetMusicTrack(
    JNIEnv* env, jobject, jlong handle, jstring title, jstring sourceUri,
    jboolean isBundled, jlong durationUs, jlong trimStartUs, jlong trimEndUs,
    jfloat volume, jboolean muted, jboolean loop, jlong fadeInUs, jlong fadeOutUs) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    trisvax::AudioClip m;
    m.title = jstringToStd(env, title);
    m.sourceUri = jstringToStd(env, sourceUri);
    m.isBundledAsset = (isBundled == JNI_TRUE);
    m.sourceDurationUs = durationUs;
    m.trimStartUs = trimStartUs;
    m.trimEndUs = trimEndUs;
    m.volume = volume;
    m.isMuted = (muted == JNI_TRUE);
    m.loopToFit = (loop == JNI_TRUE);
    m.fadeInUs = fadeInUs;
    m.fadeOutUs = fadeOutUs;
    return ctx->engine->setMusicTrack(m).ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeRemoveMusicTrack(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    return (ctx && ctx->engine->removeMusicTrack().ok()) ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeAddVoiceover(
    JNIEnv* env, jobject, jlong handle, jstring title, jstring filePath,
    jlong durationUs, jlong timelineStartUs, jfloat volume) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("");
    trisvax::VoiceoverClip vo;
    vo.title = jstringToStd(env, title);
    vo.filePath = jstringToStd(env, filePath);
    vo.sourceDurationUs = durationUs;
    vo.trimStartUs = 0;
    vo.trimEndUs = durationUs;
    vo.timelineStartUs = timelineStartUs;
    vo.volume = volume;
    auto res = ctx->engine->addVoiceover(vo);
    return env->NewStringUTF(res.ok ? res.value.value.c_str() : "");
  } catch (...) {
    return env->NewStringUTF("");
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeRemoveVoiceover(
    JNIEnv* env, jobject, jlong handle, jstring voId) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->removeVoiceover(trisvax::VoiceoverId{jstringToStd(env, voId)}).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jstring JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeUpsertTextOverlay(
    JNIEnv* env, jobject, jlong handle, jstring id, jstring text, jstring fontFamily,
    jfloat fontSizeSp, jlong colorArgb, jint align, jfloat normX, jfloat normY,
    jlong startUs, jlong endUs, jint animation) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return env->NewStringUTF("");
    trisvax::TextOverlay t;
    std::string idStr = jstringToStd(env, id);
    if (!idStr.empty()) t.id = trisvax::TextOverlayId{idStr};
    t.text = jstringToStd(env, text);
    t.fontFamily = jstringToStd(env, fontFamily);
    t.fontSizeSp = fontSizeSp;
    t.colorArgb = static_cast<uint32_t>(colorArgb);
    t.alignment = static_cast<trisvax::TextAlign>(align);
    t.normalizedX = normX;
    t.normalizedY = normY;
    t.startUs = startUs;
    t.endUs = endUs;
    t.animation = static_cast<trisvax::TextAnimation>(animation);
    auto res = ctx->engine->addOrUpdateTextOverlay(t);
    return env->NewStringUTF(res.ok ? res.value.value.c_str() : "");
  } catch (...) {
    return env->NewStringUTF("");
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeRemoveTextOverlay(
    JNIEnv* env, jobject, jlong handle, jstring id) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->removeTextOverlay(trisvax::TextOverlayId{jstringToStd(env, id)}).ok()
               ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT void JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSeekTo(
    JNIEnv*, jobject, jlong handle, jlong playheadUs) {
  try {
    auto* ctx = fromHandle(handle);
    if (ctx && ctx->engine) {
      ctx->engine->seekTo(playheadUs);
    }
    if (ctx && ctx->audioPlayer) {
      ctx->audioPlayer->setPlayheadUs(playheadUs);
    }
  } catch (...) {
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeExportProject(
    JNIEnv* env, jobject, jlong handle, jstring outputPath, jint fps, jobject callbackObj) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;

    jobject globalCb = callbackObj ? env->NewGlobalRef(callbackObj) : nullptr;
    jmethodID onProgMethod = nullptr;
    if (globalCb) {
      jclass cls = env->GetObjectClass(globalCb);
      onProgMethod = env->GetMethodID(cls, "onProgress", "(ILjava/lang/String;)V");
    }

    trisvax::ExportSettings settings;
    settings.outputPath = jstringToStd(env, outputPath);
    settings.fps = fps > 0 ? fps : 30;
    settings.watermarkEnabled = false;

    trisvax::Error err = ctx->engine->exportProject(settings, [&](const trisvax::ExportProgress& p) {
      if (!globalCb || !onProgMethod || !gJvm) return;
      ScopedJniEnv scoped(gJvm);
      JNIEnv* cbEnv = scoped.get();
      if (!cbEnv) return;
      jstring jMsg = cbEnv->NewStringUTF(p.statusMessage.c_str());
      cbEnv->CallVoidMethod(globalCb, onProgMethod, static_cast<jint>(p.percent), jMsg);
      if (jMsg) cbEnv->DeleteLocalRef(jMsg);
    });

    if (globalCb) {
      env->DeleteGlobalRef(globalCb);
    }
    return err.ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT void JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeCancelExport(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (ctx && ctx->engine) {
      ctx->engine->cancelExport();
    }
  } catch (...) {
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeAttachPreviewSurface(
    JNIEnv* env, jobject, jlong handle, jobject surface, jint width, jint height) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx || !ctx->renderer) return JNI_FALSE;
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (!win) return JNI_FALSE;
    bool ok = ctx->renderer->attachSurface(win, width, height);
    ANativeWindow_release(win);
    return ok ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT void JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeDetachPreviewSurface(
    JNIEnv*, jobject, jlong handle) {
  try {
    auto* ctx = fromHandle(handle);
    if (ctx && ctx->renderer) {
      ctx->renderer->detach();
    }
  } catch (...) {
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetClipPlacement(
    JNIEnv* env, jobject, jlong handle, jstring clipId, jlong timelinePositionUs, jint trackIndex) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    return ctx->engine->setClipPlacement(
               trisvax::ClipId{jstringToStd(env, clipId)},
               static_cast<trisvax::TimeUs>(timelinePositionUs),
               static_cast<int>(trackIndex))
               .ok()
               ? JNI_TRUE
               : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeSetTrackState(
    JNIEnv*, jobject, jlong handle, jint kind, jint trackIndex, jboolean locked, jboolean muted, jboolean hidden, jboolean solo) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    trisvax::TimelineTrackState st;
    st.kind = static_cast<trisvax::TrackKind>(kind);
    st.trackIndex = static_cast<int>(trackIndex);
    st.locked = (locked == JNI_TRUE);
    st.muted = (muted == JNI_TRUE);
    st.hidden = (hidden == JNI_TRUE);
    st.solo = (solo == JNI_TRUE);
    return ctx->engine->setTrackState(st).ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

namespace {
trisvax::TimelineSelection parseSelectionJson(const std::string& jsonStr) {
  trisvax::TimelineSelection sel;
  auto parsed = trisvax::JsonValue::parse(jsonStr);
  if (!parsed.ok) return sel;
  const auto* obj = parsed.value.asObject();
  if (!obj) return sel;
  auto readIds = [&](const char* key, auto pushFn) {
    auto it = obj->find(key);
    if (it == obj->end()) return;
    if (const auto* arr = it->second.asArray()) {
      for (const auto& el : *arr) {
        std::string s = el.asString();
        if (!s.empty()) pushFn(s);
      }
    }
  };
  readIds("clipIds", [&](const std::string& id) { sel.clipIds.push_back(trisvax::ClipId{id}); });
  readIds("audioIds", [&](const std::string& id) { sel.audioIds.push_back(trisvax::AudioTrackId{id}); });
  readIds("voiceoverIds", [&](const std::string& id) { sel.voiceoverIds.push_back(trisvax::VoiceoverId{id}); });
  readIds("textIds", [&](const std::string& id) { sel.textIds.push_back(trisvax::TextOverlayId{id}); });
  return sel;
}
} // namespace

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeBatchShiftSelection(
    JNIEnv* env, jobject, jlong handle, jstring selectionJson, jlong deltaUs, jint trackDelta) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    auto sel = parseSelectionJson(jstringToStd(env, selectionJson));
    return ctx->engine->batchShiftSelection(sel, static_cast<trisvax::TimeUs>(deltaUs), static_cast<int>(trackDelta)).ok()
               ? JNI_TRUE
               : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeBatchMuteSelection(
    JNIEnv* env, jobject, jlong handle, jstring selectionJson, jboolean muted) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    auto sel = parseSelectionJson(jstringToStd(env, selectionJson));
    return ctx->engine->batchMuteSelection(sel, muted == JNI_TRUE).ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

JNIEXPORT jboolean JNICALL
Java_com_trisvax_studio_nativebridge_VideoEditorBridge_nativeBatchDeleteSelection(
    JNIEnv* env, jobject, jlong handle, jstring selectionJson) {
  try {
    auto* ctx = fromHandle(handle);
    if (!ctx) return JNI_FALSE;
    auto sel = parseSelectionJson(jstringToStd(env, selectionJson));
    return ctx->engine->batchDeleteSelection(sel).ok() ? JNI_TRUE : JNI_FALSE;
  } catch (...) {
    return JNI_FALSE;
  }
}

} // extern "C"
