#include "native/engine/video_editor_engine.hpp"

#include "native/storage/json_codec.hpp"
#include "native/timeline/timeline_engine.hpp"

#include <algorithm>

namespace trisvax {

VideoEditorEngine::VideoEditorEngine(std::string workspaceRootDir)
    : store_(std::make_unique<ProjectStore>(std::move(workspaceRootDir))),
      exportEngine_(std::make_unique<ExportEngine>()) {}

VideoEditorEngine::VideoEditorEngine(
    std::string workspaceRootDir,
    std::shared_ptr<MediaBackendFactory> backend)
    : store_(std::make_unique<ProjectStore>(std::move(workspaceRootDir))),
      backend_(std::move(backend)),
      exportEngine_(std::make_unique<ExportEngine>(backend_)) {}

VideoEditorEngine::~VideoEditorEngine() {
  release();
}

bool VideoEditorEngine::initialize() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  initialized_ = true;
  return true;
}

void VideoEditorEngine::release() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (exportEngine_) {
    exportEngine_->cancel();
  }
  activeProject_.reset();
  history_.clear();
  playheadUs_ = 0;
  initialized_ = false;
}

void VideoEditorEngine::setUriAvailabilityChecker(UriAvailabilityChecker checker) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (store_) {
    store_->setUriAvailabilityChecker(std::move(checker));
  }
}

Result<ProjectId> VideoEditorEngine::createProject(const std::string& name) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!initialized_) initialize();

  Project p;
  p.metadata.id = ProjectId{generateUuidLike("proj")};
  p.metadata.name = name.empty() ? "New project" : name;
  p.timeline.aspectRatio = AspectRatioPreset::Portrait9x16;

  Error err = store_->saveProject(p);
  if (!err.ok()) {
    return Result<ProjectId>::failure(err.code, err.message);
  }
  activeProject_ = p;
  history_.clear();
  playheadUs_ = 0;
  return Result<ProjectId>::success(p.metadata.id);
}

Error VideoEditorEngine::openProject(const ProjectId& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!initialized_) initialize();
  auto res = store_->loadProject(id);
  if (!res.ok) return res.error;
  TimelineEngine::recomputePositions(res.value.timeline.clips);
  activeProject_ = std::move(res.value);
  history_.clear();
  playheadUs_ = 0;
  return Error::success();
}

Error VideoEditorEngine::saveProject() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Error::make(ErrorCode::NotFound, "No active project open.");
  }
  return store_->saveProject(*activeProject_);
}

std::vector<ProjectMetadata> VideoEditorEngine::listProjects() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return store_->listProjects();
}

Error VideoEditorEngine::renameProject(const ProjectId& id, const std::string& newName) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (activeProject_.has_value() && activeProject_->metadata.id == id) {
    activeProject_->metadata.name = newName.empty() ? "Untitled" : newName;
  }
  return store_->renameProject(id, newName);
}

Error VideoEditorEngine::deleteProject(const ProjectId& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (activeProject_.has_value() && activeProject_->metadata.id == id) {
    activeProject_.reset();
    history_.clear();
  }
  return store_->deleteProject(id);
}

bool VideoEditorEngine::hasActiveProject() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return activeProject_.has_value();
}

Project VideoEditorEngine::activeProjectSnapshot() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return activeProject_.value_or(Project{});
}

void VideoEditorEngine::commitTimelineMutation(const std::string& label, TimelineSnapshot nextState) {
  if (!activeProject_.has_value()) return;
  TimelineEngine::recomputePositions(nextState.clips);
  TimelineSnapshot before = activeProject_->timeline;
  auto cmd = std::make_unique<SnapshotEditCommand>(label, std::move(before), nextState);
  activeProject_->timeline = std::move(nextState);
  activeProject_->metadata.durationUs = TimelineEngine::totalDurationUs(activeProject_->timeline);
  history_.pushExecuted(std::move(cmd));
  playheadUs_ = std::clamp<TimeUs>(playheadUs_, 0, activeProject_->metadata.durationUs);
}

