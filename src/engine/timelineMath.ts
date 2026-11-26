import {
  DEFAULT_CLIP_FILTER,
  type AspectRatioPresetId,
  type AudioSpeedMode,
  type ClipFilterConfig,
  type ClipTransitionConfig,
  type CropConfig,
  type TimelineSelection,
  type TimelineState,
  type TimelineTrackState,
  type TrackKind,
  type VideoClipItem,
} from '../types/editor';

export const MIN_CLIP_DURATION_MS = 250;
export const MAX_VIDEO_TRACKS = 5;

export const EMPTY_SELECTION: TimelineSelection = {
  clipIds: [],
  audioIds: [],
  voiceoverIds: [],
  textIds: [],
};

export function getClipEffectiveDurationMs(clip: VideoClipItem): number {
  const sourceSpan = Math.max(MIN_CLIP_DURATION_MS, clip.endPositionMs - clip.startPositionMs);
  const speed = Math.max(0.25, Math.min(4.0, clip.playbackSpeed || 1));
  return Math.max(MIN_CLIP_DURATION_MS, Math.round(sourceSpan / speed));
}

export function recomputeTimelinePositions(clips: VideoClipItem[]): VideoClipItem[] {
  let cursor = 0;
  return clips.map((clip) => {
    const trackIdx = clip.trackIndex ?? 0;
    if (trackIdx <= 0) {
      const updated: VideoClipItem = {
        ...clip,
        trackIndex: 0,
        timelineStartMs: cursor,
      };
      cursor += getClipEffectiveDurationMs(updated);
      return updated;
    }
    return {
      ...clip,
      trackIndex: trackIdx,
      timelineStartMs: Math.max(0, clip.timelineStartMs),
    };
  });
}

export function getTimelineTotalDurationMs(timeline: TimelineState): number {
  let maxEnd = 0;
  for (const clip of timeline.clips) {
    maxEnd = Math.max(maxEnd, clip.timelineStartMs + getClipEffectiveDurationMs(clip));
  }
  for (const vo of timeline.voiceovers) {
    const span = Math.max(0, vo.trimEndMs - vo.trimStartMs);
    maxEnd = Math.max(maxEnd, vo.timelineStartMs + span);
  }
  for (const txt of timeline.textOverlays) {
    maxEnd = Math.max(maxEnd, txt.endTimeMs);
  }
  if (timeline.musicTrack && !timeline.musicTrack.loopToFitVideo) {
    const mStart = timeline.musicTrack.timelineStartMs ?? 0;
    const mSpan = Math.max(0, timeline.musicTrack.trimEndMs - timeline.musicTrack.trimStartMs);
    maxEnd = Math.max(maxEnd, mStart + mSpan);
  }
  return maxEnd;
}

export function getTrackState(
  timeline: TimelineState,
  kind: TrackKind,
  trackIndex: number
): TimelineTrackState {
  return (
    timeline.trackStates?.find((st) => st.kind === kind && st.trackIndex === trackIndex) ?? {
      kind,
      trackIndex,
      locked: false,
      muted: false,
      hidden: false,
      solo: false,
    }
  );
}

export function isTrackLocked(timeline: TimelineState, kind: TrackKind, trackIndex: number): boolean {
  return Boolean(getTrackState(timeline, kind, trackIndex).locked);
}

export function isTrackHidden(timeline: TimelineState, kind: TrackKind, trackIndex: number): boolean {
  return Boolean(getTrackState(timeline, kind, trackIndex).hidden);
}

export function isAudioTrackAudible(timeline: TimelineState, trackIndex: number): boolean {
  const audioStates = (timeline.trackStates ?? []).filter((s) => s.kind === 'audio');
  const anySolo = audioStates.some((s) => Boolean(s.solo));
  const st = getTrackState(timeline, 'audio', trackIndex);
  if (st.muted) return false;
  if (anySolo) return Boolean(st.solo);
  return true;
}

