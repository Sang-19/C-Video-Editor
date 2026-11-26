#include "native/audio/audio_mixer.hpp"
#include "native/engine/video_editor_engine.hpp"
#include "native/export/export_engine.hpp"
#include "native/filters/filter_pipeline.hpp"
#include "native/rendering/compositor_math.hpp"
#include "native/storage/json_codec.hpp"
#include "native/timeline/timeline_engine.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using namespace trisvax;

namespace {

int gPassed = 0;
int gFailed = 0;

void check(bool cond, const char* name) {
  if (cond) {
    ++gPassed;
  } else {
    ++gFailed;
    std::cerr << "[FAIL] " << name << "\n";
  }
}

class MockDecoder final : public IMediaDecoder {
public:
  Error open(const std::string&) override { return Error::success(); }
  MediaSource probeInfo() const override {
    MediaSource ms;
    ms.durationUs = msToUs(4000);
    ms.width = 1080;
    ms.height = 1920;
    return ms;
  }
  Error seek(TimeUs) override { return Error::success(); }
  bool readNextVideoFrame(DecodedVideoFrame& outFrame) override {
    outFrame.width = 1080;
    outFrame.height = 1920;
    outFrame.valid = true;
    outFrame.rgbaPixels.assign(4, 128);
    return true;
  }
  bool readNextAudioChunk(DecodedAudioChunk&) override { return false; }
  void close() override {}
};

class MockEncoder final : public IMediaEncoder {
public:
  explicit MockEncoder(bool* cancelledFlag, int* framesEncoded)
      : cancelled_(cancelledFlag), framesEncoded_(framesEncoded) {}

  Error open(const ExportSettings& settings) override {
    std::ofstream ofs(settings.outputPath, std::ios::binary);
    ofs << "ftypmp42";
    return Error::success();
  }
  Error encodeVideoFrameRgba(const uint8_t*, int, int, TimeUs) override {
    ++(*framesEncoded_);
    return Error::success();
  }
  Error encodeAudioPcm(const float*, size_t, TimeUs) override { return Error::success(); }
  Error finish() override { return Error::success(); }
  void abort() override { *cancelled_ = true; }

private:
  bool* cancelled_;
  int* framesEncoded_;
};

class MockBackend final : public MediaBackendFactory {
public:
  int64_t freeBytes = 500LL * 1024LL * 1024LL;
  bool wasAborted = false;
  int framesEncoded = 0;
  ExportEngine* cancelTarget = nullptr;
  int cancelAfterFrames = -1;

  std::unique_ptr<IMediaDecoder> createDecoder() override {
    return std::make_unique<MockDecoder>();
  }

  class CancellingEncoder final : public IMediaEncoder {
  public:
    explicit CancellingEncoder(MockBackend* b) : b_(b) {}
    Error open(const ExportSettings& s) override {
      path_ = s.outputPath;
      std::ofstream ofs(path_, std::ios::binary);
      ofs << "ftyp";
      return Error::success();
    }
    Error encodeVideoFrameRgba(const uint8_t*, int, int, TimeUs) override {
      ++b_->framesEncoded;
      if (b_->cancelAfterFrames > 0 && b_->framesEncoded >= b_->cancelAfterFrames && b_->cancelTarget) {
        b_->cancelTarget->cancel();
      }
      return Error::success();
    }
    Error encodeAudioPcm(const float*, size_t, TimeUs) override { return Error::success(); }
    Error finish() override { return Error::success(); }
    void abort() override { b_->wasAborted = true; }
  private:
    MockBackend* b_;
    std::string path_;
  };

  std::unique_ptr<IMediaEncoder> createEncoder() override {
    return std::make_unique<CancellingEncoder>(this);
  }

