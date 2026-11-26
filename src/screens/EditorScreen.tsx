import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  ArrowLeftRight,
  Crop,
  FastForward,
  Layers,
  Mic,
  Music,
  Pause,
  Play,
  Plus,
  Redo2,
  RotateCw,
  Save,
  Scissors,
  Sliders,
  SlidersHorizontal,
  SquarePlus,
  Trash2,
  Type,
  Undo2,
  Volume2,
  X,
} from 'lucide-react';
import { EditorToolPanel } from '../components/EditorToolPanel';
import { TimelineTracks } from '../components/TimelineTracks';
import {
  addVideoTrack,
  applyAspectPresetToClips,
  batchDeleteSelection,
  batchShiftSelection,
  buildCssFilterString,
  deleteClip,
  deleteVideoTrack,
  EMPTY_SELECTION,
  findActiveClipsAtPlayhead,
  findClipAtPlayhead,
  formatShortTimecode,
  getClipEffectiveDurationMs,
  getTimelineTotalDurationMs,
  getVideoTrackCount,
  isAudioTrackAudible,
  isTrackHidden,
  MAX_VIDEO_TRACKS,
  reorderClip,
  recomputeTimelinePositions,
  selectionSummary,
  splitClipAtPlayhead,
  toggleTrackStateInTimeline,
  trimClip,
  updateClipCrop,
  updateClipFilter,
  updateClipRotation,
  updateClipSpeed,
  updateClipTransition,
  updateClipVolume,
} from '../engine/timelineMath';
import {
  extractImageThumbnail,
  extractVideoMetadata,
  extractVideoThumbnails,
} from '../engine/videoMediaUtils';
import { getMediaBlob, saveMediaBlob } from '../storage/projectDb';
import {
  ASPECT_PRESETS,
  DEFAULT_CLIP_FILTER,
  type BundledMusicTrack,
  type EditorToolId,
  type ProjectRecord,
  type TimelineSelection,
  type TimelineState,
  type VideoClipItem,
} from '../types/editor';

const TOOLS: {
  id: EditorToolId | 'save' | 'add_track' | 'delete';
  label: string;
  icon: React.ComponentType<{ className?: string }>;
}[] = [
  { id: 'music', label: 'Music', icon: Music },
  { id: 'text', label: 'Text', icon: Type },
  { id: 'crop', label: 'Crop', icon: Crop },
  { id: 'trim', label: 'Trim', icon: SlidersHorizontal },
  { id: 'split', label: 'Split', icon: Scissors },
  { id: 'adjust', label: 'Adjust', icon: Sliders },
  { id: 'speed', label: 'Speed', icon: FastForward },
  { id: 'volume', label: 'Volume', icon: Volume2 },
  { id: 'rotate', label: 'Rotate', icon: RotateCw },
  { id: 'voice', label: 'Voice', icon: Mic },
  { id: 'transitions', label: 'Transitions', icon: Layers },
  { id: 'clips', label: 'Clips', icon: ArrowLeftRight },
  { id: 'add_track', label: 'Add Track', icon: SquarePlus },
  { id: 'delete', label: 'Delete', icon: Trash2 },
  { id: 'save', label: 'Save', icon: Save },
];