export function toggleTrackStateInTimeline(
  timeline: TimelineState,
  kind: TrackKind,
  trackIndex: number,
  field: 'locked' | 'muted' | 'hidden' | 'solo'
): TimelineState {
  const existing = timeline.trackStates ?? [];
  const current = getTrackState(timeline, kind, trackIndex);
  const nextItem: TimelineTrackState = {
    ...current,
    [field]: !current[field],
  };
  const found = existing.some((s) => s.kind === kind && s.trackIndex === trackIndex);
  const trackStates = found
    ? existing.map((s) => (s.kind === kind && s.trackIndex === trackIndex ? nextItem : s))
    : [...existing, nextItem];
  return { ...timeline, trackStates };
}

export function findActiveClipsAtPlayhead(
  clips: VideoClipItem[],
  playheadMs: number,
  trackStates?: TimelineTrackState[]
): Array<{ clip: VideoClipItem; index: number; localSourceTimeMs: number }> {
  if (clips.length === 0) return [];
  const clamped = Math.max(0, playheadMs);
  const matches: Array<{ clip: VideoClipItem; index: number; localSourceTimeMs: number }> = [];

  for (let i = 0; i < clips.length; i++) {
    const clip = clips[i];
    const trackIdx = clip.trackIndex ?? 0;
    const hidden = trackStates?.some((s) => s.kind === 'video' && s.trackIndex === trackIdx && s.hidden);
    if (hidden) continue;

    const start = clip.timelineStartMs;
    const dur = getClipEffectiveDurationMs(clip);
    const end = start + dur;
    if (clamped >= start && clamped < end) {
      const offsetTimeline = Math.min(dur, Math.max(0, clamped - start));
      const localSource = Math.min(
        clip.endPositionMs,
        clip.startPositionMs + offsetTimeline * clip.playbackSpeed
      );
      matches.push({ clip, index: i, localSourceTimeMs: localSource });
    }
  }

  matches.sort((a, b) => (a.clip.trackIndex ?? 0) - (b.clip.trackIndex ?? 0));
  return matches;
}

export function findClipAtPlayhead(
  clips: VideoClipItem[],
  playheadMs: number,
  trackStates?: TimelineTrackState[]
): { clip: VideoClipItem; index: number; localSourceTimeMs: number } | null {
  if (clips.length === 0) return null;
  const active = findActiveClipsAtPlayhead(clips, playheadMs, trackStates);
  if (active.length > 0) {
    return active[active.length - 1];
  }
  const visible = clips
    .map((clip, index) => ({ clip, index }))
    .filter(
      ({ clip }) =>
        !trackStates?.some(
          (s) => s.kind === 'video' && s.trackIndex === (clip.trackIndex ?? 0) && s.hidden
        )
    );
  const pool = visible.length > 0 ? visible : [{ clip: clips[0], index: 0 }];
  const last = pool[pool.length - 1];
  const dur = getClipEffectiveDurationMs(last.clip);
  const clamped = Math.max(0, playheadMs);
  const offsetTimeline = Math.min(dur, Math.max(0, clamped - last.clip.timelineStartMs));
  const localSource = Math.min(
    last.clip.endPositionMs,
    last.clip.startPositionMs + offsetTimeline * last.clip.playbackSpeed
  );
  return { clip: last.clip, index: last.index, localSourceTimeMs: localSource };
}

export function trimClip(
  clips: VideoClipItem[],
  clipId: string,
  newStartMs: number,
  newEndMs: number
): VideoClipItem[] {
  const mapped = clips.map((clip) => {
    if (clip.id !== clipId) return clip;
    const boundedStart = Math.max(0, Math.min(newStartMs, clip.sourceDurationMs - MIN_CLIP_DURATION_MS));
    const boundedEnd = Math.min(
      clip.sourceDurationMs,
      Math.max(newEndMs, boundedStart + MIN_CLIP_DURATION_MS)
    );
    return {
      ...clip,
      startPositionMs: Math.round(boundedStart),
      endPositionMs: Math.round(boundedEnd),
    };
  });
  return recomputeTimelinePositions(mapped);
}

