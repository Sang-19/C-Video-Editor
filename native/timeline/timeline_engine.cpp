#include "native/timeline/timeline_engine.hpp"

#include <algorithm>
#include <cmath>

namespace trisvax {

TimeUs TimelineEngine::clipEffectiveDurationUs(const Clip& clip) noexcept {
  const TimeUs rawSpan = std::max<TimeUs>(kMinClipDurationUs, clip.outPointUs - clip.inPointUs);
  const double speed = std::clamp(clip.playbackSpeed, 0.25, 4.0);
  return static_cast<TimeUs>(std::llround(static_cast<double>(rawSpan) / speed));
}

TimeUs TimelineEngine::totalDurationUs(const TimelineSnapshot& timeline) noexcept {
  TimeUs maxEnd = 0;
  for (const auto& clip : timeline.clips) {
    maxEnd = std::max<TimeUs>(maxEnd, clip.timelinePositionUs + clipEffectiveDurationUs(clip));
  }
  for (const auto& vo : timeline.voiceovers) {
    const TimeUs span = std::max<TimeUs>(0, vo.trimEndUs - vo.trimStartUs);
    maxEnd = std::max<TimeUs>(maxEnd, vo.timelineStartUs + span);
  }
  for (const auto& txt : timeline.textOverlays) {
    maxEnd = std::max<TimeUs>(maxEnd, txt.endUs);
  }
  return maxEnd;
}

void TimelineEngine::recomputePositions(std::vector<Clip>& clips) noexcept {
  TimeUs cursor = 0;
  for (auto& clip : clips) {
    if (clip.trackIndex <= 0) {
      clip.trackIndex = 0;
      clip.timelinePositionUs = cursor;
      cursor += clipEffectiveDurationUs(clip);
    } else {
      clip.timelinePositionUs = std::max<TimeUs>(0, clip.timelinePositionUs);
    }
  }
}

Error TimelineEngine::trimClip(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    TimeUs newInUs,
    TimeUs newOutUs) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for trim.");
  }

  const TimeUs maxSource = std::max<TimeUs>(kMinTrimSpanUs, it->sourceDurationUs);
  const TimeUs clampedIn = std::clamp<TimeUs>(newInUs, 0, maxSource - kMinTrimSpanUs);
  const TimeUs clampedOut = std::clamp<TimeUs>(newOutUs, clampedIn + kMinTrimSpanUs, maxSource);

  it->inPointUs = clampedIn;
  it->outPointUs = clampedOut;
  recomputePositions(clips);
  return Error::success();
}

Error TimelineEngine::splitClipAtPlayhead(
    std::vector<Clip>& clips,
    TimeUs playheadUs,
    ClipId* outSecondClipId) {
  recomputePositions(clips);

  size_t targetIdx = clips.size();
  for (size_t i = 0; i < clips.size(); ++i) {
    const TimeUs eff = clipEffectiveDurationUs(clips[i]);
    if (playheadUs > clips[i].timelinePositionUs + kMinClipDurationUs &&
        playheadUs < clips[i].timelinePositionUs + eff - kMinClipDurationUs) {
      targetIdx = i;
      break;
    }
  }

  if (targetIdx == clips.size()) {
    return Error::make(ErrorCode::InvalidArgument, "Playhead is too close to clip boundary to split.");
  }

  Clip target = clips[targetIdx];
  const TimeUs timelineOffset = playheadUs - target.timelinePositionUs;
  const double speed = std::clamp(target.playbackSpeed, 0.25, 4.0);
  const TimeUs sourceSplitUs = target.inPointUs +
      static_cast<TimeUs>(std::llround(static_cast<double>(timelineOffset) * speed));

  if (sourceSplitUs <= target.inPointUs + kMinClipDurationUs ||
      sourceSplitUs >= target.outPointUs - kMinClipDurationUs) {
    return Error::make(ErrorCode::InvalidArgument, "Split point violates minimum clip span.");
  }

  Clip firstHalf = target;
  firstHalf.outPointUs = sourceSplitUs;

  Clip secondHalf = target;
  secondHalf.id = ClipId{generateUuidLike("clip")};
  secondHalf.inPointUs = sourceSplitUs;
  secondHalf.transitionIn = TransitionSpec{TransitionType::Cut, 0};

  if (outSecondClipId != nullptr) {
    *outSecondClipId = secondHalf.id;
  }

  clips[targetIdx] = std::move(firstHalf);
  clips.insert(clips.begin() + static_cast<ptrdiff_t>(targetIdx + 1), std::move(secondHalf));
  recomputePositions(clips);
  return Error::success();
}