Result<ClipId> VideoEditorEngine::importMedia(const MediaSource& source) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Result<ClipId>::failure(ErrorCode::NotFound, "No active project open.");
  }
  if (source.uri.empty()) {
    return Result<ClipId>::failure(ErrorCode::InvalidArgument, "Media URI cannot be empty.");
  }

  Clip c;
  c.id = ClipId{generateUuidLike("clip")};
  c.sourceUri = source.uri;
  c.displayName = source.displayName.empty() ? "Clip" : source.displayName;
  c.isImage = source.isImage;
  c.sourceDurationUs = source.isImage
      ? kDefaultImageDurationUs
      : std::max<TimeUs>(kMinClipDurationUs, source.durationUs);
  c.inPointUs = 0;
  c.outPointUs = c.sourceDurationUs;
  c.crop.preset = activeProject_->timeline.aspectRatio;
  c.crop.aspectWidth = activeProject_->timeline.customAspectWidth;
  c.crop.aspectHeight = activeProject_->timeline.customAspectHeight;
  c.thumbnailPath = source.thumbnailPath;
  c.mediaAvailable = true;

  TimelineSnapshot next = activeProject_->timeline;
  next.clips.push_back(c);
  commitTimelineMutation("Import media", std::move(next));
  return Result<ClipId>::success(c.id);
}

Result<ClipId> VideoEditorEngine::addClip(const Clip& clip) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Result<ClipId>::failure(ErrorCode::NotFound, "No active project open.");
  }
  Clip copy = clip;
  if (copy.id.empty()) {
    copy.id = ClipId{generateUuidLike("clip")};
  }
  TimelineSnapshot next = activeProject_->timeline;
  next.clips.push_back(copy);
  commitTimelineMutation("Add clip", std::move(next));
  return Result<ClipId>::success(copy.id);
}

Error VideoEditorEngine::removeClip(const ClipId& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::deleteClip(next.clips, id);
  if (!err.ok()) return err;
  commitTimelineMutation("Remove clip", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::reorderClip(size_t fromIndex, size_t toIndex) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::reorderClip(next.clips, fromIndex, toIndex);
  if (!err.ok()) return err;
  commitTimelineMutation("Reorder clips", std::move(next));
  return Error::success();
}

Result<ClipId> VideoEditorEngine::splitClipAtPlayhead(TimeUs playheadUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Result<ClipId>::failure(ErrorCode::NotFound, "No active project open.");
  }
  TimelineSnapshot next = activeProject_->timeline;
  ClipId secondId;
  Error err = TimelineEngine::splitClipAtPlayhead(next.clips, playheadUs, &secondId);
  if (!err.ok()) {
    return Result<ClipId>::failure(err.code, err.message);
  }
  commitTimelineMutation("Split clip", std::move(next));
  return Result<ClipId>::success(secondId);
}

