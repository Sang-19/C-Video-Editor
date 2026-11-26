import React, { useEffect, useRef, useState } from 'react';
import { ArrowDown, ArrowUp, Mic, Square, Trash2, Upload } from 'lucide-react';
import { saveMediaBlob } from '../storage/projectDb';
import {
  ASPECT_PRESETS,
  DEFAULT_CLIP_FILTER,
  type AspectRatioPresetId,
  type AudioSpeedMode,
  type BundledMusicTrack,
  type ClipFilterConfig,
  type ClipTransitionConfig,
  type EditorToolId,
  type TextAlignment,
  type TextAnimation,
  type TextOverlayItem,
  type TimelineState,
  type TransitionType,
  type VideoClipItem,
} from '../types/editor';

export function EditorToolPanel({
  activeTool,
  timeline,
  selectedClip,
  selectedTextId,
  playheadMs,
  bundledMusic,
  onClose,
  onTrimClip,
  onChangeAspect,
  onChangeCrop,
  onRotateClip,
  onChangeFilter,
  onChangeSpeed,
  onChangeVolume,
  onChangeTransition,
  onSelectMusic,
  onUpdateMusic,
  onRemoveMusic,
  onUpsertText,
  onDeleteText,
  onAddVoiceover,
  onDeleteVoiceover,
  onReorderClip,
  onDeleteClip,
}: {
  activeTool: EditorToolId;
  timeline: TimelineState;
  selectedClip: VideoClipItem | null;
  selectedTextId: string;
  playheadMs: number;
  bundledMusic: BundledMusicTrack[];
  onClose: () => void;
  onTrimClip: (clipId: string, startMs: number, endMs: number) => void;
  onChangeAspect: (preset: AspectRatioPresetId) => void;
  onChangeCrop: (clipId: string, offsetX: number, offsetY: number, zoom: number) => void;
  onRotateClip: (clipId: string) => void;
  onChangeFilter: (clipId: string, filter: Partial<ClipFilterConfig>) => void;
  onChangeSpeed: (clipId: string, speed: number, audioMode: AudioSpeedMode) => void;
  onChangeVolume: (clipId: string, volume: number, isMuted: boolean) => void;
  onChangeTransition: (clipId: string, transition: ClipTransitionConfig) => void;
  onSelectMusic: (track: {
    title: string;
    url: string;
    isBundled: boolean;
    durationMs: number;
  }) => void;
  onUpdateMusic: (patch: {
    trimStartMs?: number;
    trimEndMs?: number;
    volume?: number;
    isMuted?: boolean;
    loopToFitVideo?: boolean;
    fadeInMs?: number;
    fadeOutMs?: number;
  }) => void;
  onRemoveMusic: () => void;
  onUpsertText: (overlay: TextOverlayItem) => void;
  onDeleteText: (id: string) => void;
  onAddVoiceover: (url: string, durationMs: number) => void;
  onDeleteVoiceover: (id: string) => void;
  onReorderClip: (fromIdx: number, toIdx: number) => void;
  onDeleteClip: (clipId: string) => void;
}) {
  const audioFileRef = useRef<HTMLInputElement | null>(null);
  const [recordingVo, setRecordingVo] = useState(false);
  const [voSeconds, setVoSeconds] = useState(0);
  const voRecorderRef = useRef<MediaRecorder | null>(null);
  const voStreamRef = useRef<MediaStream | null>(null);

  const editingText =
    timeline.textOverlays.find((t) => t.id === selectedTextId) ||
    timeline.textOverlays[0] ||
    null;

  const [textVal, setTextVal] = useState(editingText?.text || 'TRISVAX');
  const [textColor, setTextColor] = useState(editingText?.colorHex || '#FFFFFF');
  const [textSize, setTextSize] = useState(editingText?.fontSizePx || 28);
  const [textAlign, setTextAlign] = useState<TextAlignment>(editingText?.alignment || 'center');
  const [textAnim, setTextAnim] = useState<TextAnimation>(editingText?.animation || 'none');
  const [textY, setTextY] = useState(editingText?.normalizedY ?? 0.5);
  const [textStartMs, setTextStartMs] = useState(editingText?.startTimeMs ?? playheadMs);
  const [textEndMs, setTextEndMs] = useState(editingText?.endTimeMs ?? playheadMs + 3500);

  useEffect(() => {
    setTextVal(editingText?.text || 'TRISVAX');
    setTextColor(editingText?.colorHex || '#FFFFFF');
    setTextSize(editingText?.fontSizePx || 28);
    setTextAlign(editingText?.alignment || 'center');
    setTextAnim(editingText?.animation || 'none');
    setTextY(editingText?.normalizedY ?? 0.5);
    setTextStartMs(editingText?.startTimeMs ?? playheadMs);
    setTextEndMs(editingText?.endTimeMs ?? playheadMs + 3500);
  }, [editingText?.id, activeTool]);

  useEffect(() => {
    if (!recordingVo) return;
    const id = window.setInterval(() => setVoSeconds((s) => s + 1), 1000);
    return () => window.clearInterval(id);
  }, [recordingVo]);

  if (activeTool === 'none') return null;

  const toggleVoiceRecording = async () => {
    if (recordingVo) {
      voRecorderRef.current?.stop();
      setRecordingVo(false);
      return;
    }
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
      voStreamRef.current = stream;
      const chunks: BlobPart[] = [];
      const mr = new MediaRecorder(stream);
      const startedAt = Date.now();
      mr.ondataavailable = (e) => {
        if (e.data.size > 0) chunks.push(e.data);
      };
      mr.onstop = async () => {
        stream.getTracks().forEach((t) => t.stop());
        const blob = new Blob(chunks, { type: 'audio/webm' });
        const id = `vo_${Date.now()}`;
        await saveMediaBlob(id, blob);
        const url = URL.createObjectURL(blob);
        onAddVoiceover(url, Math.max(1000, Date.now() - startedAt));
      };
      voRecorderRef.current = mr;
      setVoSeconds(0);
      mr.start(200);
      setRecordingVo(true);
    } catch {
      // Microphone permission unavailable
    }
  };

  return (
    <div className="max-h-[30vh] overflow-y-auto border-t border-[#2B352D] bg-[#18110D] px-4 py-3">
      <div className="mx-auto max-w-4xl space-y-3">
        <div className="flex items-center justify-between">
          <h2 className="text-sm font-bold capitalize text-white">{activeTool}</h2>
          <button
            type="button"
            onClick={onClose}
            className="text-xs font-semibold text-[#FF5500] hover:text-[#FF8232]"
          >
            Done
          </button>
        </div>

        {activeTool === 'trim' && selectedClip && (
          <div className="grid gap-3 sm:grid-cols-2">
            <label className="block text-xs text-[#9AA69B]">
              Start: {(selectedClip.startPositionMs / 1000).toFixed(1)}s
              <input
                type="range"
                min={0}
                max={Math.max(500, selectedClip.endPositionMs - 250)}
                step={50}
                value={selectedClip.startPositionMs}
                onChange={(e) =>
                  onTrimClip(selectedClip.id, Number(e.target.value), selectedClip.endPositionMs)
                }
                className="mt-1.5 w-full accent-[#FF5500]"
              />
            </label>
            <label className="block text-xs text-[#9AA69B]">
              End: {(selectedClip.endPositionMs / 1000).toFixed(1)}s
              <input
                type="range"
                min={selectedClip.startPositionMs + 250}
                max={selectedClip.sourceDurationMs}
                step={50}
                value={selectedClip.endPositionMs}
                onChange={(e) =>
                  onTrimClip(selectedClip.id, selectedClip.startPositionMs, Number(e.target.value))
                }
                className="mt-1.5 w-full accent-[#FF5500]"
              />
            </label>
          </div>
        )}

        {activeTool === 'crop' && selectedClip && (
          <div className="space-y-3">
            <div className="flex flex-wrap gap-2">
              {(Object.keys(ASPECT_PRESETS) as AspectRatioPresetId[]).map((presetId) => {
                const p = ASPECT_PRESETS[presetId];
                const selected = timeline.aspectRatio === presetId;
                return (
                  <button
                    key={presetId}
                    type="button"
                    onClick={() => onChangeAspect(presetId)}
                    className={`rounded-lg border px-3 py-1.5 text-xs font-semibold transition-colors ${
                      selected
                        ? 'border-[#FF8232] bg-[#FF5500] text-white'
                        : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                    }`}
                  >
                    {p.label}
                  </button>
                );
              })}
            </div>
            <div className="grid gap-3 sm:grid-cols-3">
              <label className="block text-xs text-[#9AA69B]">
                Zoom: {selectedClip.cropConfig.zoom.toFixed(2)}x
                <input
                  type="range"
                  min={1}
                  max={3}
                  step={0.05}
                  value={selectedClip.cropConfig.zoom}
                  onChange={(e) =>
                    onChangeCrop(
                      selectedClip.id,
                      selectedClip.cropConfig.offsetX,
                      selectedClip.cropConfig.offsetY,
                      Number(e.target.value)
                    )
                  }
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
              <label className="block text-xs text-[#9AA69B]">
                Pan X: {selectedClip.cropConfig.offsetX.toFixed(2)}
                <input
                  type="range"
                  min={-1}
                  max={1}
                  step={0.05}
                  value={selectedClip.cropConfig.offsetX}
                  onChange={(e) =>
                    onChangeCrop(
                      selectedClip.id,
                      Number(e.target.value),
                      selectedClip.cropConfig.offsetY,
                      selectedClip.cropConfig.zoom
                    )
                  }
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
              <label className="block text-xs text-[#9AA69B]">
                Pan Y: {selectedClip.cropConfig.offsetY.toFixed(2)}
                <input
                  type="range"
                  min={-1}
                  max={1}
                  step={0.05}
                  value={selectedClip.cropConfig.offsetY}
                  onChange={(e) =>
                    onChangeCrop(
                      selectedClip.id,
                      selectedClip.cropConfig.offsetX,
                      Number(e.target.value),
                      selectedClip.cropConfig.zoom
                    )
                  }
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
            </div>
          </div>
        )}

        {activeTool === 'adjust' && selectedClip && (
          <div className="space-y-3">
            <div className="grid gap-3 sm:grid-cols-3">
              {(
                [
                  ['brightness', 'Brightness', -1, 1, 0.05],
                  ['contrast', 'Contrast', 0.2, 2, 0.05],
                  ['saturation', 'Saturation', 0, 2, 0.05],
                  ['temperature', 'Temperature', -1, 1, 0.05],
                  ['exposure', 'Exposure', -1, 1, 0.05],
                  ['sharpness', 'Sharpness', 0, 1, 0.05],
                ] as const
              ).map(([key, label, min, max, step]) => {
                const flt = selectedClip.filter ?? DEFAULT_CLIP_FILTER;
                return (
                  <label key={key} className="block text-xs text-[#9AA69B]">
                    {label}: {flt[key].toFixed(2)}
                    <input
                      type="range"
                      min={min}
                      max={max}
                      step={step}
                      value={flt[key]}
                      onChange={(e) =>
                        onChangeFilter(selectedClip.id, { [key]: Number(e.target.value) })
                      }
                      className="mt-1 w-full accent-[#FF5500]"
                    />
                  </label>
                );
              })}
            </div>
            <button
              type="button"
              onClick={() => onChangeFilter(selectedClip.id, { ...DEFAULT_CLIP_FILTER })}
              className="rounded-lg border border-[#2B352D] bg-[#221711] px-3 py-1.5 text-xs font-medium text-white hover:border-[#FF5500]"
            >
              Reset Adjustments
            </button>
          </div>
        )}

        {activeTool === 'speed' && selectedClip && (
          <div className="space-y-2">
            <div className="flex flex-wrap gap-2">
              {[0.5, 1, 1.5, 2, 3].map((spd) => (
                <button
                  key={spd}
                  type="button"
                  onClick={() =>
                    onChangeSpeed(selectedClip.id, spd, selectedClip.audioSpeedMode)
                  }
                  className={`rounded-lg border px-3 py-1.5 text-xs font-semibold ${
                    Math.abs(selectedClip.playbackSpeed - spd) < 0.05
                      ? 'border-[#FF8232] bg-[#FF5500] text-white'
                      : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                  }`}
                >
                  {spd}x
                </button>
              ))}
            </div>
            <label className="inline-flex items-center gap-2 text-xs text-[#9AA69B]">
              <input
                type="checkbox"
                checked={selectedClip.audioSpeedMode === 'pitch_preserved'}
                onChange={(e) =>
                  onChangeSpeed(
                    selectedClip.id,
                    selectedClip.playbackSpeed,
                    e.target.checked ? 'pitch_preserved' : 'rate_coupled'
                  )
                }
                className="accent-[#FF5500]"
              />
              Preserve audio pitch
            </label>
          </div>
        )}

        {activeTool === 'volume' && selectedClip && (
          <div className="flex flex-wrap items-center gap-4">
            <label className="flex-1 text-xs text-[#9AA69B]">
              Clip Volume: {Math.round(selectedClip.volume * 100)}%
              <input
                type="range"
                min={0}
                max={2}
                step={0.05}
                value={selectedClip.volume}
                onChange={(e) =>
                  onChangeVolume(selectedClip.id, Number(e.target.value), selectedClip.isMuted)
                }
                className="mt-1.5 w-full accent-[#FF5500]"
              />
            </label>
            <button
              type="button"
              onClick={() =>
                onChangeVolume(selectedClip.id, selectedClip.volume, !selectedClip.isMuted)
              }
              className="rounded-lg border border-[#2B352D] bg-[#221711] px-4 py-2 text-xs font-semibold text-white hover:border-[#FF5500]"
            >
              {selectedClip.isMuted ? 'Unmute Clip' : 'Mute Clip'}
            </button>
          </div>
        )}

        {activeTool === 'rotate' && selectedClip && (
          <div className="flex items-center justify-between">
            <span className="text-xs text-[#9AA69B]">
              Current rotation: {selectedClip.rotationDegrees}°
            </span>
            <button
              type="button"
              onClick={() => onRotateClip(selectedClip.id)}
              className="rounded-lg bg-[#FF5500] px-4 py-2 text-xs font-bold text-white hover:bg-[#FF8232]"
            >
              Rotate +90°
            </button>
          </div>
        )}

        {activeTool === 'transitions' && selectedClip && (
          <div className="space-y-2">
            <div className="flex flex-wrap gap-2">
              {(
                [
                  ['cut', 'Cut'],
                  ['fade', 'Fade'],
                  ['cross_dissolve', 'Cross Dissolve'],
                ] as [TransitionType, string][]
              ).map(([id, label]) => (
                <button
                  key={id}
                  type="button"
                  onClick={() =>
                    onChangeTransition(selectedClip.id, {
                      type: id,
                      durationMs: id === 'cut' ? 0 : selectedClip.transitionIn?.durationMs || 500,
                    })
                  }
                  className={`rounded-lg border px-3 py-1.5 text-xs font-semibold ${
                    (selectedClip.transitionIn?.type ?? 'cut') === id
                      ? 'border-[#FF8232] bg-[#FF5500] text-white'
                      : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                  }`}
                >
                  {label}
                </button>
              ))}
            </div>
            {selectedClip.transitionIn && selectedClip.transitionIn.type !== 'cut' && (
              <label className="block text-xs text-[#9AA69B]">
                Transition Duration: {(selectedClip.transitionIn.durationMs / 1000).toFixed(1)}s
                <input
                  type="range"
                  min={200}
                  max={1500}
                  step={50}
                  value={selectedClip.transitionIn.durationMs}
                  onChange={(e) =>
                    onChangeTransition(selectedClip.id, {
                      type: selectedClip.transitionIn.type,
                      durationMs: Number(e.target.value),
                    })
                  }
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
            )}
          </div>
        )}

        {activeTool === 'music' && (
          <div className="space-y-3">
            <div className="flex flex-wrap gap-2">
              {bundledMusic.map((track) => (
                <button
                  key={track.id}
                  type="button"
                  onClick={() =>
                    onSelectMusic({
                      title: track.title,
                      url: track.url,
                      isBundled: true,
                      durationMs: track.durationMs,
                    })
                  }
                  className={`rounded-lg border px-3 py-1.5 text-xs font-semibold ${
                    timeline.musicTrack?.title === track.title
                      ? 'border-[#FF8232] bg-[#FF5500] text-white'
                      : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                  }`}
                >
                  {track.title}
                </button>
              ))}
              <button
                type="button"
                onClick={() => audioFileRef.current?.click()}
                className="inline-flex items-center gap-1.5 rounded-lg border border-[#2B352D] bg-[#221711] px-3 py-1.5 text-xs font-semibold text-[#FF5500]"
              >
                <Upload className="h-3.5 w-3.5" />
                Device Audio
              </button>
            </div>
            {timeline.musicTrack && (
              <div className="grid gap-3 sm:grid-cols-2">
                <label className="text-xs text-[#9AA69B]">
                  Volume: {Math.round(timeline.musicTrack.volume * 100)}%
                  <input
                    type="range"
                    min={0}
                    max={1.5}
                    step={0.05}
                    value={timeline.musicTrack.volume}
                    onChange={(e) => onUpdateMusic({ volume: Number(e.target.value) })}
                    className="mt-1 w-full accent-[#FF5500]"
                  />
                </label>
                <label className="text-xs text-[#9AA69B]">
                  Trim Start: {(timeline.musicTrack.trimStartMs / 1000).toFixed(1)}s
                  <input
                    type="range"
                    min={0}
                    max={Math.max(500, timeline.musicTrack.trimEndMs - 500)}
                    step={100}
                    value={timeline.musicTrack.trimStartMs}
                    onChange={(e) => onUpdateMusic({ trimStartMs: Number(e.target.value) })}
                    className="mt-1 w-full accent-[#FF5500]"
                  />
                </label>
                <label className="text-xs text-[#9AA69B]">
                  Fade In: {(timeline.musicTrack.fadeInMs / 1000).toFixed(1)}s
                  <input
                    type="range"
                    min={0}
                    max={3000}
                    step={100}
                    value={timeline.musicTrack.fadeInMs}
                    onChange={(e) => onUpdateMusic({ fadeInMs: Number(e.target.value) })}
                    className="mt-1 w-full accent-[#FF5500]"
                  />
                </label>
                <label className="text-xs text-[#9AA69B]">
                  Fade Out: {(timeline.musicTrack.fadeOutMs / 1000).toFixed(1)}s
                  <input
                    type="range"
                    min={0}
                    max={3000}
                    step={100}
                    value={timeline.musicTrack.fadeOutMs}
                    onChange={(e) => onUpdateMusic({ fadeOutMs: Number(e.target.value) })}
                    className="mt-1 w-full accent-[#FF5500]"
                  />
                </label>
                <div className="flex items-center gap-3 sm:col-span-2">
                  <label className="inline-flex items-center gap-1.5 text-xs text-[#9AA69B]">
                    <input
                      type="checkbox"
                      checked={timeline.musicTrack.loopToFitVideo}
                      onChange={(e) => onUpdateMusic({ loopToFitVideo: e.target.checked })}
                      className="accent-[#FF5500]"
                    />
                    Loop to fit video
                  </label>
                  <button
                    type="button"
                    onClick={onRemoveMusic}
                    className="ml-auto rounded-lg bg-[#FF5A5A] px-3 py-1.5 text-xs font-semibold text-white"
                  >
                    Remove Music
                  </button>
                </div>
              </div>
            )}
            <input
              ref={audioFileRef}
              type="file"
              accept="audio/*"
              className="hidden"
              onChange={async (e) => {
                const file = e.target.files?.[0];
                if (!file) return;
                const id = `audio_${Date.now()}`;
                await saveMediaBlob(id, file);
                const url = URL.createObjectURL(file);
                onSelectMusic({ title: file.name, url, isBundled: false, durationMs: 30000 });
              }}
            />
          </div>
        )}

        {activeTool === 'text' && (
          <div className="space-y-3">
            <input
              type="text"
              value={textVal}
              onChange={(e) => setTextVal(e.target.value)}
              placeholder="Enter overlay text..."
              className="w-full rounded-xl border border-[#2B352D] bg-[#0B0807] px-3 py-2 text-sm text-white outline-none focus:border-[#FF5500]"
            />
            <div className="flex flex-wrap items-center gap-2">
              {['#FFFFFF', '#FF5500', '#FF8232', '#82B8FF', '#FFD166'].map((c) => (
                <button
                  key={c}
                  type="button"
                  aria-label={`Color ${c}`}
                  onClick={() => setTextColor(c)}
                  style={{ backgroundColor: c }}
                  className={`h-7 w-7 rounded-full border-2 ${
                    textColor === c ? 'border-[#FF5500]' : 'border-[#2B352D]'
                  }`}
                />
              ))}
              {(['left', 'center', 'right'] as TextAlignment[]).map((al) => (
                <button
                  key={al}
                  type="button"
                  onClick={() => setTextAlign(al)}
                  className={`rounded-lg border px-2.5 py-1 text-xs capitalize ${
                    textAlign === al
                      ? 'border-[#FF8232] bg-[#FF5500] text-white'
                      : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                  }`}
                >
                  {al}
                </button>
              ))}
              {(
                [
                  ['none', 'Static'],
                  ['fade', 'Fade'],
                  ['slide_up', 'Slide Up'],
                ] as [TextAnimation, string][]
              ).map(([animId, label]) => (
                <button
                  key={animId}
                  type="button"
                  onClick={() => setTextAnim(animId)}
                  className={`rounded-lg border px-2.5 py-1 text-xs ${
                    textAnim === animId
                      ? 'border-[#FF8232] bg-[#FF5500] text-white'
                      : 'border-[#2B352D] bg-[#221711] text-[#9AA69B]'
                  }`}
                >
                  {label}
                </button>
              ))}
            </div>
            <div className="grid gap-3 sm:grid-cols-2">
              <label className="text-xs text-[#9AA69B]">
                Size: {textSize}px
                <input
                  type="range"
                  min={16}
                  max={60}
                  value={textSize}
                  onChange={(e) => setTextSize(Number(e.target.value))}
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
              <label className="text-xs text-[#9AA69B]">
                Vertical Position: {Math.round(textY * 100)}%
                <input
                  type="range"
                  min={0.1}
                  max={0.9}
                  step={0.05}
                  value={textY}
                  onChange={(e) => setTextY(Number(e.target.value))}
                  className="mt-1 w-full accent-[#FF5500]"
                />
              </label>
            </div>
            <div className="flex gap-2">
              <button
                type="button"
                onClick={() =>
                  onUpsertText({
                    id: editingText?.id || `text_${Date.now()}`,
                    text: textVal.trim() || 'TRISVAX',
                    fontFamily: 'Plus Jakarta Sans',
                    fontSizePx: textSize,
                    colorHex: textColor,
                    alignment: textAlign,
                    normalizedX: textAlign === 'left' ? 0.15 : textAlign === 'right' ? 0.85 : 0.5,
                    normalizedY: textY,
                    startTimeMs: Math.min(textStartMs, textEndMs - 300),
                    endTimeMs: Math.max(textEndMs, textStartMs + 500),
                    animation: textAnim,
                  })
                }
                className="flex-1 rounded-xl bg-[#FF5500] py-2 text-xs font-bold text-white hover:bg-[#FF8232]"
              >
                {editingText ? 'Update Text' : 'Add Text'}
              </button>
              {editingText && (
                <button
                  type="button"
                  onClick={() => onDeleteText(editingText.id)}
                  className="rounded-xl bg-[#FF5A5A] px-4 py-2 text-xs font-bold text-white"
                >
                  Delete
                </button>
              )}
            </div>
          </div>
        )}

        {activeTool === 'voice' && (
          <div className="space-y-3">
            <button
              type="button"
              onClick={toggleVoiceRecording}
              className={`flex w-full items-center justify-center gap-2 rounded-xl py-2.5 text-xs font-bold text-white ${
                recordingVo ? 'bg-[#FF5A5A]' : 'bg-[#FF5500] hover:bg-[#FF8232]'
              }`}
            >
              {recordingVo ? <Square className="h-4 w-4" /> : <Mic className="h-4 w-4" />}
              {recordingVo ? `Stop Recording (${voSeconds}s)` : 'Record Voiceover at Playhead'}
            </button>
            {timeline.voiceovers.map((vo) => (
              <div
                key={vo.id}
                className="flex items-center justify-between rounded-xl bg-[#221711] px-3 py-2 text-xs text-white"
              >
                <span>
                  {vo.title} ({Math.round(vo.durationMs / 1000)}s @{' '}
                  {Math.round(vo.timelineStartMs / 1000)}s)
                </span>
                <button
                  type="button"
                  onClick={() => onDeleteVoiceover(vo.id)}
                  aria-label="Delete voiceover"
                  className="text-[#FF5A5A]"
                >
                  <Trash2 className="h-4 w-4" />
                </button>
              </div>
            ))}
          </div>
        )}

        {activeTool === 'clips' && (
          <div className="space-y-2">
            {timeline.clips.map((clip, idx) => (
              <div
                key={clip.id}
                className="flex items-center justify-between rounded-xl bg-[#221711] px-3 py-2 text-xs text-white"
              >
                <span className="truncate">
                  {idx + 1}. {clip.displayName}
                </span>
                <div className="flex items-center gap-1">
                  <button
                    type="button"
                    disabled={idx === 0}
                    onClick={() => onReorderClip(idx, idx - 1)}
                    aria-label="Move clip earlier"
                    className="rounded p-1 hover:bg-[#18110D] disabled:opacity-30"
                  >
                    <ArrowUp className="h-4 w-4" />
                  </button>
                  <button
                    type="button"
                    disabled={idx === timeline.clips.length - 1}
                    onClick={() => onReorderClip(idx, idx + 1)}
                    aria-label="Move clip later"
                    className="rounded p-1 hover:bg-[#18110D] disabled:opacity-30"
                  >
                    <ArrowDown className="h-4 w-4" />
                  </button>
                  <button
                    type="button"
                    disabled={timeline.clips.length <= 1}
                    onClick={() => onDeleteClip(clip.id)}
                    aria-label="Delete clip"
                    className="rounded p-1 text-[#FF5A5A] hover:bg-[#18110D] disabled:opacity-30"
                  >
                    <Trash2 className="h-4 w-4" />
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