  int64_t availableStorageBytes(const std::string&) const override {
    return freeBytes;
  }
};

void testTimelineAndTimestampMath() {
  Clip c1;
  c1.id = ClipId{"c1"};
  c1.sourceUri = "file:///clip1.mp4";
  c1.sourceDurationUs = msToUs(10'000);
  c1.inPointUs = msToUs(1'000);
  c1.outPointUs = msToUs(7'000);
  c1.playbackSpeed = 1.5;

  check(TimelineEngine::clipEffectiveDurationUs(c1) == msToUs(4'000), "speed 1.5x divides 6s span to 4s");

  std::vector<Clip> clips{c1};
  TimelineEngine::recomputePositions(clips);
  check(clips[0].timelinePositionUs == 0, "first clip starts at 0us");

  // Split at 2s timeline offset -> 2s * 1.5 = 3s into trimmed clip -> 1s + 3s = 4s source
  ClipId secondId;
  Error splitErr = TimelineEngine::splitClipAtPlayhead(clips, msToUs(2'000), &secondId);
  check(splitErr.ok(), "splitClipAtPlayhead succeeds");
  check(clips.size() == 2, "split produces 2 clips");
  check(clips[0].outPointUs == msToUs(4'000), "first half outPointUs matches speed-scaled split");
  check(clips[1].inPointUs == msToUs(4'000), "second half inPointUs matches split");
  check(clips[1].timelinePositionUs == msToUs(2'000), "second half timelinePositionUs is 2s");

  // Trim clamping
  Error trimErr = TimelineEngine::trimClip(clips, clips[0].id, -500, msToUs(20'000));
  check(trimErr.ok(), "trimClip clamps bounds cleanly");
  check(clips[0].inPointUs == 0 && clips[0].outPointUs == msToUs(10'000), "trimClip clamped to [0, sourceDuration]");

  // Playhead lookup
  auto match = TimelineEngine::findClipAtPlayhead(clips, msToUs(7'000));
  check(match.has_value() && match->clipIndex == 1, "findClipAtPlayhead locates second clip");
}

void testCropAndCompositorMath() {
  auto dims916 = CompositorMath::computeTargetDimensions(AspectRatioPreset::Portrait9x16);
  check(dims916.width == 1080 && dims916.height == 1920, "9:16 resolves to 1080x1920");

  auto dims169 = CompositorMath::computeTargetDimensions(AspectRatioPreset::Landscape16x9);
  check(dims169.width == 1920 && dims169.height == 1080, "16:9 resolves to 1920x1080");

  auto dims11 = CompositorMath::computeTargetDimensions(AspectRatioPreset::Square1x1);
  check(dims11.width == 1080 && dims11.height == 1080, "1:1 resolves to 1080x1080");

  CropSettings crop;
  crop.zoom = 2.0f;
  crop.offsetX = 1.0f;
  crop.offsetY = -1.0f;
  auto uv = CompositorMath::computeCropUvRect(1080, 1920, 1080, 1920, crop);
  check(std::fabs((uv.right - uv.left) - 0.5f) < 1e-4f, "2x zoom halves UV width");
  check(std::fabs(uv.right - 1.0f) < 1e-4f, "max positive offsetX pins right UV to 1.0");
  check(std::fabs(uv.top - 0.0f) < 1e-4f, "max negative offsetY pins top UV to 0.0");

  Clip transClip;
  transClip.transitionIn = TransitionSpec{TransitionType::CrossDissolve, msToUs(1000)};
  auto trMid = CompositorMath::evaluateTransition(transClip, msToUs(500));
  check(trMid.active && std::fabs(trMid.alpha - 0.5f) < 1e-4f, "transition midpoint alpha is 0.5");

  TextOverlay ov;
  ov.startUs = msToUs(1000);
  ov.endUs = msToUs(5000);
  ov.animation = TextAnimation::FadeInOut;
  check(!CompositorMath::evaluateTextOverlay(ov, msToUs(500)).visible, "text hidden before startUs");
  check(CompositorMath::evaluateTextOverlay(ov, msToUs(2500)).visible, "text visible inside range");
}

void testFiltersAndAudioMixer() {
  FilterSettings raw;
  raw.brightness = 5.0f;
  raw.contrast = 0.1f;
  raw.sharpness = 2.0f;
  FilterSettings clamped = FilterPipeline::clamp(raw);
  check(clamped.brightness == 1.0f && clamped.contrast == 0.5f && clamped.sharpness == 1.0f,
        "filter clamping enforces bounds");

  std::vector<uint8_t> px = {100, 100, 100, 255, 100, 100, 100, 255};
  FilterSettings warm;
  warm.temperature = 1.0f;
  FilterPipeline::applyRgba8888(px.data(), 2, 1, 8, warm);
  check(px[0] > px[2], "warm temperature boosts red relative to blue");
  check(!FilterPipeline::buildGlslEs300FragmentShader().empty(), "GLSL ES 3.00 shader generated");

  // Audio mixing + fade-in + speed
  std::vector<float> tone(44100 * 2, 0.4f); // 1s stereo constant 0.4
  std::vector<float> out(4410 * 2, 0.0f);   // 100ms
  AudioTrackMixSource src;
  src.interleavedStereoPcm = tone.data();
  src.frameCount = 44100;
  src.gain = 0.5f;
  src.trimStartUs = 0;
  src.trimEndUs = msToUs(1000);
  src.fadeInUs = msToUs(100);
  AudioMixer::mixStereoBuffer(out.data(), 4410, 44100, 0, {src});
  check(std::fabs(out[0]) < 1e-4f, "fade-in starts at 0 gain");
  check(out[4409 * 2] > 0.15f && out[4409 * 2] <= 0.25f, "fade-in ramps up to track gain");

  auto fastRate = AudioMixer::processSpeed(tone.data(), 44100, 44100, 2.0, AudioSpeedMode::RateCoupled);
  check(fastRate.size() == (44100 / 2) * 2, "2.0x speed halves PCM frame count");
  auto fastPitch = AudioMixer::processSpeed(tone.data(), 44100, 44100, 2.0, AudioSpeedMode::PitchPreserved);
  check(fastPitch.size() == (44100 / 2) * 2, "2.0x pitch-preserved SOLA halves PCM frame count");
}

void testEngineUndoRedoPersistenceAndExport() {
  const fs::path tempRoot = fs::temp_directory_path() / "trisvax_cpp_tests";
  fs::remove_all(tempRoot);
  fs::create_directories(tempRoot);

  // Create dummy media file on disk so media availability check passes
  const fs::path dummyMedia = tempRoot / "sample_clip.mp4";
  {
    std::ofstream ofs(dummyMedia, std::ios::binary);
    ofs << "dummy-mp4";
  }

  auto backend = std::make_shared<MockBackend>();
  VideoEditorEngine engine(tempRoot.string(), backend);
  check(engine.initialize(), "engine initializes");

  auto projRes = engine.createProject("Test Reel");
  check(projRes.ok, "createProject succeeds");

  MediaSource ms;
  ms.uri = dummyMedia.string();
  ms.displayName = "sample_clip.mp4";
  ms.durationUs = msToUs(5000);
  auto c1 = engine.importMedia(ms);
  auto c2 = engine.importMedia(ms);
  check(c1.ok && c2.ok, "importMedia adds two clips");
  check(engine.totalDurationUs() == msToUs(10000), "total duration is 10s");

  // Trim + Undo + Redo
  check(engine.trimClip(c1.value, msToUs(1000), msToUs(4000)).ok(), "trimClip succeeds");
  check(engine.totalDurationUs() == msToUs(8000), "trimmed duration is 8s");
  check(engine.canUndo() && engine.undo(), "undo trim succeeds");
  check(engine.totalDurationUs() == msToUs(10000), "undo restores 10s duration");
  check(engine.canRedo() && engine.redo(), "redo trim succeeds");
  check(engine.totalDurationUs() == msToUs(8000), "redo restores 8s duration");

  // Crop, filter, text, audio, reorder + undo/redo
  CropSettings crop;
  crop.zoom = 1.6f;
  crop.offsetX = 0.25f;
  check(engine.setCrop(c1.value, crop).ok(), "setCrop succeeds");
  check(engine.addFilter(c1.value, FilterSettings{0.2f, 1.2f, 1.1f, 0.1f, 0.0f, 0.3f}).ok(), "addFilter succeeds");
  TextOverlay txt;
  txt.text = "TRISVAX";
  txt.startUs = 0;
  txt.endUs = msToUs(3000);
  check(engine.addOrUpdateTextOverlay(txt).ok, "addOrUpdateTextOverlay succeeds");
  check(engine.reorderClip(0, 1).ok(), "reorderClip succeeds");
  check(engine.activeProjectSnapshot().timeline.clips[0].id == c2.value, "clip 2 moved to index 0");
  check(engine.undo(), "undo reorder succeeds");
  check(engine.activeProjectSnapshot().timeline.clips[0].id == c1.value, "undo restored clip 1 to index 0");

  // Save & reopen project from disk
  check(engine.saveProject().ok(), "saveProject writes JSON manifest");
  VideoEditorEngine reopened(tempRoot.string(), backend);
  check(reopened.openProject(projRes.value).ok(), "reopened engine loads saved project");
  check(reopened.activeProjectSnapshot().timeline.textOverlays.size() == 1, "reopened project preserves text overlay");
  check(reopened.activeProjectSnapshot().metadata.missingMediaCount == 0, "media marked available");

  // Missing media check
  fs::remove(dummyMedia);
  VideoEditorEngine missingCheck(tempRoot.string(), backend);
  check(missingCheck.openProject(projRes.value).ok(), "project still opens when source media missing");
  check(missingCheck.activeProjectSnapshot().metadata.missingMediaCount == 2, "missing media flagged safely");

  // Recreate media for export tests
  {
    std::ofstream ofs(dummyMedia, std::ios::binary);
    ofs << "dummy-mp4";
  }
  check(reopened.openProject(projRes.value).ok(), "reload with media restored");

  // Export settings & execution
  ExportSettings expSettings;
  expSettings.outputPath = (tempRoot / "out_1080p.mp4").string();
  expSettings.fps = 30;
  int lastPct = 0;
  Error expErr = reopened.exportProject(expSettings, [&](const ExportProgress& p) {
    lastPct = p.percent;
  });
  check(expErr.ok() && lastPct == 100 && fs::exists(expSettings.outputPath), "exportProject writes valid output");
  check(!reopened.activeProjectSnapshot().exportSettings.watermarkEnabled, "watermark remains disabled");

  // Export cancellation test
  backend->cancelAfterFrames = 3;
  ExportEngine cancelEngine(backend);
  backend->cancelTarget = &cancelEngine;
  const fs::path cancelledOut = tempRoot / "cancelled.mp4";
  ExportSettings cancelSet;
  cancelSet.outputPath = cancelledOut.string();
  cancelSet.fps = 30;
  Project snap = reopened.activeProjectSnapshot();
  Error cancelErr = cancelEngine.exportProject(snap, cancelSet, nullptr);
  check(cancelErr.code == ErrorCode::Cancelled && backend->wasAborted && !fs::exists(cancelledOut),
        "export cancellation aborts and cleans temp files");

  fs::remove_all(tempRoot);
}

void testMultiTrackAndBatchSelection() {
  const fs::path tempRoot = fs::temp_directory_path() / "trisvax_multitrack_test";
  std::error_code ec;
  fs::remove_all(tempRoot, ec);
  fs::create_directories(tempRoot);

  auto backend = std::make_shared<MockBackend>();
  VideoEditorEngine engine(tempRoot.string(), backend);
  engine.initialize();
  auto projRes = engine.createProject("MultiTrack");
  check(projRes.ok, "createProject for multi-track succeeds");

  Clip v1Clip;
  v1Clip.sourceUri = "file://v1.mp4";
  v1Clip.displayName = "V1 Clip";
  v1Clip.sourceDurationUs = msToUs(5000);
  v1Clip.inPointUs = 0;
  v1Clip.outPointUs = msToUs(5000);
  v1Clip.trackIndex = 0;
  auto c1 = engine.addClip(v1Clip);

  Clip v2Clip = v1Clip;
  v2Clip.displayName = "V2 Overlay";
  v2Clip.trackIndex = 1;
  v2Clip.timelinePositionUs = msToUs(2000);
  auto c2 = engine.addClip(v2Clip);
  check(c1.ok && c2.ok, "add V1 and V2 clips succeeds");
  check(engine.totalDurationUs() == msToUs(7000), "total duration honors V2 overlay extending to 7s");

  AudioClip music;
  music.title = "Beat";
  music.sourceUri = "bundled://beat";
  music.sourceDurationUs = msToUs(10000);
  music.trimStartUs = 0;
  music.trimEndUs = msToUs(6000);
  music.timelineStartUs = msToUs(1000);
  music.loopToFit = false;
  music.trackIndex = 0;
  check(engine.setMusicTrack(music).ok(), "setMusicTrack on A1 succeeds");

  VoiceoverClip vo;
  vo.title = "VO1";
  vo.filePath = "file://vo.wav";
  vo.sourceDurationUs = msToUs(4000);
  vo.trimStartUs = 0;
  vo.trimEndUs = msToUs(4000);
  vo.timelineStartUs = msToUs(1500);
  vo.trackIndex = 1;
  auto voId = engine.addVoiceover(vo);
  check(voId.ok, "addVoiceover on A2 succeeds");

  TextOverlay txt;
  txt.text = "MultiTitle";
  txt.startUs = msToUs(1500);
  txt.endUs = msToUs(4500);
  txt.trackIndex = 0;
  auto tId = engine.addOrUpdateTextOverlay(txt);
  check(tId.ok, "add text overlay on T1 succeeds");

  // Marquee select across Video + Audio + Text in [1500ms, 3000ms]
  TimelineSelection sel = engine.marqueeSelect(msToUs(1500), msToUs(3000), true, true, true, 0, 5);
  check(sel.clipIds.size() == 2, "marquee selects both V1 and V2 overlapping clips");
  check(sel.audioIds.size() == 1 && sel.voiceoverIds.size() == 1, "marquee selects music + voiceover");
  check(sel.textIds.size() == 1, "marquee selects text overlay");

  // Batch shift by +500ms
  check(engine.batchShiftSelection(sel, msToUs(500), 0).ok(), "batchShiftSelection succeeds");
  const auto snapAfterShift = engine.activeProjectSnapshot();
  check(snapAfterShift.timeline.clips[1].timelinePositionUs == msToUs(2500), "V2 clip shifted to 2.5s");
  check(snapAfterShift.timeline.musicTrack->timelineStartUs == msToUs(1500), "Music track shifted to 1.5s");
  check(snapAfterShift.timeline.textOverlays[0].startUs == msToUs(2000), "Text shifted to 2.0s");

  // Batch mute + undo
  check(engine.batchMuteSelection(sel, true).ok(), "batchMuteSelection succeeds");
  check(engine.activeProjectSnapshot().timeline.clips[0].isMuted &&
        engine.activeProjectSnapshot().timeline.musicTrack->isMuted &&
        engine.activeProjectSnapshot().timeline.voiceovers[0].isMuted,
        "batchMuteSelection mutes video, music, and voiceover");
  check(engine.undo(), "undo batchMuteSelection succeeds");
  check(!engine.activeProjectSnapshot().timeline.musicTrack->isMuted, "undo restores music unmuted");

  // Lock Text T1 -> marquee should skip locked text
  TimelineTrackState lockText{TrackKind::Text, 0, true, false, false, false};
  check(engine.setTrackState(lockText).ok(), "locking Text track succeeds");
  TimelineSelection selWithLock = engine.marqueeSelect(msToUs(1500), msToUs(3000), true, true, true, 0, 5);
  check(selWithLock.textIds.empty(), "locked Text track excluded from marquee selection");

  // Save and reload preserves trackIndex & trackStates
  check(engine.saveProject().ok(), "saveProject with multi-track succeeds");
  VideoEditorEngine reloaded(tempRoot.string(), backend);
  check(reloaded.openProject(projRes.value).ok(), "reload multi-track project succeeds");
  const auto reloadedSnap = reloaded.activeProjectSnapshot();
  check(reloadedSnap.timeline.clips[1].trackIndex == 1 &&
        reloadedSnap.timeline.clips[1].timelinePositionUs == msToUs(2500),
        "reloaded project preserves V2 trackIndex & timelinePositionUs");
  check(reloadedSnap.timeline.trackStates.size() == 1 && reloadedSnap.timeline.trackStates[0].locked,
        "reloaded project preserves trackStates");

  // Batch delete clears music/voiceover while keeping at least 1 video clip
  check(engine.batchDeleteSelection(sel).ok(), "batchDeleteSelection succeeds");
  check(!engine.activeProjectSnapshot().timeline.musicTrack.has_value() &&
        engine.activeProjectSnapshot().timeline.voiceovers.empty() &&
        engine.activeProjectSnapshot().timeline.clips.size() == 1,
        "batchDeleteSelection removes audio/voiceover and preserves fallback V1 clip");

  fs::remove_all(tempRoot);
}

} // namespace

int main() {
  testTimelineAndTimestampMath();
  testCropAndCompositorMath();
  testFiltersAndAudioMixer();
  testEngineUndoRedoPersistenceAndExport();
  testMultiTrackAndBatchSelection();

  std::cout << "TRISVAX C++20 Test Results: " << gPassed << " passed, " << gFailed << " failed\n";
  return gFailed == 0 ? 0 : 1;
}