Error TimelineEngine::setClipSpeed(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    double speed,
    AudioSpeedMode mode) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for speed change.");
  }
  it->playbackSpeed = std::clamp(speed, 0.25, 4.0);
  it->audioSpeedMode = mode;
  recomputePositions(clips);
  return Error::success();
}

Error TimelineEngine::setClipVolume(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    float volume,
    bool muted) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for volume change.");
  }
  it->volume = std::clamp(volume, 0.0f, 2.0f);
  it->isMuted = muted;
  return Error::success();
}

Error TimelineEngine::setClipRotation(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    int rotationDegrees) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for rotation.");
  }
  int normalized = ((rotationDegrees % 360) + 360) % 360;
  it->crop.rotationDegrees = normalized;
  return Error::success();
}

Error TimelineEngine::setClipCrop(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    const CropSettings& crop) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for crop.");
  }
  CropSettings sanitized = crop;
  sanitized.zoom = std::clamp(sanitized.zoom, 1.0f, 3.0f);
  const float maxOffset = std::max(0.0f, 1.0f - (1.0f / sanitized.zoom));
  sanitized.offsetX = std::clamp(sanitized.offsetX, -maxOffset, maxOffset);
  sanitized.offsetY = std::clamp(sanitized.offsetY, -maxOffset, maxOffset);
  sanitized.rotationDegrees = ((sanitized.rotationDegrees % 360) + 360) % 360;
  sanitized.aspectWidth = std::max(1, sanitized.aspectWidth);
  sanitized.aspectHeight = std::max(1, sanitized.aspectHeight);
  it->crop = sanitized;
  return Error::success();
}

void TimelineEngine::applyAspectRatioToAll(
    TimelineSnapshot& timeline,
    AspectRatioPreset preset,
    int customW,
    int customH) {
  timeline.aspectRatio = preset;
  switch (preset) {
    case AspectRatioPreset::Portrait9x16:
      timeline.customAspectWidth = 9;
      timeline.customAspectHeight = 16;
      break;
    case AspectRatioPreset::Landscape16x9:
      timeline.customAspectWidth = 16;
      timeline.customAspectHeight = 9;
      break;
    case AspectRatioPreset::Square1x1:
      timeline.customAspectWidth = 1;
      timeline.customAspectHeight = 1;
      break;
    case AspectRatioPreset::Freeform:
      timeline.customAspectWidth = std::max(1, customW);
      timeline.customAspectHeight = std::max(1, customH);
      break;
  }
  for (auto& clip : timeline.clips) {
    clip.crop.preset = preset;
    clip.crop.aspectWidth = timeline.customAspectWidth;
    clip.crop.aspectHeight = timeline.customAspectHeight;
  }
}

Error TimelineEngine::setClipFilter(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    const FilterSettings& filter) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for filter.");
  }
  FilterSettings clamped = filter;
  clamped.brightness = std::clamp(clamped.brightness, -1.0f, 1.0f);
  clamped.contrast = std::clamp(clamped.contrast, 0.5f, 2.0f);
  clamped.saturation = std::clamp(clamped.saturation, 0.0f, 2.0f);
  clamped.temperature = std::clamp(clamped.temperature, -1.0f, 1.0f);
  clamped.exposure = std::clamp(clamped.exposure, -1.0f, 1.0f);
  clamped.sharpness = std::clamp(clamped.sharpness, 0.0f, 1.0f);
  it->filter = clamped;
  return Error::success();
}

