export type AspectRatioPresetId =
  | 'PORTRAIT_9_16'
  | 'LANDSCAPE_16_9'
  | 'SQUARE_1_1'
  | 'FREEFORM';

export interface AspectPresetMeta {
  id: AspectRatioPresetId;
  label: string;
  ratioWidth: number;
  ratioHeight: number;
  exportWidth: number;
  exportHeight: number;
}

export const ASPECT_PRESETS: Record<AspectRatioPresetId, AspectPresetMeta> = {
  PORTRAIT_9_16: {
    id: 'PORTRAIT_9_16',
    label: '9:16',
    ratioWidth: 9,
    ratioHeight: 16,
    exportWidth: 1080,
    exportHeight: 1920,
  },
  LANDSCAPE_16_9: {
    id: 'LANDSCAPE_16_9',
    label: '16:9',
    ratioWidth: 16,
    ratioHeight: 9,
    exportWidth: 1920,
    exportHeight: 1080,
  },
  SQUARE_1_1: {
    id: 'SQUARE_1_1',
    label: '1:1',
    ratioWidth: 1,
    ratioHeight: 1,
    exportWidth: 1080,
    exportHeight: 1080,
  },
  FREEFORM: {
    id: 'FREEFORM',
    label: 'Free',
    ratioWidth: 9,
    ratioHeight: 16,
    exportWidth: 1080,
    exportHeight: 1920,
  },
};

export interface CropConfig {
  offsetX: number;
  offsetY: number;
  zoom: number;
}

export interface ClipFilterConfig {
  brightness: number;  // -1..1
  contrast: number;    // 0..2
  saturation: number;  // 0..2
  temperature: number; // -1..1
  exposure: number;    // -1..1
  sharpness: number;   // 0..1
}

export type TransitionType = 'cut' | 'fade' | 'cross_dissolve';

export interface ClipTransitionConfig {
  type: TransitionType;
  durationMs: number;
}

export type AudioSpeedMode = 'rate_coupled' | 'pitch_preserved';

export type TrackKind = 'video' | 'audio' | 'text';

export interface TimelineTrackState {
  kind: TrackKind;
  trackIndex: number;
  locked?: boolean;
  muted?: boolean;
  hidden?: boolean;
  solo?: boolean;
}

export interface TimelineSelection {
  clipIds: string[];
  audioIds: string[];
  voiceoverIds: string[];
  textIds: string[];
}

export interface VideoClipItem {
  id: string;
  sourceUrl: string;
  mediaBlobId?: string;
  displayName: string;
  isImage: boolean;
  sourceDurationMs: number;
  startPositionMs: number;
  endPositionMs: number;
  timelineStartMs: number;
  trackIndex?: number;
  playbackSpeed: number;
  audioSpeedMode: AudioSpeedMode;
  volume: number;
  isMuted: boolean;
  rotationDegrees: number;
  cropConfig: CropConfig;
  filter: ClipFilterConfig;
  transitionIn: ClipTransitionConfig;
  thumbnails: string[];
  fileSizeBytes: number;
}

export interface MusicTrackItem {
  id: string;
  title: string;
  sourceUrl: string;
  mediaBlobId?: string;
  isBundledAsset: boolean;
  sourceDurationMs: number;
  trimStartMs: number;
  trimEndMs: number;
  timelineStartMs?: number;
  trackIndex?: number;
  volume: number;
  isMuted: boolean;
  loopToFitVideo: boolean;
  fadeInMs: number;
  fadeOutMs: number;
}

export interface VoiceoverTrackItem {
  id: string;
  title: string;
  sourceUrl: string;
  mediaBlobId?: string;
  durationMs: number;
  trimStartMs: number;
  trimEndMs: number;
  timelineStartMs: number;
  trackIndex?: number;
  volume: number;
  isMuted: boolean;
}

export type TextAlignment = 'left' | 'center' | 'right';
export type TextAnimation = 'none' | 'fade' | 'slide_up';

export interface TextOverlayItem {
  id: string;
  text: string;
  fontFamily: string;
  fontSizePx: number;
  colorHex: string;
  alignment: TextAlignment;
  normalizedX: number;
  normalizedY: number;
  startTimeMs: number;
  endTimeMs: number;
  trackIndex?: number;
  animation: TextAnimation;
}

export interface TimelineState {
  aspectRatio: AspectRatioPresetId;
  clips: VideoClipItem[];
  musicTrack: MusicTrackItem | null;
  voiceovers: VoiceoverTrackItem[];
  textOverlays: TextOverlayItem[];
  trackStates?: TimelineTrackState[];
  videoTrackCount?: number;
  textTrackCount?: number;
  audioTrackCount?: number;
}

export type ProjectCategory = 'DRAFTS' | 'REELS' | 'VIDEOS';

export interface ProjectRecord {
  id: string;
  name: string;
  category: ProjectCategory;
  thumbnailUrl: string;
  durationMs: number;
  storageSizeBytes: number;
  createdAt: number;
  lastModifiedAt: number;
  timeline: TimelineState;
}

export interface DeviceMediaItem {
  id: string;
  url: string;
  displayName: string;
  isVideo: boolean;
  durationMs: number;
  thumbnailUrl: string;
  sizeBytes: number;
  mimeType: string;
}

export interface BundledMusicTrack {
  id: string;
  title: string;
  url: string;
  durationMs: number;
}

export type EditorToolId =
  | 'none'
  | 'music'
  | 'text'
  | 'crop'
  | 'trim'
  | 'split'
  | 'adjust'
  | 'speed'
  | 'volume'
  | 'rotate'
  | 'voice'
  | 'transitions'
  | 'clips';

export type ScreenRoute =
  | { name: 'welcome' }
  | { name: 'projects' }
  | { name: 'picker' }
  | { name: 'camera' }
  | { name: 'editor'; projectId: string }
  | { name: 'finished'; exportedUrl?: string; exportedFileName?: string };

export const DEFAULT_CLIP_FILTER: ClipFilterConfig = {
  brightness: 0,
  contrast: 1,
  saturation: 1,
  temperature: 0,
  exposure: 0,
  sharpness: 0,
};
