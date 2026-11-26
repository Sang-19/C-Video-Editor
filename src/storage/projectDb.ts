import type { DeviceMediaItem, ProjectRecord, TimelineState } from '../types/editor';
import { getTimelineTotalDurationMs } from '../engine/timelineMath';

const DB_NAME = 'trisvax_studio_local_db';
const DB_VERSION = 1;
const STORE_PROJECTS = 'projects';
const STORE_MEDIA_BLOBS = 'media_blobs';

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE_PROJECTS)) {
        db.createObjectStore(STORE_PROJECTS, { keyPath: 'id' });
      }
      if (!db.objectStoreNames.contains(STORE_MEDIA_BLOBS)) {
        db.createObjectStore(STORE_MEDIA_BLOBS, { keyPath: 'id' });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export async function saveMediaBlob(id: string, blob: Blob): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_MEDIA_BLOBS, 'readwrite');
    tx.objectStore(STORE_MEDIA_BLOBS).put({ id, blob });
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function getMediaBlob(id: string): Promise<Blob | null> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_MEDIA_BLOBS, 'readonly');
    const req = tx.objectStore(STORE_MEDIA_BLOBS).get(id);
    req.onsuccess = () => {
      const rec = req.result as { id: string; blob: Blob } | undefined;
      resolve(rec?.blob ?? null);
    };
    req.onerror = () => reject(req.error);
  });
}

export async function rehydrateProjectMediaUrls(project: ProjectRecord): Promise<ProjectRecord> {
  const urlCache = new Map<string, string>();
  const resolveUrl = async (currentUrl: string, blobId?: string): Promise<string> => {
    if (!currentUrl.startsWith('blob:') || !blobId) return currentUrl;
    if (urlCache.has(blobId)) return urlCache.get(blobId)!;
    try {
      const blob = await getMediaBlob(blobId);
      if (blob) {
        const freshUrl = URL.createObjectURL(blob);
        urlCache.set(blobId, freshUrl);
        return freshUrl;
      }
    } catch {
      // ignore fallback
    }
    return currentUrl;
  };

  let changed = false;
  const clips = await Promise.all(
    project.timeline.clips.map(async (c) => {
      const key = c.mediaBlobId || c.id;
      const nextUrl = await resolveUrl(c.sourceUrl, key);
      if (nextUrl !== c.sourceUrl) {
        changed = true;
        return { ...c, sourceUrl: nextUrl, mediaBlobId: key };
      }
      return c;
    })
  );

  let musicTrack = project.timeline.musicTrack;
  if (musicTrack) {
    const key = musicTrack.mediaBlobId || musicTrack.id;
    const nextUrl = await resolveUrl(musicTrack.sourceUrl, key);
    if (nextUrl !== musicTrack.sourceUrl) {
      changed = true;
      musicTrack = { ...musicTrack, sourceUrl: nextUrl, mediaBlobId: key };
    }
  }

  const voiceovers = await Promise.all(
    project.timeline.voiceovers.map(async (v) => {
      const key = v.mediaBlobId || v.id;
      const nextUrl = await resolveUrl(v.sourceUrl, key);
      if (nextUrl !== v.sourceUrl) {
        changed = true;
        return { ...v, sourceUrl: nextUrl, mediaBlobId: key };
      }
      return v;
    })
  );

  if (!changed) return project;
  return {
    ...project,
    timeline: {
      ...project.timeline,
      clips,
      musicTrack,
      voiceovers,
    },
  };
}

export async function listAllProjects(): Promise<ProjectRecord[]> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PROJECTS, 'readonly');
    const req = tx.objectStore(STORE_PROJECTS).getAll();
    req.onsuccess = () => {
      const items = (req.result as ProjectRecord[]) || [];
      items.sort((a, b) => b.lastModifiedAt - a.lastModifiedAt);
      resolve(items);
    };
    req.onerror = () => reject(req.error);
  });
}

export async function getProjectById(id: string): Promise<ProjectRecord | null> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PROJECTS, 'readonly');
    const req = tx.objectStore(STORE_PROJECTS).get(id);
    req.onsuccess = () => resolve((req.result as ProjectRecord) || null);
    req.onerror = () => reject(req.error);
  });
}