Error TimelineEngine::setClipTransition(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    const TransitionSpec& transition) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for transition.");
  }
  TransitionSpec spec = transition;
  if (spec.type == TransitionType::Cut) {
    spec.durationUs = 0;
  } else {
    const TimeUs maxDur = std::max<TimeUs>(100'000, clipEffectiveDurationUs(*it) / 2);
    spec.durationUs = std::clamp<TimeUs>(spec.durationUs, 100'000, std::min<TimeUs>(2'000'000, maxDur));
  }
  it->transitionIn = spec;
  return Error::success();
}

Error TimelineEngine::reorderClip(
    std::vector<Clip>& clips,
    size_t fromIndex,
    size_t toIndex) {
  if (fromIndex >= clips.size() || toIndex >= clips.size()) {
    return Error::make(ErrorCode::InvalidArgument, "Clip reorder index out of range.");
  }
  if (fromIndex == toIndex) {
    return Error::success();
  }
  Clip moved = std::move(clips[fromIndex]);
  clips.erase(clips.begin() + static_cast<ptrdiff_t>(fromIndex));
  clips.insert(clips.begin() + static_cast<ptrdiff_t>(toIndex), std::move(moved));
  recomputePositions(clips);
  return Error::success();
}

Error TimelineEngine::deleteClip(
    std::vector<Clip>& clips,
    const ClipId& clipId) {
  if (clips.size() <= 1) {
    return Error::make(ErrorCode::InvalidArgument, "Cannot delete the last remaining clip.");
  }
  const auto oldSize = clips.size();
  clips.erase(
      std::remove_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; }),
      clips.end());
  if (clips.size() == oldSize) {
    return Error::make(ErrorCode::NotFound, "Clip not found for deletion.");
  }
  recomputePositions(clips);
  return Error::success();
}

Error TimelineEngine::setClipPlacement(
    std::vector<Clip>& clips,
    const ClipId& clipId,
    TimeUs timelinePositionUs,
    int trackIndex) {
  auto it = std::find_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id == clipId; });
  if (it == clips.end()) {
    return Error::make(ErrorCode::NotFound, "Clip not found for placement.");
  }
  it->trackIndex = std::clamp(trackIndex, 0, 15);
  it->timelinePositionUs = std::max<TimeUs>(0, timelinePositionUs);
  return Error::success();
}

bool TimelineEngine::isTrackLocked(
    const TimelineSnapshot& timeline,
    TrackKind kind,
    int trackIndex) noexcept {
  for (const auto& st : timeline.trackStates) {
    if (st.kind == kind && st.trackIndex == trackIndex) {
      return st.locked;
    }
  }
  return false;
}

TimelineSelection TimelineEngine::marqueeSelect(
    const TimelineSnapshot& timeline,
    TimeUs startUs,
    TimeUs endUs,
    bool includeVideo,
    bool includeAudio,
    bool includeText,
    int minTrackIndex,
    int maxTrackIndex) noexcept {
  TimelineSelection sel;
  const TimeUs lo = std::min(startUs, endUs);
  const TimeUs hi = std::max(startUs, endUs);
  const auto overlaps = [&](TimeUs aStart, TimeUs aEnd) {
    return aEnd >= lo && aStart <= hi;
  };

  if (includeVideo) {
    for (const auto& c : timeline.clips) {
      if (c.trackIndex < minTrackIndex || c.trackIndex > maxTrackIndex) continue;
      if (isTrackLocked(timeline, TrackKind::Video, c.trackIndex)) continue;
      const TimeUs cStart = c.timelinePositionUs;
      const TimeUs cEnd = cStart + clipEffectiveDurationUs(c);
      if (overlaps(cStart, cEnd)) {
        sel.clipIds.push_back(c.id);
      }
    }
  }

  if (includeAudio) {
    if (timeline.musicTrack.has_value()) {
      const auto& m = *timeline.musicTrack;
      if (m.trackIndex >= minTrackIndex && m.trackIndex <= maxTrackIndex &&
          !isTrackLocked(timeline, TrackKind::Audio, m.trackIndex)) {
        const TimeUs mSpan = m.loopToFit
            ? std::max<TimeUs>(m.trimEndUs - m.trimStartUs, totalDurationUs(timeline))
            : std::max<TimeUs>(0, m.trimEndUs - m.trimStartUs);
        if (overlaps(m.timelineStartUs, m.timelineStartUs + mSpan)) {
          sel.audioIds.push_back(m.id);
        }
      }
    }
    for (const auto& v : timeline.voiceovers) {
      if (v.trackIndex < minTrackIndex || v.trackIndex > maxTrackIndex) continue;
      if (isTrackLocked(timeline, TrackKind::Audio, v.trackIndex)) continue;
      const TimeUs vSpan = std::max<TimeUs>(0, v.trimEndUs - v.trimStartUs);
      if (overlaps(v.timelineStartUs, v.timelineStartUs + vSpan)) {
        sel.voiceoverIds.push_back(v.id);
      }
    }
  }

  if (includeText) {
    for (const auto& t : timeline.textOverlays) {
      if (t.trackIndex < minTrackIndex || t.trackIndex > maxTrackIndex) continue;
      if (isTrackLocked(timeline, TrackKind::Text, t.trackIndex)) continue;
      if (overlaps(t.startUs, t.endUs)) {
        sel.textIds.push_back(t.id);
      }
    }
  }

  return sel;
}