export function splitClipAtPlayhead(
  clips: VideoClipItem[],
  playheadMs: number
): VideoClipItem[] | null {
  const found = findClipAtPlayhead(clips, playheadMs);
  if (!found) return null;
  const { clip, index, localSourceTimeMs } = found;
  if (
    localSourceTimeMs - clip.startPositionMs < MIN_CLIP_DURATION_MS ||
    clip.endPositionMs - localSourceTimeMs < MIN_CLIP_DURATION_MS
  ) {
    return null;
  }

  const splitPoint = Math.round(localSourceTimeMs);
  const firstHalf: VideoClipItem = {
    ...clip,
    endPositionMs: splitPoint,
  };
  const secondHalf: VideoClipItem = {
    ...clip,
    id: `${clip.id}_part_${Date.now()}_${Math.floor(Math.random() * 1000)}`,
    startPositionMs: splitPoint,
    timelineStartMs: Math.round(playheadMs),
    transitionIn: { type: 'cut', durationMs: 0 },
  };
  const next = [...clips.slice(0, index), firstHalf, secondHalf, ...clips.slice(index + 1)];
  return recomputeTimelinePositions(next);
}

export function updateClipSpeed(
  clips: VideoClipItem[],
  clipId: string,
  playbackSpeed: number,
  audioSpeedMode?: AudioSpeedMode
): VideoClipItem[] {
  const clamped = Math.max(0.25, Math.min(4.0, playbackSpeed));
  const mapped = clips.map((c) =>
    c.id === clipId
      ? {
          ...c,
          playbackSpeed: clamped,
          audioSpeedMode: audioSpeedMode ?? c.audioSpeedMode,
        }
      : c
  );
  return recomputeTimelinePositions(mapped);
}

export function updateClipVolume(
  clips: VideoClipItem[],
  clipId: string,
  volume: number,
  isMuted: boolean
): VideoClipItem[] {
  const clamped = Math.max(0, Math.min(2, volume));
  return clips.map((c) =>
    c.id === clipId ? { ...c, volume: clamped, isMuted } : c
  );
}

export function updateClipRotation(
  clips: VideoClipItem[],
  clipId: string,
  rotationDegrees: number
): VideoClipItem[] {
  const normalized = ((rotationDegrees % 360) + 360) % 360;
  return clips.map((c) => (c.id === clipId ? { ...c, rotationDegrees: normalized } : c));
}

export function updateClipCrop(
  clips: VideoClipItem[],
  clipId: string,
  cropConfig: CropConfig
): VideoClipItem[] {
  return clips.map((c) =>
    c.id === clipId
      ? {
          ...c,
          cropConfig: {
            offsetX: Math.max(-1, Math.min(1, cropConfig.offsetX)),
            offsetY: Math.max(-1, Math.min(1, cropConfig.offsetY)),
            zoom: Math.max(1, Math.min(3, cropConfig.zoom)),
          },
        }
      : c
  );
}

export function updateClipFilter(
  clips: VideoClipItem[],
  clipId: string,
  filter: Partial<ClipFilterConfig>
): VideoClipItem[] {
  return clips.map((c) => {
    if (c.id !== clipId) return c;
    const base = c.filter ?? DEFAULT_CLIP_FILTER;
    return {
      ...c,
      filter: {
        brightness: Math.max(-1, Math.min(1, filter.brightness ?? base.brightness)),
        contrast: Math.max(0, Math.min(2, filter.contrast ?? base.contrast)),
        saturation: Math.max(0, Math.min(2, filter.saturation ?? base.saturation)),
        temperature: Math.max(-1, Math.min(1, filter.temperature ?? base.temperature)),
        exposure: Math.max(-1, Math.min(1, filter.exposure ?? base.exposure)),
        sharpness: Math.max(0, Math.min(1, filter.sharpness ?? base.sharpness)),
      },
    };
  });
}

export function updateClipTransition(
  clips: VideoClipItem[],
  clipId: string,
  transitionIn: ClipTransitionConfig
): VideoClipItem[] {
  const dur = transitionIn.type === 'cut' ? 0 : Math.max(200, Math.min(2000, transitionIn.durationMs));
  return clips.map((c) =>
    c.id === clipId ? { ...c, transitionIn: { type: transitionIn.type, durationMs: dur } } : c
  );
}

