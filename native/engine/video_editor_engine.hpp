#pragma once

#include "native/engine/command_history.hpp"
#include "native/engine/types.hpp"
#include "native/export/export_engine.hpp"
#include "native/media/media_pipeline.hpp"
#include "native/storage/project_store.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace trisvax {

class VideoEditorEngine {
public:
  explicit VideoEditorEngine(std::string workspaceRootDir);
  VideoEditorEngine(std::string workspaceRootDir, std::shared_ptr<MediaBackendFactory> backend);
  ~VideoEditorEngine();

  bool initialize();
  void release();

  void setUriAvailabilityChecker(UriAvailabilityChecker checker);

  // Project lifecycle
  Result<ProjectId> createProject(const std::string& name = "New project");
  Error openProject(const ProjectId& id);
  Error saveProject();
  std::vector<ProjectMetadata> listProjects();
  Error renameProject(const ProjectId& id, const std::string& newName);
  Error deleteProject(const ProjectId& id);
  bool hasActiveProject() const;
  Project activeProjectSnapshot() const;

  // Media & clips
  Result<ClipId> importMedia(const MediaSource& source);
  Result<ClipId> addClip(const Clip& clip);
  Error removeClip(const ClipId& id);
  Error reorderClip(size_t fromIndex, size_t toIndex);
  Result<ClipId> splitClipAtPlayhead(TimeUs playheadUs);

  // Clip edits
  Error trimClip(const ClipId& id, TimeUs startUs, TimeUs endUs);
  Error setPlaybackSpeed(const ClipId& id, double speed, AudioSpeedMode mode = AudioSpeedMode::RateCoupled);
  Error setVolume(const ClipId& id, float volume, bool muted = false);
  Error setCrop(const ClipId& id, const CropSettings& crop);
  Error setAspectRatio(AspectRatioPreset preset, int customW = 9, int customH = 16);
  Error rotateClip(const ClipId& id, int rotationDegrees);
  Error addFilter(const ClipId& id, const FilterSettings& settings);
  Error setTransition(const ClipId& id, const TransitionSpec& transition);

  // Audio & voiceover
  Error setMusicTrack(const AudioClip& music);
  Error updateMusicTrack(float volume, TimeUs trimStartUs, TimeUs trimEndUs, bool muted, bool loop, TimeUs fadeInUs, TimeUs fadeOutUs);
  Error removeMusicTrack();
  Result<VoiceoverId> addVoiceover(const VoiceoverClip& vo);
  Error updateVoiceover(const VoiceoverId& id, float volume, TimeUs trimStartUs, TimeUs trimEndUs, TimeUs timelineStartUs, bool muted);
  Error removeVoiceover(const VoiceoverId& id);

  // Text overlays
  Result<TextOverlayId> addOrUpdateTextOverlay(const TextOverlay& overlay);
  Error removeTextOverlay(const TextOverlayId& id);

  // Multi-track & multi-select batch operations
  Error setClipPlacement(const ClipId& id, TimeUs timelinePositionUs, int trackIndex);
  Error setTrackState(const TimelineTrackState& state);
  TimelineSelection marqueeSelect(
      TimeUs startUs,
      TimeUs endUs,
      bool includeVideo,
      bool includeAudio,
      bool includeText,
      int minTrackIndex = 0,
      int maxTrackIndex = 16) const;
  Error batchShiftSelection(const TimelineSelection& selection, TimeUs deltaUs, int trackDelta);
  Error batchMuteSelection(const TimelineSelection& selection, bool muted);
  Error batchDeleteSelection(const TimelineSelection& selection);

  // Playback / seeking query
  void seekTo(TimeUs playheadUs);
  TimeUs playheadUs() const;
  TimeUs totalDurationUs() const;

  // History
  bool undo();
  bool redo();
  bool canUndo() const;
  bool canRedo() const;

  // Export
  Error exportProject(const ExportSettings& settings, const ExportProgressCallback& progressCb = nullptr);
  void cancelExport();

  // Serialization helper for bridge
  std::string activeProjectJson() const;
  Error loadTimelineFromJson(const std::string& jsonStr);

private:
  mutable std::recursive_mutex mutex_;
  std::unique_ptr<ProjectStore> store_;
  std::shared_ptr<MediaBackendFactory> backend_;
  std::unique_ptr<ExportEngine> exportEngine_;
  std::optional<Project> activeProject_;
  CommandHistory history_;
  TimeUs playheadUs_ = 0;
  bool initialized_ = false;

  void commitTimelineMutation(const std::string& label, TimelineSnapshot nextState);
};

} // namespace trisvax
