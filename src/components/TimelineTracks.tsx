import React, { useMemo, useRef, useState } from 'react';
import {
  Eye,
  EyeOff,
  Lock,
  Plus,
  Trash2,
  Unlock,
  Volume2,
  VolumeX,
} from 'lucide-react';
import {
  EMPTY_SELECTION,
  formatShortTimecode,
  getClipEffectiveDurationMs,
  getTimelineTotalDurationMs,
  getTrackState,
  getVideoTrackCount,
  marqueeSelectTimeline,
  selectionSummary,
} from '../engine/timelineMath';
import type { TimelineSelection, TimelineState, TrackKind } from '../types/editor';

const PX_PER_SECOND = 44;
const TIMELINE_PAD_LEFT = 10;

interface TrackRowMeta {
  key: string;
  kind: TrackKind;
  trackIndex: number;
  label: string;
  heightPx: number;
  badgeClass: string;
}

interface MarqueeBox {
  startX: number;
  startY: number;
  curX: number;
  curY: number;
}

interface DragClipSession {
  itemType: 'clip' | 'music' | 'voiceover' | 'text';
  itemId: string;
  originClientX: number;
  originClientY: number;
  dragSelection: TimelineSelection;
}

function WaveformSvg({
  widthPx,
  seedStr,
  strokeColor,
}: {
  widthPx: number;
  seedStr: string;
  strokeColor: string;
}) {
  const barCount = Math.max(10, Math.min(180, Math.floor(widthPx / 4)));
  let seed = 0;
  for (let i = 0; i < seedStr.length; i++) {
    seed = (seed * 31 + seedStr.charCodeAt(i)) | 0;
  }
  const bars = [];
  for (let i = 0; i < barCount; i++) {
    const val = Math.abs(Math.sin(i * 0.65 + (seed % 97) * 0.13));
    const hPct = 22 + Math.round(val * 70);
    bars.push(
      <div
        key={i}
        style={{ height: `${hPct}%`, backgroundColor: strokeColor }}
        className="w-[2px] shrink-0 rounded-t-sm opacity-65"
      />
    );
  }
  return (
    <div className="pointer-events-none absolute inset-x-1 bottom-0.5 top-2 flex items-end gap-[2px] overflow-hidden">
      {bars}
    </div>
  );
}