Error VideoEditorEngine::trimClip(const ClipId& id, TimeUs startUs, TimeUs endUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::trimClip(next.clips, id, startUs, endUs);
  if (!err.ok()) return err;
  commitTimelineMutation("Trim clip", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setPlaybackSpeed(const ClipId& id, double speed, AudioSpeedMode mode) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipSpeed(next.clips, id, speed, mode);
  if (!err.ok()) return err;
  commitTimelineMutation("Change speed", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setVolume(const ClipId& id, float volume, bool muted) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipVolume(next.clips, id, volume, muted);
  if (!err.ok()) return err;
  commitTimelineMutation("Change volume", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setCrop(const ClipId& id, const CropSettings& crop) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipCrop(next.clips, id, crop);
  if (!err.ok()) return err;
  commitTimelineMutation("Crop clip", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setAspectRatio(AspectRatioPreset preset, int customW, int customH) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  TimelineEngine::applyAspectRatioToAll(next, preset, customW, customH);
  commitTimelineMutation("Aspect ratio", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::rotateClip(const ClipId& id, int rotationDegrees) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipRotation(next.clips, id, rotationDegrees);
  if (!err.ok()) return err;
  commitTimelineMutation("Rotate clip", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::addFilter(const ClipId& id, const FilterSettings& settings) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipFilter(next.clips, id, settings);
  if (!err.ok()) return err;
  commitTimelineMutation("Adjust filter", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setTransition(const ClipId& id, const TransitionSpec& transition) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipTransition(next.clips, id, transition);
  if (!err.ok()) return err;
  commitTimelineMutation("Set transition", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setMusicTrack(const AudioClip& music) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  AudioClip sanitized = music;
  if (sanitized.id.empty()) sanitized.id = AudioTrackId{generateUuidLike("aud")};
  sanitized.volume = std::clamp(sanitized.volume, 0.0f, 1.0f);
  sanitized.trimStartUs = std::clamp<TimeUs>(sanitized.trimStartUs, 0, std::max<TimeUs>(0, sanitized.sourceDurationUs - 500'000));
  sanitized.trimEndUs = std::clamp<TimeUs>(sanitized.trimEndUs, sanitized.trimStartUs + 500'000, std::max<TimeUs>(sanitized.trimStartUs + 500'000, sanitized.sourceDurationUs));
  next.musicTrack = sanitized;
  commitTimelineMutation("Set music", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::updateMusicTrack(
    float volume,
    TimeUs trimStartUs,
    TimeUs trimEndUs,
    bool muted,
    bool loop,
    TimeUs fadeInUs,
    TimeUs fadeOutUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value() || !activeProject_->timeline.musicTrack.has_value()) {
    return Error::make(ErrorCode::NotFound, "No music track to update.");
  }
  TimelineSnapshot next = activeProject_->timeline;
  AudioClip& m = *next.musicTrack;
  m.volume = std::clamp(volume, 0.0f, 1.0f);
  m.trimStartUs = std::clamp<TimeUs>(trimStartUs, 0, std::max<TimeUs>(0, m.sourceDurationUs - 500'000));
  m.trimEndUs = std::clamp<TimeUs>(trimEndUs, m.trimStartUs + 500'000, std::max<TimeUs>(m.trimStartUs + 500'000, m.sourceDurationUs));
  m.isMuted = muted;
  m.loopToFit = loop;
  m.fadeInUs = std::clamp<TimeUs>(fadeInUs, 0, 5'000'000);
  m.fadeOutUs = std::clamp<TimeUs>(fadeOutUs, 0, 5'000'000);
  commitTimelineMutation("Update music", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::removeMusicTrack() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  next.musicTrack.reset();
  commitTimelineMutation("Remove music", std::move(next));
  return Error::success();
}

Result<VoiceoverId> VideoEditorEngine::addVoiceover(const VoiceoverClip& vo) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Result<VoiceoverId>::failure(ErrorCode::NotFound, "No active project open.");
  }
  VoiceoverClip item = vo;
  if (item.id.empty()) item.id = VoiceoverId{generateUuidLike("vo")};
  item.volume = std::clamp(item.volume, 0.0f, 1.0f);
  item.trimStartUs = std::max<TimeUs>(0, item.trimStartUs);
  item.trimEndUs = std::max<TimeUs>(item.trimStartUs + kMinTrimSpanUs, item.trimEndUs);
  TimelineSnapshot next = activeProject_->timeline;
  next.voiceovers.push_back(item);
  commitTimelineMutation("Add voiceover", std::move(next));
  return Result<VoiceoverId>::success(item.id);
}

Error VideoEditorEngine::updateVoiceover(
    const VoiceoverId& id,
    float volume,
    TimeUs trimStartUs,
    TimeUs trimEndUs,
    TimeUs timelineStartUs,
    bool muted) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  auto it = std::find_if(next.voiceovers.begin(), next.voiceovers.end(), [&](const VoiceoverClip& v) {
    return v.id == id;
  });
  if (it == next.voiceovers.end()) {
    return Error::make(ErrorCode::NotFound, "Voiceover not found.");
  }
  it->volume = std::clamp(volume, 0.0f, 1.0f);
  it->trimStartUs = std::clamp<TimeUs>(trimStartUs, 0, std::max<TimeUs>(0, it->sourceDurationUs - kMinTrimSpanUs));
  it->trimEndUs = std::clamp<TimeUs>(trimEndUs, it->trimStartUs + kMinTrimSpanUs, std::max<TimeUs>(it->trimStartUs + kMinTrimSpanUs, it->sourceDurationUs));
  it->timelineStartUs = std::max<TimeUs>(0, timelineStartUs);
  it->isMuted = muted;
  commitTimelineMutation("Update voiceover", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::removeVoiceover(const VoiceoverId& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  const size_t before = next.voiceovers.size();
  next.voiceovers.erase(
      std::remove_if(next.voiceovers.begin(), next.voiceovers.end(), [&](const VoiceoverClip& v) {
        return v.id == id;
      }),
      next.voiceovers.end());
  if (next.voiceovers.size() == before) {
    return Error::make(ErrorCode::NotFound, "Voiceover not found.");
  }
  commitTimelineMutation("Remove voiceover", std::move(next));
  return Error::success();
}

Result<TextOverlayId> VideoEditorEngine::addOrUpdateTextOverlay(const TextOverlay& overlay) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Result<TextOverlayId>::failure(ErrorCode::NotFound, "No active project open.");
  }
  if (overlay.text.empty()) {
    return Result<TextOverlayId>::failure(ErrorCode::InvalidArgument, "Text overlay cannot be empty.");
  }
  TextOverlay item = overlay;
  if (item.id.empty()) item.id = TextOverlayId{generateUuidLike("txt")};
  item.fontSizeSp = std::clamp(item.fontSizeSp, 12.0f, 72.0f);
  item.normalizedX = std::clamp(item.normalizedX, 0.05f, 0.95f);
  item.normalizedY = std::clamp(item.normalizedY, 0.05f, 0.95f);
  item.startUs = std::max<TimeUs>(0, item.startUs);
  item.endUs = std::max<TimeUs>(item.startUs + 300'000, item.endUs);

  TimelineSnapshot next = activeProject_->timeline;
  auto it = std::find_if(next.textOverlays.begin(), next.textOverlays.end(), [&](const TextOverlay& t) {
    return t.id == item.id;
  });
  if (it != next.textOverlays.end()) {
    *it = item;
  } else {
    next.textOverlays.push_back(item);
  }
  commitTimelineMutation("Edit text overlay", std::move(next));
  return Result<TextOverlayId>::success(item.id);
}

Error VideoEditorEngine::removeTextOverlay(const TextOverlayId& id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  const size_t before = next.textOverlays.size();
  next.textOverlays.erase(
      std::remove_if(next.textOverlays.begin(), next.textOverlays.end(), [&](const TextOverlay& t) {
        return t.id == id;
      }),
      next.textOverlays.end());
  if (next.textOverlays.size() == before) {
    return Error::make(ErrorCode::NotFound, "Text overlay not found.");
  }
  commitTimelineMutation("Remove text overlay", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setClipPlacement(const ClipId& id, TimeUs timelinePositionUs, int trackIndex) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::setClipPlacement(next.clips, id, timelinePositionUs, trackIndex);
  if (!err.ok()) return err;
  commitTimelineMutation("Move clip track/position", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::setTrackState(const TimelineTrackState& state) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  auto it = std::find_if(next.trackStates.begin(), next.trackStates.end(), [&](const TimelineTrackState& st) {
    return st.kind == state.kind && st.trackIndex == state.trackIndex;
  });
  if (it != next.trackStates.end()) {
    *it = state;
  } else {
    next.trackStates.push_back(state);
  }
  commitTimelineMutation("Update track state", std::move(next));
  return Error::success();
}

TimelineSelection VideoEditorEngine::marqueeSelect(
    TimeUs startUs,
    TimeUs endUs,
    bool includeVideo,
    bool includeAudio,
    bool includeText,
    int minTrackIndex,
    int maxTrackIndex) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return {};
  return TimelineEngine::marqueeSelect(
      activeProject_->timeline, startUs, endUs, includeVideo, includeAudio, includeText, minTrackIndex, maxTrackIndex);
}

Error VideoEditorEngine::batchShiftSelection(
    const TimelineSelection& selection,
    TimeUs deltaUs,
    int trackDelta) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::batchShiftSelection(next, selection, deltaUs, trackDelta);
  if (!err.ok()) return err;
  commitTimelineMutation("Shift selection", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::batchMuteSelection(const TimelineSelection& selection, bool muted) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::batchMuteSelection(next, selection, muted);
  if (!err.ok()) return err;
  commitTimelineMutation(muted ? "Mute selection" : "Unmute selection", std::move(next));
  return Error::success();
}

Error VideoEditorEngine::batchDeleteSelection(const TimelineSelection& selection) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return Error::make(ErrorCode::NotFound, "No active project open.");
  TimelineSnapshot next = activeProject_->timeline;
  Error err = TimelineEngine::batchDeleteSelection(next, selection);
  if (!err.ok()) return err;
  commitTimelineMutation("Delete selection", std::move(next));
  return Error::success();
}

void VideoEditorEngine::seekTo(TimeUs playheadUs) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const TimeUs maxDur = totalDurationUs();
  playheadUs_ = std::clamp<TimeUs>(playheadUs, 0, maxDur);
}

TimeUs VideoEditorEngine::playheadUs() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return playheadUs_;
}

TimeUs VideoEditorEngine::totalDurationUs() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return 0;
  return TimelineEngine::totalDurationUs(activeProject_->timeline);
}

bool VideoEditorEngine::undo() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return false;
  if (!history_.undo(activeProject_->timeline)) return false;
  TimelineEngine::recomputePositions(activeProject_->timeline.clips);
  activeProject_->metadata.durationUs = TimelineEngine::totalDurationUs(activeProject_->timeline);
  playheadUs_ = std::clamp<TimeUs>(playheadUs_, 0, activeProject_->metadata.durationUs);
  return true;
}

bool VideoEditorEngine::redo() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return false;
  if (!history_.redo(activeProject_->timeline)) return false;
  TimelineEngine::recomputePositions(activeProject_->timeline.clips);
  activeProject_->metadata.durationUs = TimelineEngine::totalDurationUs(activeProject_->timeline);
  playheadUs_ = std::clamp<TimeUs>(playheadUs_, 0, activeProject_->metadata.durationUs);
  return true;
}

bool VideoEditorEngine::canUndo() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return history_.canUndo();
}

bool VideoEditorEngine::canRedo() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return history_.canRedo();
}

Error VideoEditorEngine::exportProject(
    const ExportSettings& settings,
    const ExportProgressCallback& progressCb) {
  Project snapshot;
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!activeProject_.has_value()) {
      return Error::make(ErrorCode::NotFound, "No active project open.");
    }
    snapshot = *activeProject_;
  }
  Error err = exportEngine_->exportProject(snapshot, settings, progressCb);
  if (err.ok()) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (activeProject_.has_value()) {
      activeProject_->metadata.exportedPath = settings.outputPath;
      store_->saveProject(*activeProject_);
    }
  }
  return err;
}

void VideoEditorEngine::cancelExport() {
  if (exportEngine_) {
    exportEngine_->cancel();
  }
}

std::string VideoEditorEngine::activeProjectJson() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) return "{}";
  return ProjectJsonCodec::serializeProject(*activeProject_);
}

Error VideoEditorEngine::loadTimelineFromJson(const std::string& jsonStr) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!activeProject_.has_value()) {
    return Error::make(ErrorCode::NotFound, "No active project open.");
  }
  auto parsed = JsonValue::parse(jsonStr);
  if (!parsed.ok) return parsed.error;
  TimelineSnapshot tl = ProjectJsonCodec::deserializeTimeline(parsed.value);
  commitTimelineMutation("Load timeline", std::move(tl));
  return Error::success();
}

} // namespace trisvax