export async function upsertProject(project: ProjectRecord): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PROJECTS, 'readwrite');
    tx.objectStore(STORE_PROJECTS).put(project);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function deleteProjectById(id: string): Promise<void> {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_PROJECTS, 'readwrite');
    tx.objectStore(STORE_PROJECTS).delete(id);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

export async function renameProjectById(id: string, newName: string): Promise<void> {
  const existing = await getProjectById(id);
  if (!existing) return;
  await upsertProject({
    ...existing,
    name: newName.trim() || 'Untitled',
    lastModifiedAt: Date.now(),
  });
}

export function inferProjectCategory(timeline: TimelineState): ProjectRecord['category'] {
  if (timeline.clips.length === 0) return 'DRAFTS';
  if (timeline.aspectRatio === 'PORTRAIT_9_16') return 'REELS';
  return 'VIDEOS';
}

export function computeTimelineStorageBytes(timeline: TimelineState): number {
  return timeline.clips.reduce((sum, clip) => sum + (clip.fileSizeBytes || 450000), 0);
}

export const BUNDLED_DEVICE_SAMPLES: DeviceMediaItem[] = [
  {
    id: 'sample_sunset_skyline',
    url: '/assets/samples/sunset_skyline.mp4',
    displayName: 'Sunset Skyline',
    isVideo: true,
    durationMs: 8000,
    thumbnailUrl: '',
    sizeBytes: 373721,
    mimeType: 'video/mp4',
  },
  {
    id: 'sample_forest_road',
    url: '/assets/samples/forest_road.mp4',
    displayName: 'Forest Road',
    isVideo: true,
    durationMs: 7000,
    thumbnailUrl: '',
    sizeBytes: 466258,
    mimeType: 'video/mp4',
  },
  {
    id: 'sample_window_light',
    url: '/assets/samples/window_light.mp4',
    displayName: 'Window Light',
    isVideo: true,
    durationMs: 6000,
    thumbnailUrl: '',
    sizeBytes: 433975,
    mimeType: 'video/mp4',
  },
];

export async function seedStarterProjectsIfEmpty(): Promise<ProjectRecord[]> {
  const existing = await listAllProjects();
  if (existing.length > 0) return existing;

  const now = Date.now();
  const starterTimeline: TimelineState = {
    aspectRatio: 'PORTRAIT_9_16',
    clips: [
      {
        id: 'starter_clip_1',
        sourceUrl: '/assets/samples/sunset_skyline.mp4',
        displayName: 'Sunset Skyline',
        isImage: false,
        sourceDurationMs: 8000,
        startPositionMs: 0,
        endPositionMs: 8000,
        timelineStartMs: 0,
        playbackSpeed: 1,
        audioSpeedMode: 'rate_coupled',
        volume: 1,
        isMuted: false,
        rotationDegrees: 0,
        cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
        filter: { brightness: 0, contrast: 1, saturation: 1, temperature: 0, exposure: 0, sharpness: 0 },
        transitionIn: { type: 'cut', durationMs: 0 },
        thumbnails: [],
        fileSizeBytes: 373721,
      },
      {
        id: 'starter_clip_2',
        sourceUrl: '/assets/samples/forest_road.mp4',
        displayName: 'Forest Road',
        isImage: false,
        sourceDurationMs: 7000,
        startPositionMs: 0,
        endPositionMs: 7000,
        timelineStartMs: 8000,
        playbackSpeed: 1,
        audioSpeedMode: 'rate_coupled',
        volume: 1,
        isMuted: false,
        rotationDegrees: 0,
        cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
        filter: { brightness: 0, contrast: 1, saturation: 1, temperature: 0, exposure: 0, sharpness: 0 },
        transitionIn: { type: 'fade', durationMs: 500 },
        thumbnails: [],
        fileSizeBytes: 466258,
      },
    ],
    musicTrack: null,
    voiceovers: [],
    textOverlays: [],
  };

  const starter: ProjectRecord = {
    id: 'proj_starter_sunset',
    name: 'Sunset Reel',
    category: 'REELS',
    thumbnailUrl: '',
    durationMs: getTimelineTotalDurationMs(starterTimeline),
    storageSizeBytes: computeTimelineStorageBytes(starterTimeline),
    createdAt: now - 3600_000,
    lastModifiedAt: now - 1800_000,
    timeline: starterTimeline,
  };

  await upsertProject(starter);
  return [starter];
}
