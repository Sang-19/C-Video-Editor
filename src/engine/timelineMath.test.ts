import assert from 'node:assert/strict';
import test from 'node:test';
import {
  addVideoTrack,
  batchDeleteSelection,
  batchDuplicateSelection,
  batchMuteSelection,
  batchShiftSelection,
  buildCssFilterString,
  deleteClip,
  deleteVideoTrack,
  findActiveClipsAtPlayhead,
  findClipAtPlayhead,
  getClipEffectiveDurationMs,
  getTimelineTotalDurationMs,
  getVideoTrackCount,
  isAudioTrackAudible,
  marqueeSelectTimeline,
  MAX_VIDEO_TRACKS,
  moveClipToTrackAndTime,
  reorderClip,
  selectionSummary,
  splitClipAtPlayhead,
  toggleTrackStateInTimeline,
  toNativeTimelinePayload,
  trimClip,
  updateClipFilter,
  updateClipRotation,
  updateClipSpeed,
  updateClipTransition,
} from './timelineMath';
import { DEFAULT_CLIP_FILTER, type TimelineState, type VideoClipItem } from '../types/editor';

function makeClip(id: string, durMs = 6000, trackIndex = 0, timelineStartMs = 0): VideoClipItem {
  return {
    id,
    sourceUrl: `/assets/samples/${id}.mp4`,
    displayName: id,
    isImage: false,
    sourceDurationMs: durMs,
    startPositionMs: 0,
    endPositionMs: durMs,
    timelineStartMs,
    trackIndex,
    playbackSpeed: 1,
    audioSpeedMode: 'rate_coupled',
    volume: 1,
    isMuted: false,
    rotationDegrees: 0,
    cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
    filter: { ...DEFAULT_CLIP_FILTER },
    transitionIn: { type: 'cut', durationMs: 0 },
    thumbnails: [],
    fileSizeBytes: 500000,
  };
}

