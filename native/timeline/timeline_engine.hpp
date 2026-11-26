#pragma once

#include "native/engine/types.hpp"

#include <optional>
#include <vector>

namespace trisvax {

struct PlayheadMatch {
  size_t clipIndex = 0;
  ClipId clipId;
  TimeUs localSourceUs = 0;
  TimeUs clipOffsetUs = 0;
};

class TimelineEngine {
public:
  static TimeUs clipEffectiveDurationUs(const Clip& clip) noexcept;
  static TimeUs totalDurationUs(const TimelineSnapshot& timeline) noexcept;
  static void recomputePositions(std::vector<Clip>& clips) noexcept;

  static Error trimClip(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      TimeUs newInUs,
      TimeUs newOutUs);

  static Error splitClipAtPlayhead(
      std::vector<Clip>& clips,
      TimeUs playheadUs,
      ClipId* outSecondClipId = nullptr);

  static Error setClipSpeed(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      double speed,
      AudioSpeedMode mode);

  static Error setClipVolume(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      float volume,
      bool muted);

  static Error setClipRotation(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      int rotationDegrees);

  static Error setClipCrop(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      const CropSettings& crop);

  static void applyAspectRatioToAll(
      TimelineSnapshot& timeline,
      AspectRatioPreset preset,
      int customW = 9,
      int customH = 16);

  static Error setClipFilter(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      const FilterSettings& filter);

  static Error setClipTransition(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      const TransitionSpec& transition);

  static Error reorderClip(
      std::vector<Clip>& clips,
      size_t fromIndex,
      size_t toIndex);

  static Error deleteClip(
      std::vector<Clip>& clips,
      const ClipId& clipId);

  static Error setClipPlacement(
      std::vector<Clip>& clips,
      const ClipId& clipId,
      TimeUs timelinePositionUs,
      int trackIndex);

  static bool isTrackLocked(
      const TimelineSnapshot& timeline,
      TrackKind kind,
      int trackIndex) noexcept;

  static TimelineSelection marqueeSelect(
      const TimelineSnapshot& timeline,
      TimeUs startUs,
      TimeUs endUs,
      bool includeVideo,
      bool includeAudio,
      bool includeText,
      int minTrackIndex = 0,
      int maxTrackIndex = 16) noexcept;

  static Error batchShiftSelection(
      TimelineSnapshot& timeline,
      const TimelineSelection& selection,
      TimeUs deltaUs,
      int trackDelta);

  static Error batchMuteSelection(
      TimelineSnapshot& timeline,
      const TimelineSelection& selection,
      bool muted);

  static Error batchDeleteSelection(
      TimelineSnapshot& timeline,
      const TimelineSelection& selection);

  static std::optional<PlayheadMatch> findClipAtPlayhead(
      const std::vector<Clip>& clips,
      TimeUs playheadUs) noexcept;
};

} // namespace trisvax