export function TimelineTracks({
  timeline,
  playheadMs,
  selectedClipId,
  selection,
  onChangeSelection,
  onClearSelection,
  onBatchShift,
  onToggleTrackState,
  onAddTrackLane,
  onDeleteVideoTrack,
  onUploadToVideoTrack,
  onSeek,
  onSelectClip,
  onToggleClipMute,
  onSelectText,
  onOpenMusicTool,
  onOpenTextTool,
  onOpenVoiceTool,
}: {
  timeline: TimelineState;
  playheadMs: number;
  selectedClipId: string | null;
  selection: TimelineSelection;
  onChangeSelection: (sel: TimelineSelection) => void;
  onClearSelection: () => void;
  onBatchShift: (deltaMs: number, trackDelta?: number, overrideSel?: TimelineSelection) => void;
  onToggleTrackState: (
    kind: TrackKind,
    trackIndex: number,
    field: 'locked' | 'muted' | 'hidden' | 'solo'
  ) => void;
  onAddTrackLane: (kind: TrackKind) => void;
  onDeleteVideoTrack: (trackIndex: number) => void;
  onUploadToVideoTrack: (trackIndex: number) => void;
  onSeek: (ms: number) => void;
  onSelectClip: (clipId: string, additive?: boolean) => void;
  onToggleClipMute: (clipId: string) => void;
  onSelectText: (textId: string, additive?: boolean) => void;
  onOpenMusicTool: () => void;
  onOpenTextTool: () => void;
  onOpenVoiceTool: () => void;
}) {
  const lanesRef = useRef<HTMLDivElement | null>(null);
  const rowRefs = useRef<Record<string, HTMLDivElement | null>>({});
  const scrubbingRef = useRef(false);
  const [marquee, setMarquee] = useState<MarqueeBox | null>(null);
  const marqueeRef = useRef<MarqueeBox | null>(null);
  const dragClipRef = useRef<DragClipSession | null>(null);
  const [dragOffset, setDragOffset] = useState<{ dx: number; dy: number } | null>(null);
  const didDragMoveRef = useRef(false);

  const maxTextTrack = timeline.textOverlays.reduce((m, t) => Math.max(m, t.trackIndex ?? 0), 0);
  const maxAudioTrack = Math.max(
    timeline.musicTrack?.trackIndex ?? 0,
    timeline.voiceovers.reduce((m, v) => Math.max(m, v.trackIndex ?? 1), 1)
  );

  const videoTrackCount = getVideoTrackCount(timeline);
  const textTrackCount = Math.max(1, timeline.textTrackCount ?? 1, maxTextTrack + 1);
  const audioTrackCount = Math.max(2, timeline.audioTrackCount ?? 2, maxAudioTrack + 1);

  const { videoTextRows, audioRows, allRows } = useMemo(() => {
    const vt: TrackRowMeta[] = [];
    for (let i = videoTrackCount - 1; i >= 0; i--) {
      vt.push({
        key: `video-${i}`,
        kind: 'video',
        trackIndex: i,
        label: `V${i + 1}`,
        heightPx: i === 0 ? 44 : 38,
        badgeClass: 'text-[#82B8FF]',
      });
    }
    for (let i = textTrackCount - 1; i >= 0; i--) {
      vt.push({
        key: `text-${i}`,
        kind: 'text',
        trackIndex: i,
        label: `T${i + 1}`,
        heightPx: 30,
        badgeClass: 'text-[#FF8232]',
      });
    }
    const au: TrackRowMeta[] = [];
    for (let i = 0; i < audioTrackCount; i++) {
      au.push({
        key: `audio-${i}`,
        kind: 'audio',
        trackIndex: i,
        label: `A${i + 1}`,
        heightPx: 36,
        badgeClass: i === 0 ? 'text-[#34D399]' : 'text-[#82B8FF]',
      });
    }
    return { videoTextRows: vt, audioRows: au, allRows: [...vt, ...au] };
  }, [videoTrackCount, textTrackCount, audioTrackCount]);

  const totalMs = Math.max(4000, getTimelineTotalDurationMs(timeline));
  const totalSec = Math.max(4, Math.ceil(totalMs / 1000) + 1);
  const trackWidthPx = Math.max(360, TIMELINE_PAD_LEFT + totalSec * PX_PER_SECOND + 24);
  const playheadLeftPx = TIMELINE_PAD_LEFT + (playheadMs / 1000) * PX_PER_SECOND;
  const summary = selectionSummary(selection);

  const seekFromClientX = (clientX: number) => {
    const el = lanesRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const x = clientX - rect.left + el.scrollLeft - TIMELINE_PAD_LEFT;
    const ms = Math.max(0, Math.min(totalMs, Math.round((x / PX_PER_SECOND) * 1000)));
    onSeek(ms);
  };

  const computeMarqueeSelection = (box: MarqueeBox) => {
    const container = lanesRef.current;
    if (!container) return;
    const containerRect = container.getBoundingClientRect();
    const loX = Math.min(box.startX, box.curX) - TIMELINE_PAD_LEFT;
    const hiX = Math.max(box.startX, box.curX) - TIMELINE_PAD_LEFT;
    const loY = Math.min(box.startY, box.curY);
    const hiY = Math.max(box.startY, box.curY);

    const startMs = Math.max(0, Math.round((loX / PX_PER_SECOND) * 1000));
    const endMs = Math.max(startMs + 40, Math.round((hiX / PX_PER_SECOND) * 1000));

    const hitRows: Array<{ kind: TrackKind; trackIndex: number }> = [];
    for (const row of allRows) {
      const rowEl = rowRefs.current[row.key];
      if (!rowEl) continue;
      const rRect = rowEl.getBoundingClientRect();
      const rowTop = rRect.top - containerRect.top + container.scrollTop;
      const rowBottom = rowTop + rRect.height;
      if (rowBottom >= loY && rowTop <= hiY) {
        hitRows.push({ kind: row.kind, trackIndex: row.trackIndex });
      }
    }

    const nextSel = marqueeSelectTimeline(timeline, startMs, endMs, hitRows);
    onChangeSelection(nextSel);
  };

  const renderTrackHeader = (row: TrackRowMeta) => {
    const st = getTrackState(timeline, row.kind, row.trackIndex);
    return (
      <div
        key={row.key}
        style={{ height: `${row.heightPx}px` }}
        className="flex shrink-0 items-center justify-between rounded-md border border-[#2B352D] bg-[#18110D] px-1.5"
      >
        <span className={`text-[10px] font-bold tracking-tight ${row.badgeClass}`}>{row.label}</span>
        <div className="flex items-center gap-1">
          <button
            type="button"
            title={st.locked ? `Unlock ${row.label}` : `Lock ${row.label}`}
            aria-label={`Toggle lock ${row.label}`}
            onClick={() => onToggleTrackState(row.kind, row.trackIndex, 'locked')}
            className={`flex h-5 w-5 items-center justify-center rounded text-[10px] transition-colors ${
              st.locked ? 'bg-[#FF5500]/20 text-[#FF5500]' : 'text-[#667568] hover:text-white'
            }`}
          >
            {st.locked ? <Lock className="h-3 w-3" /> : <Unlock className="h-3 w-3" />}
          </button>

          {row.kind === 'audio' ? (
            <>
              <button
                type="button"
                title={`Mute ${row.label}`}
                aria-label={`Toggle mute ${row.label}`}
                onClick={() => onToggleTrackState('audio', row.trackIndex, 'muted')}
                className={`flex h-5 w-4 items-center justify-center rounded text-[10px] font-bold ${
                  st.muted ? 'bg-[#FF5A5A]/25 text-[#FF5A5A]' : 'text-[#9AA69B] hover:text-white'
                }`}
              >
                M
              </button>
              <button
                type="button"
                title={`Solo ${row.label}`}
                aria-label={`Toggle solo ${row.label}`}
                onClick={() => onToggleTrackState('audio', row.trackIndex, 'solo')}
                className={`flex h-5 w-4 items-center justify-center rounded text-[10px] font-bold ${
                  st.solo ? 'bg-[#FF8232]/25 text-[#FF8232]' : 'text-[#9AA69B] hover:text-white'
                }`}
              >
                S
              </button>
            </>
          ) : (
            <>
              <button
                type="button"
                title={st.hidden ? `Show ${row.label}` : `Hide ${row.label}`}
                aria-label={`Toggle visibility ${row.label}`}
                onClick={() => onToggleTrackState(row.kind, row.trackIndex, 'hidden')}
                className={`flex h-5 w-5 items-center justify-center rounded ${
                  st.hidden ? 'bg-[#221711] text-[#667568]' : 'text-[#9AA69B] hover:text-white'
                }`}
              >
                {st.hidden ? <EyeOff className="h-3 w-3" /> : <Eye className="h-3 w-3" />}
              </button>
              {row.kind === 'video' && row.trackIndex >= 1 && (
                <button
                  type="button"
                  title={`Delete ${row.label} and its videos`}
                  aria-label={`Delete track ${row.label}`}
                  onClick={() => onDeleteVideoTrack(row.trackIndex)}
                  className="flex h-5 w-5 items-center justify-center rounded text-[#9AA69B] transition-colors hover:bg-[#FF5A5A]/20 hover:text-[#FF5A5A]"
                >
                  <Trash2 className="h-3 w-3" />
                </button>
              )}
            </>
          )}
        </div>
      </div>
    );
  };

  const renderTrackLane = (row: TrackRowMeta) => {
    const st = getTrackState(timeline, row.kind, row.trackIndex);

    return (
      <div
        key={row.key}
        ref={(el) => {
          rowRefs.current[row.key] = el;
        }}
        data-track-kind={row.kind}
        data-track-index={row.trackIndex}
        style={{ height: `${row.heightPx}px` }}
        className={`relative w-full shrink-0 rounded-md border bg-[#140E0B] transition-opacity ${
          st.locked ? 'border-[#FF5500]/40 bg-[#18100C]' : 'border-[#242D26]'
        } ${st.hidden || st.muted ? 'opacity-45' : 'opacity-100'}`}
      >
        {row.kind === 'video' && (
          <>
            {row.trackIndex >= 1 &&
              timeline.clips.filter((c) => (c.trackIndex ?? 0) === row.trackIndex).length === 0 && (
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation();
                    onUploadToVideoTrack(row.trackIndex);
                  }}
                  className="ml-2 mt-1.5 inline-flex items-center gap-1 rounded px-1.5 py-0.5 text-[10px] text-[#9AA69B] hover:bg-[#221711] hover:text-white"
                >
                  <Plus className="h-3 w-3 text-[#82B8FF]" />
                  <span>Upload video</span>
                </button>
              )}
            {timeline.clips
              .filter((c) => (c.trackIndex ?? 0) === row.trackIndex)
              .map((clip) => {
              const effMs = getClipEffectiveDurationMs(clip);
              const leftPx = TIMELINE_PAD_LEFT + (clip.timelineStartMs / 1000) * PX_PER_SECOND;
              const widthPx = Math.max(48, Math.round((effMs / 1000) * PX_PER_SECOND));
              const isSelected =
                selection.clipIds.includes(clip.id) ||
                (summary.total === 0 && clip.id === selectedClipId);

              const isDraggingThis = Boolean(
                dragOffset && dragClipRef.current?.dragSelection.clipIds.includes(clip.id)
              );

              return (
                <div
                  key={clip.id}
                  data-timeline-item="clip"
                  data-item-id={clip.id}
                  style={{
                    left: `${leftPx}px`,
                    width: `${widthPx}px`,
                    transform: isDraggingThis
                      ? `translate(${dragOffset!.dx}px, ${dragOffset!.dy}px)`
                      : undefined,
                  }}
                  onPointerDown={(e) => {
                    if ((e.target as HTMLElement).closest('button')) return;
                    e.stopPropagation();
                    didDragMoveRef.current = false;
                    const additive = e.shiftKey || e.metaKey || e.ctrlKey;
                    const inSelection = selection.clipIds.includes(clip.id);
                    if (!additive && !inSelection) {
                      onSelectClip(clip.id, false);
                    }
                    if (!st.locked) {
                      dragClipRef.current = {
                        itemType: 'clip',
                        itemId: clip.id,
                        originClientX: e.clientX,
                        originClientY: e.clientY,
                        dragSelection: inSelection
                          ? selection
                          : { ...EMPTY_SELECTION, clipIds: [clip.id] },
                      };
                    }
                  }}
                  onClick={(e) => {
                    e.stopPropagation();
                    if (didDragMoveRef.current) return;
                    const additive = e.shiftKey || e.metaKey || e.ctrlKey;
                    onSelectClip(clip.id, additive);
                  }}
                  className={`group absolute bottom-0.5 top-0.5 cursor-grab overflow-hidden rounded-[7px] bg-[#1E2530] active:cursor-grabbing ${
                    isSelected
                      ? 'z-10 border-2 border-[#FF5500] shadow-[0_0_0_1px_rgba(255,85,0,0.45)]'
                      : 'border border-[#384659]'
                  }`}
                >
                  {clip.thumbnails[0] ? (
                    <img
                      src={clip.thumbnails[0]}
                      alt={clip.displayName}
                      draggable={false}
                      className="pointer-events-none h-full w-full object-cover opacity-80"
                    />
                  ) : (
                    <div className="h-full w-full bg-[#221711]" />
                  )}
                  <div className="pointer-events-none absolute inset-0 bg-gradient-to-t from-black/75 via-transparent to-black/25" />
                  <span className="pointer-events-none absolute bottom-0.5 left-1.5 right-6 truncate text-[10px] font-medium text-white">
                    {clip.displayName}
                  </span>
                  <button
                    type="button"
                    aria-label={clip.isMuted ? `Unmute ${clip.displayName}` : `Mute ${clip.displayName}`}
                    onClick={(e) => {
                      e.stopPropagation();
                      onToggleClipMute(clip.id);
                    }}
                    className="absolute right-1 top-1 flex h-4 w-4 items-center justify-center rounded-full bg-black/70 text-white hover:bg-[#FF5500]"
                  >
                    {clip.isMuted ? (
                      <VolumeX className="h-2.5 w-2.5 text-[#FF5A5A]" />
                    ) : (
                      <Volume2 className="h-2.5 w-2.5" />
                    )}
                  </button>
                </div>
              );
            })}
          </>
        )}

        {row.kind === 'text' && (
          <>
            {timeline.textOverlays.filter((t) => (t.trackIndex ?? 0) === row.trackIndex).length ===
              0 && (
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  onOpenTextTool();
                }}
                className="ml-2 mt-1 inline-flex items-center gap-1 rounded px-1.5 py-0.5 text-[10px] text-[#9AA69B] hover:bg-[#221711] hover:text-white"
              >
                <Plus className="h-3 w-3 text-[#FF8232]" />
                <span>Add text</span>
              </button>
            )}
            {timeline.textOverlays
              .filter((t) => (t.trackIndex ?? 0) === row.trackIndex)
              .map((ov) => {
                const leftPx = TIMELINE_PAD_LEFT + (ov.startTimeMs / 1000) * PX_PER_SECOND;
                const widthPx = Math.max(
                  58,
                  ((ov.endTimeMs - ov.startTimeMs) / 1000) * PX_PER_SECOND
                );
                const isSelected = selection.textIds.includes(ov.id);
                const isDraggingThis = Boolean(
                  dragOffset && dragClipRef.current?.dragSelection.textIds.includes(ov.id)
                );
                return (
                  <div
                    key={ov.id}
                    data-timeline-item="text"
                    data-item-id={ov.id}
                    style={{
                      left: `${leftPx}px`,
                      width: `${widthPx}px`,
                      transform: isDraggingThis ? `translateX(${dragOffset!.dx}px)` : undefined,
                    }}
                    onPointerDown={(e) => {
                      e.stopPropagation();
                      didDragMoveRef.current = false;
                      const inSelection = selection.textIds.includes(ov.id);
                      if (!st.locked) {
                        dragClipRef.current = {
                          itemType: 'text',
                          itemId: ov.id,
                          originClientX: e.clientX,
                          originClientY: e.clientY,
                          dragSelection: inSelection
                            ? selection
                            : { ...EMPTY_SELECTION, textIds: [ov.id] },
                        };
                      }
                    }}
                    onClick={(e) => {
                      e.stopPropagation();
                      if (didDragMoveRef.current) return;
                      const additive = e.shiftKey || e.metaKey || e.ctrlKey;
                      onSelectText(ov.id, additive);
                    }}
                    className={`absolute bottom-0.5 top-0.5 flex cursor-grab items-center rounded-md bg-[#2A1C12] px-2 text-[10px] font-medium text-white active:cursor-grabbing ${
                      isSelected
                        ? 'z-10 border-2 border-[#FF5500]'
                        : 'border border-[#FF8232]/75'
                    }`}
                  >
                    <span className="truncate">{ov.text}</span>
                  </div>
                );
              })}
          </>
        )}

        {row.kind === 'audio' && (
          <>
            {timeline.musicTrack && (timeline.musicTrack.trackIndex ?? 0) === row.trackIndex && (() => {
              const m = timeline.musicTrack;
              const startMs = m.timelineStartMs ?? 0;
              const spanMs = m.loopToFitVideo
                ? Math.max(m.trimEndMs - m.trimStartMs, totalMs - startMs)
                : Math.max(400, m.trimEndMs - m.trimStartMs);
              const leftPx = TIMELINE_PAD_LEFT + (startMs / 1000) * PX_PER_SECOND;
              const widthPx = Math.max(64, Math.round((spanMs / 1000) * PX_PER_SECOND));
              const isSelected = selection.audioIds.includes(m.id);
              const isDraggingThis = Boolean(
                dragOffset && dragClipRef.current?.dragSelection.audioIds.includes(m.id)
              );
              return (
                <div
                  key={m.id}
                  data-timeline-item="music"
                  data-item-id={m.id}
                  style={{
                    left: `${leftPx}px`,
                    width: `${widthPx}px`,
                    transform: isDraggingThis ? `translateX(${dragOffset!.dx}px)` : undefined,
                  }}
                  onPointerDown={(e) => {
                    e.stopPropagation();
                    didDragMoveRef.current = false;
                    const inSelection = selection.audioIds.includes(m.id);
                    if (!st.locked) {
                      dragClipRef.current = {
                        itemType: 'music',
                        itemId: m.id,
                        originClientX: e.clientX,
                        originClientY: e.clientY,
                        dragSelection: inSelection
                          ? selection
                          : { ...EMPTY_SELECTION, audioIds: [m.id] },
                      };
                    }
                  }}
                  onClick={(e) => {
                    e.stopPropagation();
                    if (didDragMoveRef.current) return;
                    const additive = e.shiftKey || e.metaKey || e.ctrlKey;
                    if (additive) {
                      const next = isSelected
                        ? selection.audioIds.filter((id) => id !== m.id)
                        : [...selection.audioIds, m.id];
                      onChangeSelection({ ...selection, audioIds: next });
                    } else {
                      onChangeSelection({ ...EMPTY_SELECTION, audioIds: [m.id] });
                      onOpenMusicTool();
                    }
                  }}
                  className={`absolute bottom-0.5 top-0.5 cursor-grab overflow-hidden rounded-md bg-[#112A22] px-2 active:cursor-grabbing ${
                    isSelected
                      ? 'z-10 border-2 border-[#FF5500]'
                      : 'border border-[#2FA37A]'
                  }`}
                >
                  <WaveformSvg widthPx={widthPx} seedStr={m.title} strokeColor="#34D399" />
                  <span className="relative z-10 block truncate pt-0.5 text-[10px] font-medium text-white drop-shadow">
                    {m.title} {m.isMuted ? '(Muted)' : `(${Math.round(m.volume * 100)}%)`}
                  </span>
                </div>
              );
            })()}

            {timeline.voiceovers
              .filter((v) => (v.trackIndex ?? 1) === row.trackIndex)
              .map((vo) => {
                const spanMs = Math.max(400, vo.trimEndMs - vo.trimStartMs);
                const leftPx = TIMELINE_PAD_LEFT + (vo.timelineStartMs / 1000) * PX_PER_SECOND;
                const widthPx = Math.max(58, Math.round((spanMs / 1000) * PX_PER_SECOND));
                const isSelected = selection.voiceoverIds.includes(vo.id);
                const isDraggingThis = Boolean(
                  dragOffset && dragClipRef.current?.dragSelection.voiceoverIds.includes(vo.id)
                );
                return (
                  <div
                    key={vo.id}
                    data-timeline-item="voiceover"
                    data-item-id={vo.id}
                    style={{
                      left: `${leftPx}px`,
                      width: `${widthPx}px`,
                      transform: isDraggingThis ? `translateX(${dragOffset!.dx}px)` : undefined,
                    }}
                    onPointerDown={(e) => {
                      e.stopPropagation();
                      didDragMoveRef.current = false;
                      const inSelection = selection.voiceoverIds.includes(vo.id);
                      if (!st.locked) {
                        dragClipRef.current = {
                          itemType: 'voiceover',
                          itemId: vo.id,
                          originClientX: e.clientX,
                          originClientY: e.clientY,
                          dragSelection: inSelection
                            ? selection
                            : { ...EMPTY_SELECTION, voiceoverIds: [vo.id] },
                        };
                      }
                    }}
                    onClick={(e) => {
                      e.stopPropagation();
                      if (didDragMoveRef.current) return;
                      const additive = e.shiftKey || e.metaKey || e.ctrlKey;
                      if (additive) {
                        const next = isSelected
                          ? selection.voiceoverIds.filter((id) => id !== vo.id)
                          : [...selection.voiceoverIds, vo.id];
                        onChangeSelection({ ...selection, voiceoverIds: next });
                      } else {
                        onChangeSelection({ ...EMPTY_SELECTION, voiceoverIds: [vo.id] });
                        onOpenVoiceTool();
                      }
                    }}
                    className={`absolute bottom-0.5 top-0.5 cursor-grab overflow-hidden rounded-md bg-[#132238] px-2 active:cursor-grabbing ${
                      isSelected
                        ? 'z-10 border-2 border-[#FF5500]'
                        : 'border border-[#82B8FF]/80'
                    }`}
                  >
                    <WaveformSvg widthPx={widthPx} seedStr={vo.title} strokeColor="#82B8FF" />
                    <span className="relative z-10 block truncate pt-0.5 text-[10px] font-medium text-white drop-shadow">
                      {vo.title} {vo.isMuted ? '(Muted)' : ''}
                    </span>
                  </div>
                );
              })}

            {!timeline.musicTrack && row.trackIndex === 0 && (
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  onOpenMusicTool();
                }}
                className="ml-2 mt-1.5 inline-flex items-center gap-1 rounded px-1.5 py-0.5 text-[10px] text-[#9AA69B] hover:bg-[#221711] hover:text-white"
              >
                <Plus className="h-3 w-3 text-[#FF5500]" />
                <span>Add audio</span>
              </button>
            )}

            {timeline.voiceovers.length === 0 && row.trackIndex === 1 && (
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  onOpenVoiceTool();
                }}
                className="ml-2 mt-1.5 inline-flex items-center gap-1 rounded px-1.5 py-0.5 text-[10px] text-[#9AA69B] hover:bg-[#221711] hover:text-white"
              >
                <Plus className="h-3 w-3 text-[#82B8FF]" />
                <span>Add voiceover</span>
              </button>
            )}
          </>
        )}
      </div>
    );
  };

  return (
    <div className="w-full border-t border-[#2B352D] bg-[#0B0807] px-2.5 py-2">
      {/* Split Multi-Track Workspace */}
      <div className="flex max-h-[240px] w-full items-start overflow-y-auto overflow-x-hidden">
        {/* Left Sticky Track Header Column */}
        <div className="mr-1.5 flex w-[96px] shrink-0 flex-col gap-1 self-start sm:w-[110px]">
          <div className="flex h-[22px] shrink-0 items-center justify-between px-1">
            <span className="text-[9px] font-bold tracking-wider text-[#667568]">
              TRACKS ({videoTrackCount}/5)
            </span>
            <button
              type="button"
              title="Add Audio Track"
              onClick={() => onAddTrackLane('audio')}
              className="rounded px-1 text-[9px] font-bold text-[#34D399] hover:bg-[#18110D]"
            >
              +A
            </button>
          </div>

          {videoTextRows.map(renderTrackHeader)}
          <div className="my-0.5 h-[1px] w-full shrink-0 bg-[#2B352D]" />
          {audioRows.map(renderTrackHeader)}
        </div>

        {/* Right Scrollable Multi-Track Canvas with Marquee Select */}
        <div
          ref={lanesRef}
          data-testid="timeline-lanes-canvas"
          onPointerDown={(e) => {
            if ((e.target as HTMLElement).closest('button')) return;
            const el = lanesRef.current;
            if (!el) return;
            const rect = el.getBoundingClientRect();
            const relY = e.clientY - rect.top;
            const relX = e.clientX - rect.left + el.scrollLeft;

            // Ruler scrub if pointer is in top 22px
            if (relY <= 22) {
              scrubbingRef.current = true;
              seekFromClientX(e.clientX);
              return;
            }

            // Otherwise start marquee selection box in empty lane space
            const box: MarqueeBox = {
              startX: relX,
              startY: relY + el.scrollTop,
              curX: relX,
              curY: relY + el.scrollTop,
            };
            marqueeRef.current = box;
            setMarquee(box);
          }}
          onPointerMove={(e) => {
            if (scrubbingRef.current) {
              seekFromClientX(e.clientX);
              return;
            }

            const drag = dragClipRef.current;
            if (drag) {
              const dx = e.clientX - drag.originClientX;
              const dy = e.clientY - drag.originClientY;
              if (Math.abs(dx) > 4 || Math.abs(dy) > 4) {
                didDragMoveRef.current = true;
              }
              setDragOffset({ dx, dy: drag.itemType === 'clip' ? dy : 0 });
              return;
            }

            if (marqueeRef.current && lanesRef.current) {
              const rect = lanesRef.current.getBoundingClientRect();
              const nextBox: MarqueeBox = {
                ...marqueeRef.current,
                curX: e.clientX - rect.left + lanesRef.current.scrollLeft,
                curY: e.clientY - rect.top + lanesRef.current.scrollTop,
              };
              marqueeRef.current = nextBox;
              setMarquee(nextBox);
              if (
                Math.abs(nextBox.curX - nextBox.startX) > 6 ||
                Math.abs(nextBox.curY - nextBox.startY) > 6
              ) {
                computeMarqueeSelection(nextBox);
              }
            }
          }}
          onPointerUp={(e) => {
            if (scrubbingRef.current) {
              scrubbingRef.current = false;
              return;
            }
            if (dragClipRef.current) {
              const drag = dragClipRef.current;
              const dx = e.clientX - drag.originClientX;
              const dy = e.clientY - drag.originClientY;
              const deltaMs = Math.round(((dx / PX_PER_SECOND) * 1000) / 50) * 50;
              const trackDelta = drag.itemType === 'clip' ? Math.round(-dy / 40) : 0;
              dragClipRef.current = null;
              setDragOffset(null);
              if (didDragMoveRef.current && (deltaMs !== 0 || trackDelta !== 0)) {
                onBatchShift(deltaMs, trackDelta, drag.dragSelection);
              }
              return;
            }
            if (marqueeRef.current) {
              const box = marqueeRef.current;
              const dragged =
                Math.abs(box.curX - box.startX) > 6 || Math.abs(box.curY - box.startY) > 6;
              if (!dragged) {
                seekFromClientX(e.clientX);
                if (!e.shiftKey) {
                  onClearSelection();
                }
              } else {
                computeMarqueeSelection(box);
              }
              marqueeRef.current = null;
              setMarquee(null);
            }
          }}
          onPointerLeave={() => {
            scrubbingRef.current = false;
            if (dragClipRef.current) {
              dragClipRef.current = null;
              setDragOffset(null);
            }
            if (marqueeRef.current) {
              marqueeRef.current = null;
              setMarquee(null);
            }
          }}
          className="relative flex-1 overflow-x-auto pb-1 select-none"
        >
          <div style={{ width: `${trackWidthPx}px` }} className="relative flex flex-col gap-1">
            {/* Time Ruler */}
            <div
              style={{ paddingLeft: `${TIMELINE_PAD_LEFT}px` }}
              className="flex h-[22px] shrink-0 cursor-ew-resize items-end border-b border-[#242D26] pb-0.5"
            >
              {Array.from({ length: totalSec + 1 }).map((_, sec) => (
                <div
                  key={sec}
                  style={{ width: `${PX_PER_SECOND}px` }}
                  className="shrink-0 text-[10px] leading-none text-[#667568] font-mono-tabular"
                >
                  {formatShortTimecode(sec * 1000)}
                </div>
              ))}
            </div>

            {videoTextRows.map(renderTrackLane)}
            <div className="my-0.5 h-[1px] w-full shrink-0 bg-[#2B352D]" />
            {audioRows.map(renderTrackLane)}

            {/* Marquee Selection Box */}
            {marquee && (
              <div
                data-testid="marquee-box"
                style={{
                  left: `${Math.min(marquee.startX, marquee.curX)}px`,
                  top: `${Math.min(marquee.startY, marquee.curY)}px`,
                  width: `${Math.max(2, Math.abs(marquee.curX - marquee.startX))}px`,
                  height: `${Math.max(2, Math.abs(marquee.curY - marquee.startY))}px`,
                }}
                className="pointer-events-none absolute z-30 rounded border border-dashed border-[#FF5500] bg-[#FF5500]/15"
              />
            )}

            {/* Draggable White Playhead Line + Top Cap */}
            <div
              style={{ left: `${playheadLeftPx}px` }}
              onPointerDown={(e) => {
                e.stopPropagation();
                scrubbingRef.current = true;
              }}
              className="pointer-events-auto absolute bottom-0 top-0 z-20 flex w-4 -translate-x-1/2 cursor-ew-resize items-center justify-center"
            >
              <div className="h-full w-[2px] bg-white shadow" />
              <div className="absolute top-0 h-2 w-2.5 rounded-b-sm border border-[#0B0807] bg-white" />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
