import { useEffect, useRef, useState } from 'react';
import {
  getTimelineTotalDurationMs,
  recomputeTimelinePositions,
} from './engine/timelineMath';
import {
  exportTimelineTo1080pMp4,
  extractVideoThumbnails,
} from './engine/videoMediaUtils';
import { CameraCaptureScreen } from './screens/CameraCaptureScreen';
import { EditorScreen } from './screens/EditorScreen';
import { ExportProgressModal, FinishedScreen } from './screens/ExportAndFinish';
import { MediaPickerScreen } from './screens/MediaPickerScreen';
import { ProjectsScreen } from './screens/ProjectsScreen';
import { WelcomeScreen } from './screens/WelcomeScreen';
import {
  computeTimelineStorageBytes,
  deleteProjectById,
  inferProjectCategory,
  listAllProjects,
  rehydrateProjectMediaUrls,
  renameProjectById,
  seedStarterProjectsIfEmpty,
  upsertProject,
} from './storage/projectDb';
import {
  DEFAULT_CLIP_FILTER,
  type DeviceMediaItem,
  type ProjectRecord,
  type ScreenRoute,
  type TimelineState,
  type VideoClipItem,
} from './types/editor';

export function App() {
  const initialRoute: ScreenRoute =
    typeof window !== 'undefined' &&
    new URLSearchParams(window.location.search).get('autostart') === '1'
      ? { name: 'projects' }
      : { name: 'welcome' };

  const [route, setRoute] = useState<ScreenRoute>(initialRoute);
  const [projects, setProjects] = useState<ProjectRecord[]>([]);
  const [exportProgress, setExportProgress] = useState(0);
  const [exportStatus, setExportStatus] = useState('');
  const [exportVisible, setExportVisible] = useState(false);
  const [exportError, setExportError] = useState<string | null>(null);
  const exportAbortRef = useRef<AbortController | null>(null);

  const refreshProjects = async () => {
    const rawList = await seedStarterProjectsIfEmpty();
    const list = await Promise.all(rawList.map(rehydrateProjectMediaUrls));
    // Populate starter thumbnails asynchronously if blank
    const enriched = await Promise.all(
      list.map(async (p) => {
        if (p.thumbnailUrl || p.timeline.clips.length === 0) return p;
        const firstClip = p.timeline.clips[0];
        if (!firstClip.isImage) {
          const thumbs = await extractVideoThumbnails(
            firstClip.sourceUrl,
            firstClip.sourceDurationMs,
            2
          );
          if (thumbs.length > 0) {
            const updatedClips = p.timeline.clips.map((c, idx) =>
              idx === 0 ? { ...c, thumbnails: thumbs } : c
            );
            const updated: ProjectRecord = {
              ...p,
              thumbnailUrl: thumbs[0],
              timeline: { ...p.timeline, clips: updatedClips },
            };
            await upsertProject(updated);
            return updated;
          }
        }
        return p;
      })
    );
    setProjects(enriched);
  };

  useEffect(() => {
    refreshProjects();
  }, []);

  const createProjectFromMedia = async (selectedItems: DeviceMediaItem[]) => {
    const clips: VideoClipItem[] = [];
    for (const item of selectedItems) {
      const thumbs =
        item.thumbnailUrl && !item.isVideo
          ? [item.thumbnailUrl]
          : item.isVideo
            ? await extractVideoThumbnails(item.url, item.durationMs, 3)
            : [item.url];
      clips.push({
        id: `clip_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`,
        sourceUrl: item.url,
        mediaBlobId: item.id,
        displayName: item.displayName,
        isImage: !item.isVideo,
        sourceDurationMs: item.durationMs,
        startPositionMs: 0,
        endPositionMs: item.durationMs,
        timelineStartMs: 0,
        playbackSpeed: 1,
        audioSpeedMode: 'rate_coupled',
        volume: 1,
        isMuted: false,
        rotationDegrees: 0,
        cropConfig: { offsetX: 0, offsetY: 0, zoom: 1 },
        filter: { ...DEFAULT_CLIP_FILTER },
        transitionIn: { type: 'cut', durationMs: 0 },
        thumbnails: thumbs.length > 0 ? thumbs : item.thumbnailUrl ? [item.thumbnailUrl] : [],
        fileSizeBytes: item.sizeBytes,
      });
    }

    const timeline: TimelineState = {
      aspectRatio: 'PORTRAIT_9_16',
      clips: recomputeTimelinePositions(clips),
      musicTrack: null,
      voiceovers: [],
      textOverlays: [],
    };
    const now = Date.now();
    const record: ProjectRecord = {
      id: `proj_${now}`,
      name:
        selectedItems[0]?.displayName.replace(/\.[^.]+$/, '') || 'New project',
      category: inferProjectCategory(timeline),
      thumbnailUrl: timeline.clips[0]?.thumbnails[0] || '',
      durationMs: getTimelineTotalDurationMs(timeline),
      storageSizeBytes: computeTimelineStorageBytes(timeline),
      createdAt: now,
      lastModifiedAt: now,
      timeline,
    };
    await upsertProject(record);
    const all = await listAllProjects();
    setProjects(all);
    setRoute({ name: 'editor', projectId: record.id });
  };

  const handleSaveProject = async (updated: ProjectRecord) => {
    await upsertProject(updated);
    setProjects((prev) =>
      prev.map((p) => (p.id === updated.id ? updated : p))
    );
  };

  const handleExport = async (timeline: TimelineState) => {
    const controller = new AbortController();
    exportAbortRef.current = controller;
    setExportError(null);
    setExportProgress(2);
    setExportStatus('Preparing C++20 export pipeline...');
    setExportVisible(true);

    try {
      const result = await exportTimelineTo1080pMp4(
        timeline,
        (pct, msg) => {
          setExportProgress(pct);
          setExportStatus(msg);
        },
        controller.signal
      );
      setExportVisible(false);
      setRoute({
        name: 'finished',
        exportedUrl: result.blobUrl,
        exportedFileName: result.fileName,
      });
    } catch (err) {
      setExportVisible(false);
      if (!controller.signal.aborted) {
        setExportError(err instanceof Error ? err.message : 'Export failed');
      }
    }
  };

  const activeProject =
    route.name === 'editor'
      ? projects.find((p) => p.id === route.projectId) || projects[0] || null
      : null;

  return (
    <>
      {route.name === 'welcome' && (
        <WelcomeScreen onOpenStudio={() => setRoute({ name: 'projects' })} />
      )}

      {route.name === 'projects' && (
        <ProjectsScreen
          projects={projects}
          onOpenProject={(projectId) => setRoute({ name: 'editor', projectId })}
          onOpenPicker={() => setRoute({ name: 'picker' })}
          onOpenCamera={() => setRoute({ name: 'camera' })}
          onQuickCreateFromMedia={createProjectFromMedia}
          onRenameProject={async (id, name) => {
            await renameProjectById(id, name);
            setProjects(await listAllProjects());
          }}
          onDeleteProject={async (id) => {
            await deleteProjectById(id);
            setProjects(await listAllProjects());
          }}
        />
      )}

      {route.name === 'picker' && (
        <MediaPickerScreen
          onBack={() => setRoute({ name: 'projects' })}
          onContinue={createProjectFromMedia}
        />
      )}

      {route.name === 'camera' && (
        <CameraCaptureScreen
          onBack={() => setRoute({ name: 'projects' })}
          onCaptured={(item) => createProjectFromMedia([item])}
        />
      )}

      {route.name === 'editor' && activeProject && (
        <EditorScreen
          project={activeProject}
          onSaveProject={handleSaveProject}
          onClose={() => setRoute({ name: 'projects' })}
          onExport={handleExport}
        />
      )}

      {route.name === 'finished' && (
        <FinishedScreen
          exportedUrl={route.exportedUrl}
          exportedFileName={route.exportedFileName}
          onGoToHome={() => setRoute({ name: 'projects' })}
        />
      )}

      <ExportProgressModal
        visible={exportVisible}
        progress={exportProgress}
        statusMessage={exportStatus}
        error={exportError}
        onCancel={() => {
          exportAbortRef.current?.abort();
          setExportVisible(false);
        }}
        onDismissError={() => setExportError(null)}
      />
    </>
  );
}

export default App;
