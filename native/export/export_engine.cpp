#include "native/export/export_engine.hpp"

#include "native/audio/audio_mixer.hpp"
#include "native/filters/filter_pipeline.hpp"
#include "native/rendering/compositor_math.hpp"
#include "native/timeline/timeline_engine.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace trisvax {

ExportEngine::ExportEngine(std::shared_ptr<MediaBackendFactory> backend)
    : backend_(std::move(backend)) {}

ExportSettings ExportEngine::normalizeSettings(const Project& project, const ExportSettings& requested) {
  ExportSettings out = requested;
  const auto dims = CompositorMath::computeTargetDimensions(
      project.timeline.aspectRatio,
      project.timeline.customAspectWidth,
      project.timeline.customAspectHeight);
  out.width = dims.width;
  out.height = dims.height;
  out.fps = std::clamp(requested.fps > 0 ? requested.fps : 30, 24, 60);
  out.videoBitrateBps = requested.videoBitrateBps > 0 ? requested.videoBitrateBps : 12'000'000;
  out.audioSampleRate = requested.audioSampleRate > 0 ? requested.audioSampleRate : 44'100;
  out.audioBitrateBps = requested.audioBitrateBps > 0 ? requested.audioBitrateBps : 160'000;
  out.watermarkEnabled = false;
  return out;
}

Error ExportEngine::validateProjectForExport(const Project& project, const ExportSettings& settings) {
  if (project.timeline.clips.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Add at least one clip before exporting.");
  }
  if (settings.outputPath.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Export output path is required.");
  }
  for (const auto& clip : project.timeline.clips) {
    if (!clip.mediaAvailable) {
      return Error::make(ErrorCode::NotFound, "Source media missing for clip: " + clip.displayName);
    }
  }
  const TimeUs totalUs = TimelineEngine::totalDurationUs(project.timeline);
  if (totalUs < kMinClipDurationUs) {
    return Error::make(ErrorCode::InvalidArgument, "Timeline duration is too short to export.");
  }
  if (settings.width < 240 || settings.height < 240 || (settings.width % 2) != 0 || (settings.height % 2) != 0) {
    return Error::make(ErrorCode::InvalidArgument, "Export resolution must use even dimensions >= 240px.");
  }
  return Error::success();
}

int64_t ExportEngine::computeTotalFrames(TimeUs durationUs, int fps) noexcept {
  if (durationUs <= 0 || fps <= 0) return 0;
  return std::max<int64_t>(1, static_cast<int64_t>(std::ceil(
      (static_cast<double>(durationUs) / 1'000'000.0) * static_cast<double>(fps))));
}

void ExportEngine::cancel() noexcept {
  cancelRequested_.store(true, std::memory_order_release);
}

bool ExportEngine::isCancelled() const noexcept {
  return cancelRequested_.load(std::memory_order_acquire);
}