export function reorderClip(
  clips: VideoClipItem[],
  fromIndex: number,
  toIndex: number
): VideoClipItem[] {
  if (
    fromIndex < 0 ||
    toIndex < 0 ||
    fromIndex >= clips.length ||
    toIndex >= clips.length ||
    fromIndex === toIndex
  ) {
    return clips;
  }
  const copy = [...clips];
  const [moved] = copy.splice(fromIndex, 1);
  copy.splice(toIndex, 0, moved);
  return recomputeTimelinePositions(copy);
}

export function deleteClip(clips: VideoClipItem[], clipId: string): VideoClipItem[] {
  if (clips.length <= 1) return clips;
  const remaining = clips.filter((c) => c.id !== clipId);
  return recomputeTimelinePositions(remaining);
}

export function getVideoTrackCount(timeline: TimelineState): number {
  const maxClipTrack = timeline.clips.reduce((m, c) => Math.max(m, c.trackIndex ?? 0), 0);
  return Math.min(MAX_VIDEO_TRACKS, Math.max(1, timeline.videoTrackCount ?? 1, maxClipTrack + 1));
}

export function addVideoTrack(timeline: TimelineState): TimelineState {
  const current = getVideoTrackCount(timeline);
  if (current >= MAX_VIDEO_TRACKS) return timeline;
  return {
    ...timeline,
    videoTrackCount: current + 1,
  };
}

export function deleteVideoTrack(timeline: TimelineState, trackIndex: number): TimelineState {
  const current = getVideoTrackCount(timeline);
  if (trackIndex < 1 || trackIndex >= current) return timeline;

  const remainingClips = timeline.clips
    .filter((c) => (c.trackIndex ?? 0) !== trackIndex)
    .map((c) => {
      const idx = c.trackIndex ?? 0;
      return idx > trackIndex ? { ...c, trackIndex: idx - 1 } : c;
    });

  const nextTrackStates = (timeline.trackStates ?? [])
    .filter((st) => !(st.kind === 'video' && st.trackIndex === trackIndex))
    .map((st) =>
      st.kind === 'video' && st.trackIndex > trackIndex
        ? { ...st, trackIndex: st.trackIndex - 1 }
        : st
    );

  const maxRemainingTrack = remainingClips.reduce((m, c) => Math.max(m, c.trackIndex ?? 0), 0);
  return {
    ...timeline,
    videoTrackCount: Math.max(1, Math.min(MAX_VIDEO_TRACKS, Math.max(current - 1, maxRemainingTrack + 1))),
    clips: recomputeTimelinePositions(remainingClips),
    trackStates: nextTrackStates,
  };
}

export function moveClipToTrackAndTime(
  timeline: TimelineState,
  clipId: string,
  targetStartMs: number,
  targetTrackIndex: number
): TimelineState {
  const targetClip = timeline.clips.find((c) => c.id === clipId);
  if (!targetClip) return timeline;
  const curTrack = targetClip.trackIndex ?? 0;
  const nextTrack = Math.max(0, Math.min(MAX_VIDEO_TRACKS - 1, targetTrackIndex));
  if (isTrackLocked(timeline, 'video', curTrack) || isTrackLocked(timeline, 'video', nextTrack)) {
    return timeline;
  }
  const nextStart = Math.max(0, Math.round(targetStartMs));
  return {
    ...timeline,
    videoTrackCount: Math.min(MAX_VIDEO_TRACKS, Math.max(timeline.videoTrackCount ?? 1, nextTrack + 1)),
    clips: timeline.clips.map((c) =>
      c.id === clipId ? { ...c, trackIndex: nextTrack, timelineStartMs: nextStart } : c
    ),
  };
}