test('trim, split, speed, rotation, filter, and transition update timeline accurately', () => {
  let clips: VideoClipItem[] = [makeClip('a', 6000), makeClip('b', 4000)];
  clips = trimClip(clips, 'a', 1000, 5000); // 4000ms
  assert.equal(getClipEffectiveDurationMs(clips[0]), 4000);
  assert.equal(clips[1].timelineStartMs, 4000);

  const split = splitClipAtPlayhead(clips, 2500);
  assert.ok(split);
  assert.equal(split.length, 3);
  assert.equal(split[0].endPositionMs, 3500);
  assert.equal(split[1].startPositionMs, 3500);

  const sped = updateClipSpeed(split, split[0].id, 2.0, 'pitch_preserved');
  assert.equal(getClipEffectiveDurationMs(sped[0]), 1250);
  assert.equal(sped[0].audioSpeedMode, 'pitch_preserved');

  const rotated = updateClipRotation(sped, sped[0].id, 450);
  assert.equal(rotated[0].rotationDegrees, 90);

  const filtered = updateClipFilter(rotated, rotated[0].id, { brightness: 0.3, contrast: 1.25 });
  assert.match(buildCssFilterString(filtered[0].filter), /brightness\(/);

  const transitioned = updateClipTransition(filtered, filtered[1].id, {
    type: 'cross_dissolve',
    durationMs: 600,
  });
  assert.equal(transitioned[1].transitionIn.type, 'cross_dissolve');
  assert.equal(transitioned[1].transitionIn.durationMs, 600);
});

test('reorderClip, deleteClip, findClipAtPlayhead, and native payload conversion', () => {
  let clips: VideoClipItem[] = [
    makeClip('c1', 3000),
    { ...makeClip('c2', 3000), timelineStartMs: 3000 },
  ];
  clips = reorderClip(clips, 0, 1);
  assert.equal(clips[0].id, 'c2');
  assert.equal(clips[1].timelineStartMs, 3000);

  const lookup = findClipAtPlayhead(clips, 4500);
  assert.ok(lookup);
  assert.equal(lookup.clip.id, 'c1');
  assert.equal(lookup.localSourceTimeMs, 1500);

  const afterDelete = deleteClip(clips, 'c2');
  assert.equal(afterDelete.length, 1);
  // Deleting last remaining clip is prevented
  assert.equal(deleteClip(afterDelete, 'c1').length, 1);

  const tl: TimelineState = {
    aspectRatio: 'PORTRAIT_9_16',
    clips: afterDelete,
    musicTrack: null,
    voiceovers: [],
    textOverlays: [],
  };
  assert.equal(getTimelineTotalDurationMs(tl), 3000);
  const payload = toNativeTimelinePayload(tl);
  assert.equal(payload.clips[0].sourceDurationUs, 3000000);
});

test('multi-track placement, marquee selection across video+audio+text, track lock/solo/hide, and batch edits', () => {
  let tl: TimelineState = {
    aspectRatio: 'PORTRAIT_9_16',
    clips: [makeClip('v1', 5000, 0, 0), makeClip('v2', 4000, 1, 1500)],
    musicTrack: {
      id: 'mus1',
      title: 'Beat',
      sourceUrl: '/beat.wav',
      isBundledAsset: true,
      sourceDurationMs: 12000,
      trimStartMs: 0,
      trimEndMs: 6000,
      timelineStartMs: 500,
      trackIndex: 0,
      volume: 0.8,
      isMuted: false,
      loopToFitVideo: false,
      fadeInMs: 0,
      fadeOutMs: 0,
    },
    voiceovers: [
      {
        id: 'vo1',
        title: 'VO 1',
        sourceUrl: 'blob:vo',
        durationMs: 3000,
        trimStartMs: 0,
        trimEndMs: 3000,
        timelineStartMs: 2000,
        trackIndex: 1,
        volume: 1,
        isMuted: false,
      },
    ],
    textOverlays: [
      {
        id: 'txt1',
        text: 'Hello',
        fontFamily: 'Sans',
        fontSizePx: 28,
        colorHex: '#FFFFFF',
        alignment: 'center',
        normalizedX: 0.5,
        normalizedY: 0.5,
        startTimeMs: 1800,
        endTimeMs: 4200,
        trackIndex: 0,
        animation: 'none',
      },
    ],
  };

  assert.equal(getTimelineTotalDurationMs(tl), 6500); // music 500..6500
  const layers = findActiveClipsAtPlayhead(tl.clips, 2000, tl.trackStates);
  assert.equal(layers.length, 2);
  assert.equal(layers[0].clip.id, 'v1');
  assert.equal(layers[1].clip.id, 'v2');

  // Marquee select across V1, V2, T1, A1, A2 in [1600ms, 3000ms]
  const sel = marqueeSelectTimeline(tl, 1600, 3000, [
    { kind: 'video', trackIndex: 0 },
    { kind: 'video', trackIndex: 1 },
    { kind: 'text', trackIndex: 0 },
    { kind: 'audio', trackIndex: 0 },
    { kind: 'audio', trackIndex: 1 },
  ]);
  const summary = selectionSummary(sel);
  assert.equal(summary.videoCount, 2);
  assert.equal(summary.audioCount, 2);
  assert.equal(summary.textCount, 1);
  assert.equal(summary.total, 5);

  // Batch shift by +500ms
  tl = batchShiftSelection(tl, sel, 500, 0);
  assert.equal(tl.clips[1].timelineStartMs, 2000);
  assert.equal(tl.musicTrack?.timelineStartMs, 1000);
  assert.equal(tl.voiceovers[0].timelineStartMs, 2500);
  assert.equal(tl.textOverlays[0].startTimeMs, 2300);

  // Batch mute
  tl = batchMuteSelection(tl, sel, true);
  assert.equal(tl.clips[0].isMuted, true);
  assert.equal(tl.musicTrack?.isMuted, true);
  assert.equal(tl.voiceovers[0].isMuted, true);

  // Lock Text T1 -> marquee excludes text
  tl = toggleTrackStateInTimeline(tl, 'text', 0, 'locked');
  const selLocked = marqueeSelectTimeline(tl, 2000, 3000, [
    { kind: 'video', trackIndex: 0 },
    { kind: 'text', trackIndex: 0 },
  ]);
  assert.equal(selLocked.textIds.length, 0);
  assert.equal(selLocked.clipIds.length, 1);

  // Hide V2 -> active layers skips V2
  tl = toggleTrackStateInTimeline(tl, 'video', 1, 'hidden');
  assert.equal(findActiveClipsAtPlayhead(tl.clips, 2200, tl.trackStates).length, 1);

  // Solo A2 -> A1 not audible, A2 audible
  tl = toggleTrackStateInTimeline(tl, 'audio', 1, 'solo');
  assert.equal(isAudioTrackAudible(tl, 0), false);
  assert.equal(isAudioTrackAudible(tl, 1), true);

  // Move clip to track & time
  tl = moveClipToTrackAndTime(tl, 'v1', 1000, 2);
  assert.equal(tl.clips[0].trackIndex, 2);
  assert.equal(tl.clips[0].timelineStartMs, 1000);

  // Duplicate + delete selection
  const dup = batchDuplicateSelection(tl, { clipIds: ['v2'], audioIds: [], voiceoverIds: ['vo1'], textIds: [] });
  assert.equal(dup.timeline.clips.length, 3);
  assert.equal(dup.timeline.voiceovers.length, 2);
  const afterDel = batchDeleteSelection(dup.timeline, dup.selection);
  assert.equal(afterDel.clips.length, 2);
  assert.equal(afterDel.voiceovers.length, 1);

  const payload = toNativeTimelinePayload(tl);
  assert.equal(payload.trackStates.length, 3);
});

test('addVideoTrack enforces max 5 tracks and deleteVideoTrack removes track and its videos', () => {
  let tl: TimelineState = {
    aspectRatio: 'PORTRAIT_9_16',
    clips: [makeClip('v1', 4000, 0, 0)],
    musicTrack: null,
    voiceovers: [],
    textOverlays: [],
  };

  assert.equal(getVideoTrackCount(tl), 1);

  for (let i = 0; i < 8; i++) {
    tl = addVideoTrack(tl);
  }
  assert.equal(getVideoTrackCount(tl), MAX_VIDEO_TRACKS);
  assert.equal(tl.videoTrackCount, 5);

  tl = {
    ...tl,
    clips: [
      makeClip('v1', 4000, 0, 0),
      makeClip('v2a', 2000, 1, 500),
      makeClip('v2b', 1500, 1, 2600),
      makeClip('v3', 3000, 2, 1000),
    ],
  };

  // Delete V2 (index 1) -> v2a & v2b deleted, v3 shifts down to index 1
  tl = deleteVideoTrack(tl, 1);
  assert.equal(getVideoTrackCount(tl), 4);
  assert.equal(tl.clips.some((c) => c.id === 'v2a' || c.id === 'v2b'), false);
  const shiftedV3 = tl.clips.find((c) => c.id === 'v3');
  assert.ok(shiftedV3);
  assert.equal(shiftedV3.trackIndex, 1);
});
