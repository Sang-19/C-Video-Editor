import React, { useRef, useState } from 'react';
import {
  Camera,
  Film,
  Images,
  MoreVertical,
  Play,
  Plus,
  Search,
  Video,
  X,
} from 'lucide-react';
import { TrisvaxClapperIcon } from '../components/TrisvaxBrand';
import {
  formatDurationBadge,
  formatRelativeTime,
  formatStorageBytes,
} from '../engine/timelineMath';
import {
  extractImageThumbnail,
  extractVideoMetadata,
  extractVideoThumbnails,
} from '../engine/videoMediaUtils';
import { saveMediaBlob } from '../storage/projectDb';
import type { DeviceMediaItem, ProjectRecord } from '../types/editor';

export function ProjectsScreen({
  projects,
  onOpenProject,
  onOpenPicker,
  onOpenCamera,
  onQuickCreateFromMedia,
  onRenameProject,
  onDeleteProject,
}: {
  projects: ProjectRecord[];
  onOpenProject: (projectId: string) => void;
  onOpenPicker: () => void;
  onOpenCamera: () => void;
  onQuickCreateFromMedia: (items: DeviceMediaItem[]) => void;
  onRenameProject: (projectId: string, name: string) => void;
  onDeleteProject: (projectId: string) => void;
}) {
  const [searchOpen, setSearchOpen] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [createPopupOpen, setCreatePopupOpen] = useState(false);
  const [menuOpenId, setMenuOpenId] = useState<string | null>(null);
  const [renameTarget, setRenameTarget] = useState<ProjectRecord | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const [deleteTarget, setDeleteTarget] = useState<ProjectRecord | null>(null);
  const quickVideoInputRef = useRef<HTMLInputElement | null>(null);

  const filtered = searchQuery.trim()
    ? projects.filter((p) =>
        p.name.toLowerCase().includes(searchQuery.trim().toLowerCase())
      )
    : projects;

  const totalStorage = projects.reduce((sum, p) => sum + (p.storageSizeBytes || 0), 0);

  const handleQuickVideoFiles = async (files: FileList) => {
    const items: DeviceMediaItem[] = [];
    for (const file of Array.from(files)) {
      const id = `quick_${Date.now()}_${Math.random().toString(36).slice(2, 7)}`;
      await saveMediaBlob(id, file);
      const url = URL.createObjectURL(file);
      const isVideo = file.type.startsWith('video/');
      if (isVideo) {
        const meta = await extractVideoMetadata(url);
        const thumbs = await extractVideoThumbnails(url, meta.durationMs, 1);
        items.push({
          id,
          url,
          displayName: file.name,
          isVideo: true,
          durationMs: meta.durationMs,
          thumbnailUrl: thumbs[0] || '',
          sizeBytes: file.size,
          mimeType: file.type,
        });
      } else {
        const thumb = await extractImageThumbnail(url);
        items.push({
          id,
          url,
          displayName: file.name,
          isVideo: false,
          durationMs: 3000,
          thumbnailUrl: thumb,
          sizeBytes: file.size,
          mimeType: file.type,
        });
      }
    }
    if (items.length > 0) {
      onQuickCreateFromMedia(items);
    }
  };

  return (
    <div className="relative min-h-screen w-full bg-[#0B0807] px-4 pb-28 pt-5">
      <div className="mx-auto max-w-5xl">
        {/* Clean Projects Header: Clapper + Projects + Search Toggle */}
        <header className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <TrisvaxClapperIcon />
            <h1 className="text-[26px] font-bold tracking-tight text-white">Projects</h1>
          </div>
          <button
            type="button"
            aria-label="Search projects"
            onClick={() => {
              setSearchOpen((prev) => !prev);
              if (searchOpen) setSearchQuery('');
            }}
            className="flex h-11 w-11 items-center justify-center rounded-xl text-white hover:bg-[#18110D]"
          >
            {searchOpen ? <X className="h-5 w-5" /> : <Search className="h-5 w-5" />}
          </button>
        </header>

        <p className="mt-1.5 text-[13px] text-[#9AA69B] font-mono-tabular">
          {projects.length} {projects.length === 1 ? 'project' : 'projects'} •{' '}
          {formatStorageBytes(totalStorage)}
        </p>

        {searchOpen && (
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Search projects by name..."
            autoFocus
            className="mt-3 w-full rounded-xl border border-[#2B352D] bg-[#18110D] px-3.5 py-2.5 text-sm text-white placeholder-[#667568] outline-none focus:border-[#FF5500]"
          />
        )}

        {/* 2-Column Portrait Grid */}
        {filtered.length === 0 ? (
          <div className="flex min-h-[55vh] flex-col items-center justify-center text-center">
            <p className="text-lg font-semibold text-white">
              {searchQuery.trim() ? 'No matching projects' : 'No projects yet'}
            </p>
            <p className="mt-1.5 text-sm text-[#9AA69B]">
              Tap + to import clips or capture video.
            </p>
          </div>
        ) : (
          <div className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-3 md:grid-cols-4">
            {filtered.map((project) => (
              <div
                key={project.id}
                className="group relative overflow-hidden rounded-2xl border border-[#2B352D] bg-[#18110D]"
              >
                <div
                  onClick={() => onOpenProject(project.id)}
                  className="relative aspect-[0.95] w-full cursor-pointer overflow-hidden bg-[#221711]"
                >
                  {project.thumbnailUrl ? (
                    <img
                      src={project.thumbnailUrl}
                      alt={project.name}
                      className="h-full w-full object-cover transition-transform duration-300 group-hover:scale-105"
                    />
                  ) : (
                    <div className="flex h-full w-full items-center justify-center bg-gradient-to-b from-[#221711] to-[#0B0807]">
                      <Play className="h-8 w-8 text-[#FF5500]" />
                    </div>
                  )}

                  <div className="pointer-events-none absolute left-2 top-2 flex items-center gap-1 rounded-lg bg-black/65 px-2 py-1 text-[11px] font-medium text-white font-mono-tabular">
                    <Film className="h-3 w-3" />
                    <span>{formatDurationBadge(project.durationMs)}</span>
                  </div>
                </div>

                {/* 3-dot Menu */}
                <div className="absolute right-1 top-1">
                  <button
                    type="button"
                    aria-label={`Options for ${project.name}`}
                    onClick={(e) => {
                      e.stopPropagation();
                      setMenuOpenId((cur) => (cur === project.id ? null : project.id));
                    }}
                    className="flex h-8 w-8 items-center justify-center rounded-full bg-black/45 text-white hover:bg-black/75"
                  >
                    <MoreVertical className="h-4 w-4" />
                  </button>
                  {menuOpenId === project.id && (
                    <div className="absolute right-0 top-9 z-30 w-32 rounded-xl border border-[#2B352D] bg-[#221711] py-1 shadow-xl">
                      <button
                        type="button"
                        onClick={() => {
                          setMenuOpenId(null);
                          setRenameTarget(project);
                          setRenameValue(project.name);
                        }}
                        className="block w-full px-3 py-2 text-left text-xs font-medium text-white hover:bg-[#18110D]"
                      >
                        Rename
                      </button>
                      <button
                        type="button"
                        onClick={() => {
                          setMenuOpenId(null);
                          setDeleteTarget(project);
                        }}
                        className="block w-full px-3 py-2 text-left text-xs font-medium text-[#FF5A5A] hover:bg-[#18110D]"
                      >
                        Delete
                      </button>
                    </div>
                  )}
                </div>

                <div
                  onClick={() => onOpenProject(project.id)}
                  className="cursor-pointer px-3 py-2.5"
                >
                  <p className="truncate text-sm font-semibold text-white">{project.name}</p>
                  <div className="my-1.5 h-0.5 w-6 rounded bg-[#FF5500]" />
                  <p className="truncate text-[11px] text-[#9AA69B] font-mono-tabular">
                    {formatRelativeTime(project.lastModifiedAt)} •{' '}
                    {formatStorageBytes(project.storageSizeBytes)}
                  </p>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Create Popup Backdrop */}
      {createPopupOpen && (
        <div
          onClick={() => setCreatePopupOpen(false)}
          className="fixed inset-0 z-30 bg-black/45"
        />
      )}

      {/* Floating + Action Button & 3-Item Create Popup */}
      <div className="fixed bottom-6 right-5 z-40 flex flex-col items-end gap-3">
        {createPopupOpen && (
          <div className="w-[196px] overflow-hidden rounded-2xl border border-[#2B352D] bg-[#18110D] py-1.5 shadow-2xl">
            <button
              type="button"
              onClick={() => {
                setCreatePopupOpen(false);
                quickVideoInputRef.current?.click();
              }}
              className="flex w-full items-center gap-3.5 px-4 py-3 text-left text-[15px] font-medium text-white hover:bg-[#221711]"
            >
              <Video className="h-5 w-5 text-[#FF5500]" />
              <span>Video</span>
            </button>
            <button
              type="button"
              onClick={() => {
                setCreatePopupOpen(false);
                onOpenCamera();
              }}
              className="flex w-full items-center gap-3.5 px-4 py-3 text-left text-[15px] font-medium text-white hover:bg-[#221711]"
            >
              <Camera className="h-5 w-5 text-[#FF5500]" />
              <span>Camera</span>
            </button>
            <button
              type="button"
              onClick={() => {
                setCreatePopupOpen(false);
                onOpenPicker();
              }}
              className="flex w-full items-center gap-3.5 px-4 py-3 text-left text-[15px] font-medium text-white hover:bg-[#221711]"
            >
              <Images className="h-5 w-5 text-[#FF5500]" />
              <span>Gallery</span>
            </button>
          </div>
        )}

        <button
          type="button"
          aria-label="Create new project"
          onClick={() => setCreatePopupOpen((prev) => !prev)}
          className="flex h-[60px] w-[60px] items-center justify-center rounded-[18px] bg-[#FF5500] text-white shadow-xl transition-colors hover:bg-[#FF8232]"
        >
          {createPopupOpen ? <X className="h-7 w-7" /> : <Plus className="h-7 w-7" />}
        </button>
      </div>

      <input
        ref={quickVideoInputRef}
        type="file"
        accept="video/*"
        multiple
        className="hidden"
        onChange={(e) => {
          if (e.target.files && e.target.files.length > 0) {
            handleQuickVideoFiles(e.target.files);
          }
          e.target.value = '';
        }}
      />

      {/* Rename Modal */}
      {renameTarget && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
          <div className="w-full max-w-sm rounded-2xl border border-[#2B352D] bg-[#18110D] p-5">
            <h2 className="text-base font-bold text-white">Rename project</h2>
            <input
              type="text"
              value={renameValue}
              onChange={(e) => setRenameValue(e.target.value)}
              className="mt-3 w-full rounded-xl border border-[#2B352D] bg-[#0B0807] px-3 py-2 text-sm text-white outline-none focus:border-[#FF5500]"
            />
            <div className="mt-4 flex justify-end gap-2">
              <button
                type="button"
                onClick={() => setRenameTarget(null)}
                className="min-h-[40px] rounded-xl px-4 text-sm font-medium text-[#9AA69B]"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={() => {
                  onRenameProject(renameTarget.id, renameValue);
                  setRenameTarget(null);
                }}
                className="min-h-[40px] rounded-xl bg-[#FF5500] px-4 text-sm font-semibold text-white"
              >
                Save
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Delete Modal */}
      {deleteTarget && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
          <div className="w-full max-w-sm rounded-2xl border border-[#2B352D] bg-[#18110D] p-5">
            <h2 className="text-base font-bold text-white">Delete project?</h2>
            <p className="mt-2 text-xs text-[#9AA69B]">
              This removes &ldquo;{deleteTarget.name}&rdquo; from your local TRISVAX Studio workspace.
            </p>
            <div className="mt-4 flex justify-end gap-2">
              <button
                type="button"
                onClick={() => setDeleteTarget(null)}
                className="min-h-[40px] rounded-xl px-4 text-sm font-medium text-[#9AA69B]"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={() => {
                  onDeleteProject(deleteTarget.id);
                  setDeleteTarget(null);
                }}
                className="min-h-[40px] rounded-xl bg-[#FF5A5A] px-4 text-sm font-semibold text-white"
              >
                Delete
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