Error ExportEngine::exportProject(
    const Project& project,
    const ExportSettings& requested,
    const ExportProgressCallback& progressCb) {
  cancelRequested_.store(false, std::memory_order_release);
  auto emit = [&](ExportStage stage, int pct, const std::string& msg, const Error& err = {}) {
    if (progressCb) {
      ExportProgress p;
      p.stage = stage;
      p.percent = std::clamp(pct, 0, 100);
      p.statusMessage = msg;
      p.outputPath = requested.outputPath;
      p.error = err;
      progressCb(p);
    }
  };

  const ExportSettings settings = normalizeSettings(project, requested);
  emit(ExportStage::Validating, 1, "Validating timeline and storage...");

  Error valErr = validateProjectForExport(project, settings);
  if (!valErr.ok()) {
    emit(ExportStage::Failed, 0, valErr.message, valErr);
    return valErr;
  }

  if (!backend_) {
    Error err = Error::make(ErrorCode::Unsupported, "No platform media encoder backend attached.");
    emit(ExportStage::Failed, 0, err.message, err);
    return err;
  }

  const int64_t freeBytes = backend_->availableStorageBytes(settings.outputPath);
  if (freeBytes >= 0 && freeBytes < kMinRequiredFreeBytes) {
    Error err = Error::make(ErrorCode::InsufficientStorage, "Insufficient device storage for 1080p MP4 export.");
    emit(ExportStage::Failed, 0, err.message, err);
    return err;
  }

  emit(ExportStage::Preparing, 5, "Initializing 1080p H.264/AAC pipeline...");
  auto encoder = backend_->createEncoder();
  if (!encoder) {
    Error err = Error::make(ErrorCode::CodecError, "Unable to create media encoder.");
    emit(ExportStage::Failed, 0, err.message, err);
    return err;
  }

  const std::string tempPath = settings.outputPath + ".tmp.mp4";
  ExportSettings tempSettings = settings;
  tempSettings.outputPath = tempPath;

  Error openErr = encoder->open(tempSettings);
  if (!openErr.ok()) {
    emit(ExportStage::Failed, 0, openErr.message, openErr);
    return openErr;
  }

  const TimeUs totalDurationUs = TimelineEngine::totalDurationUs(project.timeline);
  const int64_t totalFrames = computeTotalFrames(totalDurationUs, settings.fps);
  const TimeUs frameStepUs = 1'000'000 / settings.fps;

  auto decoder = backend_->createDecoder();
  std::string openSourceUri;
  std::vector<uint8_t> frameBuffer(static_cast<size_t>(settings.width) * static_cast<size_t>(settings.height) * 4, 0);

  for (int64_t f = 0; f < totalFrames; ++f) {
    if (isCancelled()) {
      encoder->abort();
      std::error_code ec;
      fs::remove(tempPath, ec);
      Error cerr = Error::make(ErrorCode::Cancelled, "Export cancelled by user.");
      emit(ExportStage::Cancelled, 0, cerr.message, cerr);
      return cerr;
    }

    const TimeUs timelineUs = f * frameStepUs;
    std::vector<Clip> visibleClips;
    visibleClips.reserve(project.timeline.clips.size());
    for (const auto& c : project.timeline.clips) {
      bool isHidden = false;
      for (const auto& ts : project.timeline.trackStates) {
        if (ts.kind == TrackKind::Video && ts.trackIndex == c.trackIndex && ts.hidden) {
          isHidden = true;
          break;
        }
      }
      if (!isHidden) visibleClips.push_back(c);
    }
    auto match = TimelineEngine::findClipAtPlayhead(visibleClips, timelineUs);
    if (match.has_value() && decoder) {
      const Clip& clip = visibleClips[match->clipIndex];
      if (openSourceUri != clip.sourceUri) {
        decoder->close();
        if (decoder->open(clip.sourceUri).ok()) {
          openSourceUri = clip.sourceUri;
        }
      }
      if (decoder->seek(match->localSourceUs).ok()) {
        DecodedVideoFrame decoded;
        if (decoder->readNextVideoFrame(decoded) && decoded.valid) {
          // Copy/apply filter on decoded buffer if matching dimensions
          const size_t copyBytes = std::min(frameBuffer.size(), decoded.rgbaPixels.size());
          std::copy_n(decoded.rgbaPixels.data(), copyBytes, frameBuffer.data());
          FilterPipeline::applyRgba8888(frameBuffer.data(), settings.width, settings.height, settings.width * 4, clip.filter);
        }
      }
    }

    Error encErr = encoder->encodeVideoFrameRgba(frameBuffer.data(), settings.width, settings.height, timelineUs);
    if (!encErr.ok()) {
      encoder->abort();
      std::error_code ec;
      fs::remove(tempPath, ec);
      emit(ExportStage::Failed, 0, encErr.message, encErr);
      return encErr;
    }

    const int pct = 8 + static_cast<int>((f * 84) / std::max<int64_t>(1, totalFrames));
    if (f % std::max<int64_t>(1, settings.fps / 2) == 0 || f + 1 == totalFrames) {
      emit(ExportStage::Rendering, pct, "Rendering 1080p MP4...");
    }
  }

  emit(ExportStage::Muxing, 95, "Finalizing MP4 container...");
  Error finErr = encoder->finish();
  if (!finErr.ok()) {
    std::error_code ec;
    fs::remove(tempPath, ec);
    emit(ExportStage::Failed, 0, finErr.message, finErr);
    return finErr;
  }

  std::error_code ec;
  fs::rename(tempPath, settings.outputPath, ec);
  if (ec) {
    fs::remove(tempPath, ec);
    Error ioErr = Error::make(ErrorCode::IoError, "Failed to save final MP4 file: " + ec.message());
    emit(ExportStage::Failed, 0, ioErr.message, ioErr);
    return ioErr;
  }

  emit(ExportStage::Completed, 100, "Export saved.");
  return Error::success();
}

} // namespace trisvax