export function marqueeSelectTimeline(
  timeline: TimelineState,
  startMs: number,
  endMs: number,
  rows: Array<{ kind: TrackKind; trackIndex: number }>
): TimelineSelection {
  const lo = Math.min(startMs, endMs);
  const hi = Math.max(startMs, endMs);
  const totalMs = Math.max(3000, getTimelineTotalDurationMs(timeline));
  const overlaps = (aStart: number, aEnd: number) => aEnd >= lo && aStart <= hi;
  const hasRow = (kind: TrackKind, trackIndex: number) =>
    rows.some((r) => r.kind === kind && r.trackIndex === trackIndex);

  const clipIds = timeline.clips
    .filter((c) => {
      const tr = c.trackIndex ?? 0;
      if (!hasRow('video', tr) || isTrackLocked(timeline, 'video', tr)) return false;
      return overlaps(c.timelineStartMs, c.timelineStartMs + getClipEffectiveDurationMs(c));
    })
    .map((c) => c.id);

  const audioIds: string[] = [];
  if (timeline.musicTrack) {
    const m = timeline.musicTrack;
    const mTrack = m.trackIndex ?? 0;
    if (hasRow('audio', mTrack) && !isTrackLocked(timeline, 'audio', mTrack)) {
      const mStart = m.timelineStartMs ?? 0;
      const rawSpan = Math.max(250, m.trimEndMs - m.trimStartMs);
      const mEnd = m.loopToFitVideo ? Math.max(mStart + rawSpan, totalMs) : mStart + rawSpan;
      if (overlaps(mStart, mEnd)) {
        audioIds.push(m.id);
      }
    }
  }

  const voiceoverIds = timeline.voiceovers
    .filter((v) => {
      const tr = v.trackIndex ?? 1;
      if (!hasRow('audio', tr) || isTrackLocked(timeline, 'audio', tr)) return false;
      const span = Math.max(250, v.trimEndMs - v.trimStartMs);
      return overlaps(v.timelineStartMs, v.timelineStartMs + span);
    })
    .map((v) => v.id);

  const textIds = timeline.textOverlays
    .filter((t) => {
      const tr = t.trackIndex ?? 0;
      if (!hasRow('text', tr) || isTrackLocked(timeline, 'text', tr)) return false;
      return overlaps(t.startTimeMs, t.endTimeMs);
    })
    .map((t) => t.id);

  return { clipIds, audioIds, voiceoverIds, textIds };
}

export function selectionSummary(sel: TimelineSelection) {
  const videoCount = sel.clipIds.length;
  const audioCount = sel.audioIds.length + sel.voiceoverIds.length;
  const textCount = sel.textIds.length;
  return {
    total: videoCount + audioCount + textCount,
    videoCount,
    audioCount,
    textCount,
  };
}

export function batchShiftSelection(
  timeline: TimelineState,
  selection: TimelineSelection,
  deltaMs: number,
  trackDelta: number = 0
): TimelineState {
  const clipSet = new Set(selection.clipIds);
  const audioSet = new Set(selection.audioIds);
  const voSet = new Set(selection.voiceoverIds);
  const textSet = new Set(selection.textIds);

  const clips = timeline.clips.map((c) => {
    const tr = c.trackIndex ?? 0;
    if (!clipSet.has(c.id) || isTrackLocked(timeline, 'video', tr)) return c;
    return {
      ...c,
      timelineStartMs: Math.max(0, Math.round(c.timelineStartMs + deltaMs)),
      trackIndex: Math.max(0, Math.min(MAX_VIDEO_TRACKS - 1, tr + trackDelta)),
    };
  });

  let musicTrack = timeline.musicTrack;
  if (musicTrack && audioSet.has(musicTrack.id)) {
    const tr = musicTrack.trackIndex ?? 0;
    if (!isTrackLocked(timeline, 'audio', tr)) {
      musicTrack = {
        ...musicTrack,
        timelineStartMs: Math.max(0, Math.round((musicTrack.timelineStartMs ?? 0) + deltaMs)),
        trackIndex: Math.max(0, Math.min(7, tr + trackDelta)),
      };
    }
  }

  const voiceovers = timeline.voiceovers.map((v) => {
    const tr = v.trackIndex ?? 1;
    if (!voSet.has(v.id) || isTrackLocked(timeline, 'audio', tr)) return v;
    return {
      ...v,
      timelineStartMs: Math.max(0, Math.round(v.timelineStartMs + deltaMs)),
      trackIndex: Math.max(0, Math.min(7, tr + trackDelta)),
    };
  });

  const textOverlays = timeline.textOverlays.map((t) => {
    const tr = t.trackIndex ?? 0;
    if (!textSet.has(t.id) || isTrackLocked(timeline, 'text', tr)) return t;
    const dur = Math.max(300, t.endTimeMs - t.startTimeMs);
    const nextStart = Math.max(0, Math.round(t.startTimeMs + deltaMs));
    return {
      ...t,
      startTimeMs: nextStart,
      endTimeMs: nextStart + dur,
      trackIndex: Math.max(0, Math.min(7, tr + trackDelta)),
    };
  });

  return {
    ...timeline,
    clips,
    musicTrack,
    voiceovers,
    textOverlays,
  };
}