export function EditorScreen({
  project,
  onSaveProject,
  onClose,
  onExport,
}: {
  project: ProjectRecord;
  onSaveProject: (updated: ProjectRecord) => void;
  onClose: () => void;
  onExport: (timeline: TimelineState) => void;
}) {
  const [timeline, setTimeline] = useState<TimelineState>(project.timeline);
  const [undoStack, setUndoStack] = useState<TimelineState[]>([]);
  const [redoStack, setRedoStack] = useState<TimelineState[]>([]);
  const [selectedClipId, setSelectedClipId] = useState<string | null>(
    project.timeline.clips[0]?.id || null
  );
  const [selectedTextId, setSelectedTextId] = useState<string>('');
  const [selection, setSelection] = useState<TimelineSelection>(EMPTY_SELECTION);
  const [playheadMs, setPlayheadMs] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const [activeTool, setActiveTool] = useState<EditorToolId>('none');
  const [bundledMusic, setBundledMusic] = useState<BundledMusicTrack[]>([]);
  const [saveNotice, setSaveNotice] = useState<string | null>(null);

  const videoRef = useRef<HTMLVideoElement | null>(null);
  const musicAudioRef = useRef<HTMLAudioElement | null>(null);
  const voiceAudioRef = useRef<HTMLAudioElement | null>(null);
  const addClipsInputRef = useRef<HTMLInputElement | null>(null);
  const uploadTrackIndexRef = useRef<number | null>(null);
  const playLoopRef = useRef<number | null>(null);
  const lastFrameRef = useRef<number>(0);
  const isPlayingRef = useRef(false);
  const pendingSeekSecRef = useRef<number | null>(null);
  const lastActiveClipIdRef = useRef<string | null>(null);

  isPlayingRef.current = isPlaying;

  useEffect(() => {
    setTimeline(project.timeline);
    setUndoStack([]);
    setRedoStack([]);
    setSelectedClipId(project.timeline.clips[0]?.id || null);
    setSelection(EMPTY_SELECTION);
    setPlayheadMs(0);
    setIsPlaying(false);
    pendingSeekSecRef.current = null;
    lastActiveClipIdRef.current = null;
  }, [project.id]);

  useEffect(() => {
    fetch('/api/music-catalog')
      .then((r) => r.json())
      .then((data) => {
        if (Array.isArray(data.tracks)) setBundledMusic(data.tracks);
      })
      .catch(() => {});
  }, []);

  const totalDurationMs = useMemo(() => getTimelineTotalDurationMs(timeline), [timeline]);
  const playheadLookup = useMemo(
    () => findClipAtPlayhead(timeline.clips, playheadMs, timeline.trackStates),
    [timeline.clips, timeline.trackStates, playheadMs]
  );
  const activeLayers = useMemo(
    () => findActiveClipsAtPlayhead(timeline.clips, playheadMs, timeline.trackStates),
    [timeline.clips, timeline.trackStates, playheadMs]
  );
  const activeClip = playheadLookup?.clip || timeline.clips[0] || null;
  const selectedClip = timeline.clips.find((c) => c.id === selectedClipId) || activeClip;

  const persistTimeline = (nextTimeline: TimelineState) => {
    const dur = getTimelineTotalDurationMs(nextTimeline);
    onSaveProject({
      ...project,
      durationMs: dur,
      thumbnailUrl: nextTimeline.clips[0]?.thumbnails[0] || project.thumbnailUrl,
      category:
        nextTimeline.clips.length === 0
          ? 'DRAFTS'
          : nextTimeline.aspectRatio === 'PORTRAIT_9_16'
            ? 'REELS'
            : 'VIDEOS',
      lastModifiedAt: Date.now(),
      timeline: nextTimeline,
    });
  };

  const commitTimeline = (nextTimeline: TimelineState) => {
    setUndoStack((prev) => [...prev.slice(-39), timeline]);
    setRedoStack([]);
    setTimeline(nextTimeline);
    persistTimeline(nextTimeline);
  };

  const handleUndo = () => {
    if (undoStack.length === 0) return;
    const previous = undoStack[undoStack.length - 1];
    setUndoStack((prev) => prev.slice(0, -1));
    setRedoStack((prev) => [...prev, timeline]);
    setTimeline(previous);
    persistTimeline(previous);
  };

  const handleRedo = () => {
    if (redoStack.length === 0) return;
    const next = redoStack[redoStack.length - 1];
    setRedoStack((prev) => prev.slice(0, -1));
    setUndoStack((prev) => [...prev, timeline]);
    setTimeline(next);
    persistTimeline(next);
  };

  const handleExplicitSave = () => {
    persistTimeline(timeline);
    setSaveNotice('Project saved locally');
    window.setTimeout(() => setSaveNotice(null), 2200);
  };

  const syncVideoReadyAndPlay = () => {
    const vid = videoRef.current;
    if (!vid) return;
    if (pendingSeekSecRef.current !== null && vid.readyState >= 1) {
      try {
        vid.currentTime = pendingSeekSecRef.current;
        pendingSeekSecRef.current = null;
      } catch {
        // retry on next ready event
      }
    }
    if (
      isPlayingRef.current &&
      activeLayers.length > 0 &&
      activeClip &&
      !activeClip.isImage &&
      vid.paused
    ) {
      vid.play().catch(() => {
        vid.muted = true;
        vid.play().catch(() => {});
      });
    }
  };

  useEffect(() => {
    const vid = videoRef.current;
    if (!vid) return;

    if (!activeClip || activeClip.isImage || activeLayers.length === 0) {
      if (!vid.paused) vid.pause();
      return;
    }

    const targetSec = (playheadLookup?.localSourceTimeMs ?? activeClip.startPositionMs) / 1000;
    const srcChanged = vid.getAttribute('data-src-url') !== activeClip.sourceUrl;
    const clipChanged = lastActiveClipIdRef.current !== activeClip.id;
    lastActiveClipIdRef.current = activeClip.id;

    vid.playbackRate = activeClip.playbackSpeed;
    vid.muted = activeClip.isMuted || activeClip.volume <= 0.01;
    vid.volume = Math.min(1, Math.max(0, activeClip.volume));

    if (srcChanged) {
      vid.setAttribute('data-src-url', activeClip.sourceUrl);
      pendingSeekSecRef.current = targetSec;
      vid.src = activeClip.sourceUrl;
      vid.load();
      return;
    }

    const driftThreshold = clipChanged || !isPlaying ? 0.08 : 0.45;
    if (Math.abs(vid.currentTime - targetSec) > driftThreshold) {
      if (vid.readyState >= 1) {
        try {
          vid.currentTime = targetSec;
          pendingSeekSecRef.current = null;
        } catch {
          pendingSeekSecRef.current = targetSec;
        }
      } else {
        pendingSeekSecRef.current = targetSec;
      }
    }

    if (isPlaying && vid.paused && vid.readyState >= 2) {
      vid.play().catch(() => {
        vid.muted = true;
        vid.play().catch(() => {});
      });
    }
  }, [
    activeClip?.id,
    activeClip?.sourceUrl,
    activeClip?.isImage,
    activeClip?.playbackSpeed,
    activeClip?.isMuted,
    activeClip?.volume,
    activeLayers.length,
    playheadLookup?.localSourceTimeMs,
    isPlaying,
  ]);

  useEffect(() => {
    if (!isPlaying) {
      if (playLoopRef.current) cancelAnimationFrame(playLoopRef.current);
      videoRef.current?.pause();
      musicAudioRef.current?.pause();
      voiceAudioRef.current?.pause();
      return;
    }

    if (activeLayers.length > 0 && activeClip && !activeClip.isImage && videoRef.current) {
      const vid = videoRef.current;
      if (vid.readyState >= 2) {
        vid.play().catch(() => {
          vid.muted = true;
          vid.play().catch(() => {});
        });
      }
    }

    if (
      timeline.musicTrack &&
      !timeline.musicTrack.isMuted &&
      isAudioTrackAudible(timeline, timeline.musicTrack.trackIndex ?? 0) &&
      musicAudioRef.current
    ) {
      const m = timeline.musicTrack;
      const mOffsetSec = Math.max(0, (playheadMs - (m.timelineStartMs ?? 0) + m.trimStartMs) / 1000);
      if (Math.abs(musicAudioRef.current.currentTime - mOffsetSec) > 0.25) {
        try {
          musicAudioRef.current.currentTime = mOffsetSec;
        } catch {
          // ignore before metadata
        }
      }
      musicAudioRef.current.volume = Math.min(1, m.volume);
      musicAudioRef.current.play().catch(() => {});
    }
    if (
      timeline.voiceovers[0] &&
      !timeline.voiceovers[0].isMuted &&
      isAudioTrackAudible(timeline, timeline.voiceovers[0].trackIndex ?? 1) &&
      voiceAudioRef.current
    ) {
      const vo = timeline.voiceovers[0];
      voiceAudioRef.current.volume = Math.min(1, vo.volume);
      if (playheadMs >= vo.timelineStartMs) {
        const voSec = Math.max(0, (playheadMs - vo.timelineStartMs + vo.trimStartMs) / 1000);
        if (Math.abs(voiceAudioRef.current.currentTime - voSec) > 0.25) {
          try {
            voiceAudioRef.current.currentTime = voSec;
          } catch {
            // ignore
          }
        }
        voiceAudioRef.current.play().catch(() => {});
      }
    }

    lastFrameRef.current = performance.now();
    const step = (now: number) => {
      const dt = now - lastFrameRef.current;
      lastFrameRef.current = now;
      setPlayheadMs((prev) => {
        const next = prev + dt;
        if (next >= totalDurationMs) {
          setIsPlaying(false);
          return 0;
        }
        return next;
      });
      playLoopRef.current = requestAnimationFrame(step);
    };
    playLoopRef.current = requestAnimationFrame(step);
    return () => {
      if (playLoopRef.current) cancelAnimationFrame(playLoopRef.current);
    };
  }, [isPlaying, totalDurationMs]);

  const handleSeek = (ms: number) => {
    const clamped = Math.max(0, Math.min(totalDurationMs, ms));
    setPlayheadMs(clamped);
    const lookup = findClipAtPlayhead(timeline.clips, clamped, timeline.trackStates);
    if (lookup) {
      setSelectedClipId(lookup.clip.id);
      if (videoRef.current && !lookup.clip.isImage) {
        const targetSec = lookup.localSourceTimeMs / 1000;
        if (
          videoRef.current.getAttribute('data-src-url') === lookup.clip.sourceUrl &&
          videoRef.current.readyState >= 1
        ) {
          try {
            videoRef.current.currentTime = targetSec;
            pendingSeekSecRef.current = null;
          } catch {
            pendingSeekSecRef.current = targetSec;
          }
        } else {
          pendingSeekSecRef.current = targetSec;
        }
      }
    }
    if (musicAudioRef.current && timeline.musicTrack) {
      const m = timeline.musicTrack;
      const mSec = Math.max(0, (clamped - (m.timelineStartMs ?? 0) + m.trimStartMs) / 1000);
      try {
        musicAudioRef.current.currentTime = mSec;
      } catch {
        // ignore
      }
    }
  };

  const handleAddMoreFiles = async (files: FileList) => {
    const appended: VideoClipItem[] = [];
    for (const file of Array.from(files)) {
      const id = `clip_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`;
      await saveMediaBlob(id, file);
      const url = URL.createObjectURL(file);
      const isImage = file.type.startsWith('image/');
      if (isImage) {
        const thumb = await extractImageThumbnail(url);
        appended.push({
          id,
          sourceUrl: url,
          mediaBlobId: id,
          displayName: file.name,
          isImage: true,
          sourceDurationMs: 3000,
          startPositionMs: 0,
          endPositionMs: 3000,
          timelineStartMs: 0,
          playbackSpeed: 1,
          audioSpeedMode: 'rate_coupled',
          volume: 1,
          isMuted: false,
          rotationDegrees: 0,
          cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
          filter: { ...DEFAULT_CLIP_FILTER },
          transitionIn: { type: 'cut', durationMs: 0 },
          thumbnails: [thumb],
          fileSizeBytes: file.size,
        });
      } else {
        const meta = await extractVideoMetadata(url);
        const thumbs = await extractVideoThumbnails(url, meta.durationMs, 3);
        appended.push({
          id,
          sourceUrl: url,
          mediaBlobId: id,
          displayName: file.name,
          isImage: false,
          sourceDurationMs: meta.durationMs,
          startPositionMs: 0,
          endPositionMs: meta.durationMs,
          timelineStartMs: 0,
          playbackSpeed: 1,
          audioSpeedMode: 'rate_coupled',
          volume: 1,
          isMuted: false,
          rotationDegrees: 0,
          cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
          filter: { ...DEFAULT_CLIP_FILTER },
          transitionIn: { type: 'cut', durationMs: 0 },
          thumbnails: thumbs,
          fileSizeBytes: file.size,
        });
      }
    }
    if (appended.length > 0) {
      const targetTrack = uploadTrackIndexRef.current ?? 0;
      uploadTrackIndexRef.current = null;
      if (targetTrack === 0) {
        commitTimeline({
          ...timeline,
          clips: recomputeTimelinePositions([...timeline.clips, ...appended]),
        });
      } else {
        const existingOnTrack = timeline.clips.filter((c) => (c.trackIndex ?? 0) === targetTrack);
        let cursor = existingOnTrack.reduce(
          (maxEnd, c) => Math.max(maxEnd, c.timelineStartMs + getClipEffectiveDurationMs(c)),
          0
        );
        const placed = appended.map((c) => {
          const item: VideoClipItem = {
            ...c,
            trackIndex: targetTrack,
            timelineStartMs: cursor,
          };
          cursor += getClipEffectiveDurationMs(item);
          return item;
        });
        commitTimeline({
          ...timeline,
          videoTrackCount: Math.min(MAX_VIDEO_TRACKS, Math.max(getVideoTrackCount(timeline), targetTrack + 1)),
          clips: recomputeTimelinePositions([...timeline.clips, ...placed]),
        });
        setSelectedClipId(placed[0].id);
      }
    } else {
      uploadTrackIndexRef.current = null;
    }
  };

  const presetMeta = ASPECT_PRESETS[timeline.aspectRatio];
  const transitionOpacity = useMemo(() => {
    if (!activeClip?.transitionIn || activeClip.transitionIn.type === 'cut') return 1;
    const dur = activeClip.transitionIn.durationMs;
    if (dur <= 0) return 1;
    const elapsedInClip = Math.max(0, playheadMs - activeClip.timelineStartMs);
    return elapsedInClip < dur ? Math.max(0.08, elapsedInClip / dur) : 1;
  }, [activeClip, playheadMs]);

  return (
    <div className="flex h-dvh w-full flex-col overflow-hidden bg-[#0B0807]">
      {/* Hidden Audio Elements for Music + Voiceover Mixing Preview */}
      {timeline.musicTrack && (
        <audio
          ref={musicAudioRef}
          src={timeline.musicTrack.sourceUrl}
          loop={timeline.musicTrack.loopToFitVideo}
          className="hidden"
        />
      )}
      {timeline.voiceovers[0] && (
        <audio ref={voiceAudioRef} src={timeline.voiceovers[0].sourceUrl} className="hidden" />
      )}

      {/* Top Video Preview Box with X Close, Undo/Redo & Next Export */}
      <div className="relative flex min-h-0 flex-1 items-center justify-center px-3 pb-2 pt-14 sm:p-3">
        <div
          style={{ aspectRatio: `${presetMeta.ratioWidth}/${presetMeta.ratioHeight}` }}
          className="relative h-full max-h-full w-auto max-w-full overflow-hidden rounded-2xl border border-[#2B352D] bg-black shadow-2xl"
        >
          <video
            ref={videoRef}
            playsInline
            preload="auto"
            onLoadedMetadata={syncVideoReadyAndPlay}
            onCanPlay={syncVideoReadyAndPlay}
            onError={async () => {
              if (!activeClip?.sourceUrl.startsWith('blob:')) return;
              const blobId = activeClip.mediaBlobId || activeClip.id;
              const blob = await getMediaBlob(blobId);
              if (blob) {
                const freshUrl = URL.createObjectURL(blob);
                setTimeline((cur) => ({
                  ...cur,
                  clips: cur.clips.map((c) =>
                    c.id === activeClip.id ? { ...c, sourceUrl: freshUrl, mediaBlobId: blobId } : c
                  ),
                }));
              }
            }}
            style={{
              opacity:
                activeLayers.length > 0 && activeClip && !activeClip.isImage ? transitionOpacity : 0,
              filter: activeClip ? buildCssFilterString(activeClip.filter) : 'none',
              transform: activeClip
                ? `translate(${activeClip.cropConfig.offsetX * 25}%, ${activeClip.cropConfig.offsetY * 25}%) scale(${activeClip.cropConfig.zoom}) rotate(${activeClip.rotationDegrees}deg)`
                : undefined,
            }}
            className={`h-full w-full object-cover ${
              activeLayers.length > 0 && activeClip && !activeClip.isImage
                ? ''
                : 'pointer-events-none'
            }`}
          />

          {activeLayers.length > 0 && activeClip?.isImage && (
            <img
              src={activeClip.sourceUrl}
              alt={activeClip.displayName}
              style={{
                opacity: transitionOpacity,
                filter: buildCssFilterString(activeClip.filter),
                transform: `translate(${activeClip.cropConfig.offsetX * 25}%, ${activeClip.cropConfig.offsetY * 25}%) scale(${activeClip.cropConfig.zoom}) rotate(${activeClip.rotationDegrees}deg)`,
              }}
              className="absolute inset-0 h-full w-full object-cover"
            />
          )}

          {activeLayers.length === 0 && (
            <div className="absolute inset-0 flex h-full w-full items-center justify-center bg-[#0B0807] text-xs text-[#667568]">
              Track hidden or gap
            </div>
          )}

          {/* Active Text Overlays rendered inside Preview */}
          {timeline.textOverlays
            .filter(
              (t) =>
                !isTrackHidden(timeline, 'text', t.trackIndex ?? 0) &&
                playheadMs >= t.startTimeMs &&
                playheadMs <= t.endTimeMs
            )
            .map((t) => (
              <button
                key={t.id}
                type="button"
                onClick={() => {
                  setSelectedTextId(t.id);
                  setActiveTool('text');
                }}
                style={{
                  left:
                    t.alignment === 'left'
                      ? '10%'
                      : t.alignment === 'right'
                        ? '90%'
                        : `${t.normalizedX * 100}%`,
                  top: `${t.normalizedY * 100}%`,
                  color: t.colorHex,
                  fontSize: `${Math.max(14, t.fontSizePx * 0.72)}px`,
                  transform:
                    t.alignment === 'left'
                      ? 'translate(0, -50%)'
                      : t.alignment === 'right'
                        ? 'translate(-100%, -50%)'
                        : 'translate(-50%, -50%)',
                  textShadow: '0 2px 10px rgba(0,0,0,0.85)',
                }}
                className="absolute z-10 whitespace-nowrap font-bold"
              >
                {t.text}
              </button>
            ))}
        </div>

        {/* Overlay Controls: Top-Left X, Top-Right Undo/Redo + Next */}
        <button
          type="button"
          onClick={onClose}
          aria-label="Close editor"
          className="absolute left-5 top-5 z-20 flex h-10 w-10 items-center justify-center rounded-full border border-[#2B352D] bg-black/65 text-white hover:border-[#FF5500]"
        >
          <X className="h-5 w-5" />
        </button>

        <div className="absolute right-5 top-5 z-20 flex items-center gap-2">
          <button
            type="button"
            disabled={undoStack.length === 0}
            onClick={handleUndo}
            aria-label="Undo"
            className="flex h-10 w-10 items-center justify-center rounded-full border border-[#2B352D] bg-black/65 text-white hover:border-[#FF5500] disabled:opacity-35"
          >
            <Undo2 className="h-4 w-4" />
          </button>
          <button
            type="button"
            disabled={redoStack.length === 0}
            onClick={handleRedo}
            aria-label="Redo"
            className="flex h-10 w-10 items-center justify-center rounded-full border border-[#2B352D] bg-black/65 text-white hover:border-[#FF5500] disabled:opacity-35"
          >
            <Redo2 className="h-4 w-4" />
          </button>
          <button
            type="button"
            onClick={() => onExport(timeline)}
            className="min-h-[40px] rounded-xl bg-[#FF5500] px-4 text-sm font-bold text-white shadow-lg transition-colors hover:bg-[#FF8232]"
          >
            Next
          </button>
        </div>

        {saveNotice && (
          <div className="absolute bottom-5 left-1/2 z-20 -translate-x-1/2 rounded-lg border border-[#FF5500] bg-[#18110D] px-3 py-1.5 text-xs font-medium text-white shadow">
            {saveNotice}
          </div>
        )}
      </div>

      {/* Transport Bar */}
      <div className="flex items-center justify-between px-4 py-1.5">
        <div className="flex items-center gap-3">
          <button
            type="button"
            aria-label={isPlaying ? 'Pause' : 'Play'}
            onClick={() => setIsPlaying((p) => !p)}
            className="flex h-10 w-10 items-center justify-center rounded-full border border-[#2B352D] bg-[#18110D] text-white hover:border-[#FF5500]"
          >
            {isPlaying ? <Pause className="h-4 w-4" /> : <Play className="h-4 w-4" />}
          </button>
          <span className="text-xs font-semibold text-white font-mono-tabular">
            {formatShortTimecode(playheadMs)} / {formatShortTimecode(totalDurationMs)}
          </span>
        </div>

        <div className="flex items-center gap-2">
          <button
            type="button"
            aria-label="Append clips"
            onClick={() => {
              uploadTrackIndexRef.current = 0;
              addClipsInputRef.current?.click();
            }}
            className="flex h-9 w-9 items-center justify-center rounded-full border border-[#2B352D] bg-[#18110D] text-[#FF5500] hover:border-[#FF5500]"
          >
            <Plus className="h-4 w-4" />
          </button>
          <button
            type="button"
            aria-label="Save draft"
            onClick={handleExplicitSave}
            className="flex h-9 w-9 items-center justify-center rounded-full border border-[#2B352D] bg-[#18110D] text-white hover:border-[#FF5500]"
          >
            <Save className="h-4 w-4" />
          </button>
        </div>
      </div>

      {/* Multi-Track Timeline */}
      <TimelineTracks
        timeline={timeline}
        playheadMs={playheadMs}
        selectedClipId={selectedClip?.id || null}
        selection={selection}
        onChangeSelection={(nextSel) => {
          setSelection(nextSel);
          if (nextSel.clipIds.length > 0) setSelectedClipId(nextSel.clipIds[0]);
          if (nextSel.textIds.length > 0) setSelectedTextId(nextSel.textIds[0]);
        }}
        onClearSelection={() => {
          setSelection(EMPTY_SELECTION);
        }}
        onBatchShift={(deltaMs, trackDelta = 0, overrideSel) => {
          const targetSel = overrideSel ?? selection;
          if (
            targetSel.clipIds.length +
              targetSel.audioIds.length +
              targetSel.voiceoverIds.length +
              targetSel.textIds.length ===
            0
          ) {
            return;
          }
          commitTimeline(batchShiftSelection(timeline, targetSel, deltaMs, trackDelta));
        }}
        onToggleTrackState={(kind, trackIndex, field) =>
          commitTimeline(toggleTrackStateInTimeline(timeline, kind, trackIndex, field))
        }
        onAddTrackLane={(kind) => {
          if (kind === 'video') {
            commitTimeline(addVideoTrack(timeline));
          } else if (kind === 'audio') {
            commitTimeline({
              ...timeline,
              audioTrackCount: Math.min(6, (timeline.audioTrackCount ?? 2) + 1),
            });
          } else {
            commitTimeline({
              ...timeline,
              textTrackCount: Math.min(4, (timeline.textTrackCount ?? 1) + 1),
            });
          }
        }}
        onDeleteVideoTrack={(trackIdx) => {
          const next = deleteVideoTrack(timeline, trackIdx);
          commitTimeline(next);
          setSelection(EMPTY_SELECTION);
          if (selectedClip && !next.clips.some((c) => c.id === selectedClip.id)) {
            setSelectedClipId(next.clips[0]?.id || null);
          }
        }}
        onUploadToVideoTrack={(trackIdx) => {
          uploadTrackIndexRef.current = trackIdx;
          addClipsInputRef.current?.click();
        }}
        onSeek={handleSeek}
        onSelectClip={(cid, additive) => {
          setSelectedClipId(cid);
          if (additive) {
            const exists = selection.clipIds.includes(cid);
            setSelection({
              ...selection,
              clipIds: exists
                ? selection.clipIds.filter((id) => id !== cid)
                : [...selection.clipIds, cid],
            });
          } else {
            setSelection({ ...EMPTY_SELECTION, clipIds: [cid] });
          }
        }}
        onToggleClipMute={(cid) => {
          const target = timeline.clips.find((c) => c.id === cid);
          if (!target) return;
          commitTimeline({
            ...timeline,
            clips: updateClipVolume(timeline.clips, cid, target.volume, !target.isMuted),
          });
        }}
        onOpenMusicTool={() => setActiveTool('music')}
        onOpenTextTool={() => setActiveTool('text')}
        onSelectText={(tid, additive) => {
          setSelectedTextId(tid);
          if (additive) {
            const exists = selection.textIds.includes(tid);
            setSelection({
              ...selection,
              textIds: exists
                ? selection.textIds.filter((id) => id !== tid)
                : [...selection.textIds, tid],
            });
          } else {
            setSelection({ ...EMPTY_SELECTION, textIds: [tid] });
            setActiveTool('text');
          }
        }}
        onOpenVoiceTool={() => setActiveTool('voice')}
      />

      {/* Expandable Tool Sheet */}
      <EditorToolPanel
        activeTool={activeTool}
        timeline={timeline}
        selectedClip={selectedClip}
        selectedTextId={selectedTextId}
        playheadMs={playheadMs}
        bundledMusic={bundledMusic}
        onClose={() => setActiveTool('none')}
        onTrimClip={(cid, startMs, endMs) =>
          commitTimeline({ ...timeline, clips: trimClip(timeline.clips, cid, startMs, endMs) })
        }
        onChangeAspect={(preset) => commitTimeline(applyAspectPresetToClips(timeline, preset))}
        onChangeCrop={(cid, offsetX, offsetY, zoom) =>
          commitTimeline({
            ...timeline,
            clips: updateClipCrop(timeline.clips, cid, { offsetX, offsetY, zoom }),
          })
        }
        onRotateClip={(cid) => {
          const cur = timeline.clips.find((c) => c.id === cid)?.rotationDegrees || 0;
          commitTimeline({
            ...timeline,
            clips: updateClipRotation(timeline.clips, cid, cur + 90),
          });
        }}
        onChangeFilter={(cid, filterPatch) =>
          commitTimeline({
            ...timeline,
            clips: updateClipFilter(timeline.clips, cid, filterPatch),
          })
        }
        onChangeSpeed={(cid, spd, mode) =>
          commitTimeline({
            ...timeline,
            clips: updateClipSpeed(timeline.clips, cid, spd, mode),
          })
        }
        onChangeVolume={(cid, vol, muted) =>
          commitTimeline({
            ...timeline,
            clips: updateClipVolume(timeline.clips, cid, vol, muted),
          })
        }
        onChangeTransition={(cid, tr) =>
          commitTimeline({
            ...timeline,
            clips: updateClipTransition(timeline.clips, cid, tr),
          })
        }
        onSelectMusic={(track) =>
          commitTimeline({
            ...timeline,
            musicTrack: {
              id: `music_${Date.now()}`,
              title: track.title,
              sourceUrl: track.url,
              isBundledAsset: track.isBundled,
              sourceDurationMs: track.durationMs,
              trimStartMs: 0,
              trimEndMs: track.durationMs,
              volume: 0.75,
              isMuted: false,
              loopToFitVideo: true,
              fadeInMs: 300,
              fadeOutMs: 500,
            },
          })
        }
        onUpdateMusic={(patch) => {
          if (!timeline.musicTrack) return;
          commitTimeline({
            ...timeline,
            musicTrack: { ...timeline.musicTrack, ...patch },
          });
        }}
        onRemoveMusic={() => commitTimeline({ ...timeline, musicTrack: null })}
        onUpsertText={(overlay) => {
          const exists = timeline.textOverlays.some((t) => t.id === overlay.id);
          const next = exists
            ? timeline.textOverlays.map((t) => (t.id === overlay.id ? overlay : t))
            : [...timeline.textOverlays, overlay];
          setSelectedTextId(overlay.id);
          commitTimeline({ ...timeline, textOverlays: next });
        }}
        onDeleteText={(id) =>
          commitTimeline({
            ...timeline,
            textOverlays: timeline.textOverlays.filter((t) => t.id !== id),
          })
        }
        onAddVoiceover={(url, durationMs) =>
          commitTimeline({
            ...timeline,
            voiceovers: [
              ...timeline.voiceovers,
              {
                id: `vo_${Date.now()}`,
                title: `Voiceover ${timeline.voiceovers.length + 1}`,
                sourceUrl: url,
                durationMs,
                trimStartMs: 0,
                trimEndMs: durationMs,
                timelineStartMs: playheadMs,
                volume: 1,
                isMuted: false,
              },
            ],
          })
        }
        onDeleteVoiceover={(id) =>
          commitTimeline({
            ...timeline,
            voiceovers: timeline.voiceovers.filter((v) => v.id !== id),
          })
        }
        onReorderClip={(fromIdx, toIdx) =>
          commitTimeline({
            ...timeline,
            clips: reorderClip(timeline.clips, fromIdx, toIdx),
          })
        }
        onDeleteClip={(cid) =>
          commitTimeline({
            ...timeline,
            clips: deleteClip(timeline.clips, cid),
          })
        }
      />

      {/* Bottom Circular Tool Strip */}
      <div className="flex w-full items-center gap-1 overflow-x-auto border-t border-[#2B352D] bg-[#0B0807] px-2 py-2">
        {TOOLS.map((tool) => {
          const Icon = tool.icon;
          const active = activeTool === tool.id;
          const videoTrackCount = getVideoTrackCount(timeline);
          const selCount = selectionSummary(selection).total;
          const canDelete =
            selCount > 0 ||
            (selectedClip !== null && timeline.clips.length > 1) ||
            (activeTool === 'text' && Boolean(selectedTextId));
          const isDisabled =
            (tool.id === 'add_track' && videoTrackCount >= MAX_VIDEO_TRACKS) ||
            (tool.id === 'delete' && !canDelete);
          const label =
            tool.id === 'add_track' && videoTrackCount >= MAX_VIDEO_TRACKS
              ? 'Max 5'
              : tool.label;
          return (
            <button
              key={tool.id}
              type="button"
              disabled={isDisabled}
              onClick={() => {
                const toolId = tool.id;
                if (toolId === 'save') {
                  handleExplicitSave();
                } else if (toolId === 'add_track') {
                  commitTimeline(addVideoTrack(timeline));
                } else if (toolId === 'delete') {
                  if (selCount > 0) {
                    const next = batchDeleteSelection(timeline, selection);
                    commitTimeline(next);
                    setSelection(EMPTY_SELECTION);
                    if (selectedClip && !next.clips.some((c) => c.id === selectedClip.id)) {
                      setSelectedClipId(next.clips[0]?.id || null);
                    }
                  } else if (activeTool === 'text' && selectedTextId) {
                    commitTimeline({
                      ...timeline,
                      textOverlays: timeline.textOverlays.filter((t) => t.id !== selectedTextId),
                    });
                    setSelectedTextId('');
                  } else if (selectedClip && timeline.clips.length > 1) {
                    const remaining = deleteClip(timeline.clips, selectedClip.id);
                    commitTimeline({ ...timeline, clips: remaining });
                    setSelectedClipId(remaining[0]?.id || null);
                  }
                } else if (toolId === 'split') {
                  const split = splitClipAtPlayhead(timeline.clips, playheadMs);
                  if (split) {
                    commitTimeline({ ...timeline, clips: split });
                  }
                } else {
                  setActiveTool((cur) => (cur === toolId ? 'none' : toolId));
                }
              }}
              className={`flex shrink-0 flex-col items-center gap-1.5 rounded-xl px-2.5 py-1 ${
                isDisabled ? 'cursor-not-allowed opacity-40' : ''
              }`}
            >
              <span
                className={`flex h-11 w-11 items-center justify-center rounded-full border transition-colors ${
                  active
                    ? 'border-[#FF8232] bg-[#FF5500] text-white'
                    : tool.id === 'delete' && canDelete
                      ? 'border-[#FF5A5A]/60 bg-[#18110D] text-[#FF5A5A] hover:border-[#FF5A5A]'
                      : 'border-[#2B352D] bg-[#18110D] text-white hover:border-[#FF5500]'
                }`}
              >
                <Icon className="h-[18px] w-[18px]" />
              </span>
              <span
                className={`text-[11px] font-medium ${
                  active
                    ? 'text-[#FF8232]'
                    : tool.id === 'delete' && canDelete
                      ? 'text-[#FF5A5A]'
                      : 'text-white'
                }`}
              >
                {label}
              </span>
            </button>
          );
        })}
      </div>

      <input
        ref={addClipsInputRef}
        type="file"
        accept="video/*,image/*"
        multiple
        className="hidden"
        onChange={(e) => {
          if (e.target.files && e.target.files.length > 0) {
            handleAddMoreFiles(e.target.files);
          }
          e.target.value = '';
        }}
      />
    </div>
  );
}