Error TimelineEngine::batchShiftSelection(
    TimelineSnapshot& timeline,
    const TimelineSelection& selection,
    TimeUs deltaUs,
    int trackDelta) {
  if (selection.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Selection is empty.");
  }

  const auto contains = [](const auto& vec, const auto& id) {
    return std::find(vec.begin(), vec.end(), id) != vec.end();
  };

  for (auto& c : timeline.clips) {
    if (!contains(selection.clipIds, c.id)) continue;
    if (isTrackLocked(timeline, TrackKind::Video, c.trackIndex)) continue;
    c.timelinePositionUs = std::max<TimeUs>(0, c.timelinePositionUs + deltaUs);
    c.trackIndex = std::clamp(c.trackIndex + trackDelta, 0, 15);
  }

  if (timeline.musicTrack.has_value() && contains(selection.audioIds, timeline.musicTrack->id)) {
    if (!isTrackLocked(timeline, TrackKind::Audio, timeline.musicTrack->trackIndex)) {
      timeline.musicTrack->timelineStartUs = std::max<TimeUs>(0, timeline.musicTrack->timelineStartUs + deltaUs);
      timeline.musicTrack->trackIndex = std::clamp(timeline.musicTrack->trackIndex + trackDelta, 0, 15);
    }
  }

  for (auto& v : timeline.voiceovers) {
    if (!contains(selection.voiceoverIds, v.id)) continue;
    if (isTrackLocked(timeline, TrackKind::Audio, v.trackIndex)) continue;
    v.timelineStartUs = std::max<TimeUs>(0, v.timelineStartUs + deltaUs);
    v.trackIndex = std::clamp(v.trackIndex + trackDelta, 0, 15);
  }

  for (auto& t : timeline.textOverlays) {
    if (!contains(selection.textIds, t.id)) continue;
    if (isTrackLocked(timeline, TrackKind::Text, t.trackIndex)) continue;
    const TimeUs dur = std::max<TimeUs>(300'000, t.endUs - t.startUs);
    t.startUs = std::max<TimeUs>(0, t.startUs + deltaUs);
    t.endUs = t.startUs + dur;
    t.trackIndex = std::clamp(t.trackIndex + trackDelta, 0, 15);
  }

  return Error::success();
}

Error TimelineEngine::batchMuteSelection(
    TimelineSnapshot& timeline,
    const TimelineSelection& selection,
    bool muted) {
  if (selection.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Selection is empty.");
  }
  const auto contains = [](const auto& vec, const auto& id) {
    return std::find(vec.begin(), vec.end(), id) != vec.end();
  };

  for (auto& c : timeline.clips) {
    if (contains(selection.clipIds, c.id) && !isTrackLocked(timeline, TrackKind::Video, c.trackIndex)) {
      c.isMuted = muted;
    }
  }
  if (timeline.musicTrack.has_value() && contains(selection.audioIds, timeline.musicTrack->id)) {
    if (!isTrackLocked(timeline, TrackKind::Audio, timeline.musicTrack->trackIndex)) {
      timeline.musicTrack->isMuted = muted;
    }
  }
  for (auto& v : timeline.voiceovers) {
    if (contains(selection.voiceoverIds, v.id) && !isTrackLocked(timeline, TrackKind::Audio, v.trackIndex)) {
      v.isMuted = muted;
    }
  }
  return Error::success();
}

Error TimelineEngine::batchDeleteSelection(
    TimelineSnapshot& timeline,
    const TimelineSelection& selection) {
  if (selection.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Selection is empty.");
  }
  const auto contains = [](const auto& vec, const auto& id) {
    return std::find(vec.begin(), vec.end(), id) != vec.end();
  };

  if (!selection.clipIds.empty() && timeline.clips.size() > 1) {
    std::vector<Clip> kept;
    kept.reserve(timeline.clips.size());
    for (const auto& c : timeline.clips) {
      if (contains(selection.clipIds, c.id) && !isTrackLocked(timeline, TrackKind::Video, c.trackIndex)) {
        continue;
      }
      kept.push_back(c);
    }
    if (kept.empty()) {
      kept.push_back(timeline.clips.front());
    }
    timeline.clips = std::move(kept);
    recomputePositions(timeline.clips);
  }

  if (timeline.musicTrack.has_value() && contains(selection.audioIds, timeline.musicTrack->id)) {
    if (!isTrackLocked(timeline, TrackKind::Audio, timeline.musicTrack->trackIndex)) {
      timeline.musicTrack.reset();
    }
  }

  timeline.voiceovers.erase(
      std::remove_if(timeline.voiceovers.begin(), timeline.voiceovers.end(), [&](const VoiceoverClip& v) {
        return contains(selection.voiceoverIds, v.id) &&
               !isTrackLocked(timeline, TrackKind::Audio, v.trackIndex);
      }),
      timeline.voiceovers.end());

  timeline.textOverlays.erase(
      std::remove_if(timeline.textOverlays.begin(), timeline.textOverlays.end(), [&](const TextOverlay& t) {
        return contains(selection.textIds, t.id) &&
               !isTrackLocked(timeline, TrackKind::Text, t.trackIndex);
      }),
      timeline.textOverlays.end());

  return Error::success();
}

std::optional<PlayheadMatch> TimelineEngine::findClipAtPlayhead(
    const std::vector<Clip>& clips,
    TimeUs playheadUs) noexcept {
  if (clips.empty()) {
    return std::nullopt;
  }
  const TimeUs clampedPlayhead = std::max<TimeUs>(0, playheadUs);
  // Prefer highest visible trackIndex overlapping playhead; fallback to sequential order
  int bestTrack = -1;
  std::optional<PlayheadMatch> bestMatch;
  for (size_t i = 0; i < clips.size(); ++i) {
    const auto& clip = clips[i];
    const TimeUs eff = clipEffectiveDurationUs(clip);
    if (clampedPlayhead >= clip.timelinePositionUs && clampedPlayhead < clip.timelinePositionUs + eff) {
      if (clip.trackIndex >= bestTrack) {
        bestTrack = clip.trackIndex;
        const TimeUs delta = clampedPlayhead - clip.timelinePositionUs;
        const double speed = std::clamp(clip.playbackSpeed, 0.25, 4.0);
        const TimeUs localSource = std::min<TimeUs>(
            clip.outPointUs,
            clip.inPointUs + static_cast<TimeUs>(std::llround(static_cast<double>(delta) * speed)));
        bestMatch = PlayheadMatch{i, clip.id, localSource, delta};
      }
    }
  }
  if (bestMatch.has_value()) {
    return bestMatch;
  }
  const size_t lastIdx = clips.size() - 1;
  const auto& lastClip = clips[lastIdx];
  const TimeUs delta = std::max<TimeUs>(0, clampedPlayhead - lastClip.timelinePositionUs);
  const double speed = std::clamp(lastClip.playbackSpeed, 0.25, 4.0);
  const TimeUs localSource = std::min<TimeUs>(
      lastClip.outPointUs,
      lastClip.inPointUs + static_cast<TimeUs>(std::llround(static_cast<double>(delta) * speed)));
  return PlayheadMatch{lastIdx, lastClip.id, localSource, delta};
}

} // namespace trisvax