export function batchMuteSelection(
  timeline: TimelineState,
  selection: TimelineSelection,
  muted: boolean
): TimelineState {
  const clipSet = new Set(selection.clipIds);
  const audioSet = new Set(selection.audioIds);
  const voSet = new Set(selection.voiceoverIds);

  return {
    ...timeline,
    clips: timeline.clips.map((c) =>
      clipSet.has(c.id) && !isTrackLocked(timeline, 'video', c.trackIndex ?? 0)
        ? { ...c, isMuted: muted }
        : c
    ),
    musicTrack:
      timeline.musicTrack &&
      audioSet.has(timeline.musicTrack.id) &&
      !isTrackLocked(timeline, 'audio', timeline.musicTrack.trackIndex ?? 0)
        ? { ...timeline.musicTrack, isMuted: muted }
        : timeline.musicTrack,
    voiceovers: timeline.voiceovers.map((v) =>
      voSet.has(v.id) && !isTrackLocked(timeline, 'audio', v.trackIndex ?? 1)
        ? { ...v, isMuted: muted }
        : v
    ),
  };
}

export function batchDuplicateSelection(
  timeline: TimelineState,
  selection: TimelineSelection
): { timeline: TimelineState; selection: TimelineSelection } {
  const clipSet = new Set(selection.clipIds);
  const voSet = new Set(selection.voiceoverIds);
  const textSet = new Set(selection.textIds);

  const newClipIds: string[] = [];
  const addedClips: VideoClipItem[] = [];
  for (const c of timeline.clips) {
    if (!clipSet.has(c.id) || isTrackLocked(timeline, 'video', c.trackIndex ?? 0)) continue;
    const dupId = `${c.id}_dup_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
    const nextTrack = Math.min(MAX_VIDEO_TRACKS - 1, (c.trackIndex ?? 0) + 1);
    newClipIds.push(dupId);
    addedClips.push({
      ...c,
      id: dupId,
      trackIndex: nextTrack,
      timelineStartMs: c.timelineStartMs + 500,
    });
  }

  const newVoIds: string[] = [];
  const addedVos = timeline.voiceovers
    .filter((v) => voSet.has(v.id) && !isTrackLocked(timeline, 'audio', v.trackIndex ?? 1))
    .map((v) => {
      const dupId = `${v.id}_dup_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
      newVoIds.push(dupId);
      return {
        ...v,
        id: dupId,
        timelineStartMs: v.timelineStartMs + 500,
      };
    });

  const newTextIds: string[] = [];
  const addedTexts = timeline.textOverlays
    .filter((t) => textSet.has(t.id) && !isTrackLocked(timeline, 'text', t.trackIndex ?? 0))
    .map((t) => {
      const dupId = `${t.id}_dup_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
      newTextIds.push(dupId);
      return {
        ...t,
        id: dupId,
        startTimeMs: t.startTimeMs + 500,
        endTimeMs: t.endTimeMs + 500,
      };
    });

  const maxClipTrack = addedClips.reduce((m, c) => Math.max(m, c.trackIndex ?? 0), 0);
  return {
    timeline: {
      ...timeline,
      videoTrackCount: Math.min(MAX_VIDEO_TRACKS, Math.max(timeline.videoTrackCount ?? 1, maxClipTrack + 1)),
      clips: [...timeline.clips, ...addedClips],
      voiceovers: [...timeline.voiceovers, ...addedVos],
      textOverlays: [...timeline.textOverlays, ...addedTexts],
    },
    selection: {
      clipIds: newClipIds,
      audioIds: [],
      voiceoverIds: newVoIds,
      textIds: newTextIds,
    },
  };
}

export function batchDeleteSelection(
  timeline: TimelineState,
  selection: TimelineSelection
): TimelineState {
  const clipSet = new Set(selection.clipIds);
  const audioSet = new Set(selection.audioIds);
  const voSet = new Set(selection.voiceoverIds);
  const textSet = new Set(selection.textIds);

  let nextClips = timeline.clips;
  if (clipSet.size > 0 && timeline.clips.length > 1) {
    const kept = timeline.clips.filter(
      (c) => !(clipSet.has(c.id) && !isTrackLocked(timeline, 'video', c.trackIndex ?? 0))
    );
    nextClips = kept.length > 0 ? kept : [timeline.clips[0]];
  }

  const nextMusic =
    timeline.musicTrack &&
    audioSet.has(timeline.musicTrack.id) &&
    !isTrackLocked(timeline, 'audio', timeline.musicTrack.trackIndex ?? 0)
      ? null
      : timeline.musicTrack;

  const nextVos = timeline.voiceovers.filter(
    (v) => !(voSet.has(v.id) && !isTrackLocked(timeline, 'audio', v.trackIndex ?? 1))
  );

  const nextTexts = timeline.textOverlays.filter(
    (t) => !(textSet.has(t.id) && !isTrackLocked(timeline, 'text', t.trackIndex ?? 0))
  );

  return {
    ...timeline,
    clips: nextClips,
    musicTrack: nextMusic,
    voiceovers: nextVos,
    textOverlays: nextTexts,
  };
}

export function applyAspectPresetToClips(
  timeline: TimelineState,
  preset: AspectRatioPresetId
): TimelineState {
  return {
    ...timeline,
    aspectRatio: preset,
  };
}

export function buildCssFilterString(filter?: ClipFilterConfig): string {
  if (!filter) return 'none';
  const bri = Math.max(0.2, 1 + filter.brightness * 0.55 + filter.exposure * 0.45);
  const con = Math.max(0.2, filter.contrast);
  const sat = Math.max(0, filter.saturation);
  const hue = Math.round(filter.temperature * -18);
  const sepia = filter.temperature > 0 ? Math.min(0.35, filter.temperature * 0.35) : 0;
  return `brightness(${bri.toFixed(2)}) contrast(${con.toFixed(2)}) saturate(${sat.toFixed(2)}) sepia(${sepia.toFixed(2)}) hue-rotate(${hue}deg)`;
}

export function formatShortTimecode(ms: number): string {
  const totalSec = Math.max(0, Math.floor(ms / 1000));
  const m = Math.floor(totalSec / 60);
  const s = totalSec % 60;
  return `${m}:${String(s).padStart(2, '0')}`;
}

export function formatDurationBadge(ms: number): string {
  const totalSec = Math.max(0, Math.floor(ms / 1000));
  const m = Math.floor(totalSec / 60);
  const s = totalSec % 60;
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

export function formatStorageBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '0 KB';
  const kb = bytes / 1024;
  if (kb < 1024) return `${Math.max(1, Math.round(kb))} KB`;
  const mb = kb / 1024;
  if (mb < 1024) return `${mb.toFixed(1)} MB`;
  return `${(mb / 1024).toFixed(1)} GB`;
}

export function formatRelativeTime(ts: number): string {
  const diffMs = Math.max(0, Date.now() - ts);
  const mins = Math.floor(diffMs / 60000);
  if (mins < 5) return 'Now';
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  return `${days}d ago`;
}

/** Converts Web TimelineState to the C++20 trisvax_cli TimelineSnapshot JSON shape. */
export function toNativeTimelinePayload(timeline: TimelineState) {
  return {
    aspectRatio: timeline.aspectRatio,
    customAspectWidth: 9,
    customAspectHeight: 16,
    clips: timeline.clips.map((c) => ({
      id: c.id,
      sourceUri: c.sourceUrl,
      displayName: c.displayName,
      isImage: c.isImage,
      sourceDurationUs: Math.round(c.sourceDurationMs * 1000),
      inPointUs: Math.round(c.startPositionMs * 1000),
      outPointUs: Math.round(c.endPositionMs * 1000),
      timelinePositionUs: Math.round(c.timelineStartMs * 1000),
      trackIndex: c.trackIndex ?? 0,
      playbackSpeed: c.playbackSpeed,
      audioSpeedMode: c.audioSpeedMode === 'pitch_preserved' ? 1 : 0,
      volume: c.volume,
      isMuted: c.isMuted,
      crop: {
        preset: timeline.aspectRatio,
        aspectWidth: 9,
        aspectHeight: 16,
        offsetX: c.cropConfig.offsetX,
        offsetY: c.cropConfig.offsetY,
        zoom: c.cropConfig.zoom,
        rotationDegrees: c.rotationDegrees,
      },
      filter: c.filter ?? DEFAULT_CLIP_FILTER,
      transitionIn: {
        type: c.transitionIn?.type === 'fade' ? 1 : c.transitionIn?.type === 'cross_dissolve' ? 2 : 0,
        durationUs: Math.round((c.transitionIn?.durationMs ?? 0) * 1000),
      },
      thumbnailPath: c.thumbnails[0] ?? '',
      mediaAvailable: true,
    })),
    musicTrack: timeline.musicTrack
      ? {
          id: timeline.musicTrack.id,
          title: timeline.musicTrack.title,
          sourceUri: timeline.musicTrack.sourceUrl,
          isBundledAsset: timeline.musicTrack.isBundledAsset,
          sourceDurationUs: Math.round(timeline.musicTrack.sourceDurationMs * 1000),
          trimStartUs: Math.round(timeline.musicTrack.trimStartMs * 1000),
          trimEndUs: Math.round(timeline.musicTrack.trimEndMs * 1000),
          timelineStartUs: Math.round((timeline.musicTrack.timelineStartMs ?? 0) * 1000),
          trackIndex: timeline.musicTrack.trackIndex ?? 0,
          volume: timeline.musicTrack.volume,
          isMuted: timeline.musicTrack.isMuted,
          loopToFit: timeline.musicTrack.loopToFitVideo,
          fadeInUs: Math.round(timeline.musicTrack.fadeInMs * 1000),
          fadeOutUs: Math.round(timeline.musicTrack.fadeOutMs * 1000),
        }
      : null,
    voiceovers: timeline.voiceovers.map((v) => ({
      id: v.id,
      title: v.title,
      filePath: v.sourceUrl,
      sourceDurationUs: Math.round(v.durationMs * 1000),
      trimStartUs: Math.round(v.trimStartMs * 1000),
      trimEndUs: Math.round(v.trimEndMs * 1000),
      timelineStartUs: Math.round(v.timelineStartMs * 1000),
      trackIndex: v.trackIndex ?? 1,
      volume: v.volume,
      isMuted: v.isMuted,
    })),
    textOverlays: timeline.textOverlays.map((t) => ({
      id: t.id,
      text: t.text,
      fontFamily: t.fontFamily,
      fontSizeSp: t.fontSizePx,
      colorArgb: parseInt(t.colorHex.replace('#', ''), 16) | 0xff000000,
      alignment: t.alignment === 'left' ? 0 : t.alignment === 'right' ? 2 : 1,
      normalizedX: t.normalizedX,
      normalizedY: t.normalizedY,
      startUs: Math.round(t.startTimeMs * 1000),
      endUs: Math.round(t.endTimeMs * 1000),
      trackIndex: t.trackIndex ?? 0,
      animation: t.animation === 'fade' ? 1 : t.animation === 'slide_up' ? 2 : 0,
    })),
    trackStates: (timeline.trackStates ?? []).map((ts) => ({
      kind: ts.kind === 'video' ? 0 : ts.kind === 'audio' ? 1 : 2,
      trackIndex: ts.trackIndex,
      locked: Boolean(ts.locked),
      muted: Boolean(ts.muted),
      hidden: Boolean(ts.hidden),
      solo: Boolean(ts.solo),
    })),
  };
}
