import { Check, Download, FolderCode } from 'lucide-react';
import { TrisvaxBrandBadge } from '../components/TrisvaxBrand';

export function ExportProgressModal({
  visible,
  progress,
  statusMessage,
  error,
  onCancel,
  onDismissError,
}: {
  visible: boolean;
  progress: number;
  statusMessage: string;
  error: string | null;
  onCancel: () => void;
  onDismissError: () => void;
}) {
  if (!visible && !error) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4">
      <div className="w-full max-w-sm rounded-[18px] border border-[#2B352D] bg-[#18110D] p-6">
        {error ? (
          <>
            <h2 className="text-lg font-bold text-[#FF5A5A]">Export Failed</h2>
            <p className="mt-2 text-sm text-[#9AA69B]">{error}</p>
            <button
              type="button"
              onClick={onDismissError}
              className="mt-5 min-h-[44px] w-full rounded-xl bg-[#FF5500] text-sm font-bold text-white"
            >
              Close
            </button>
          </>
        ) : (
          <>
            <h2 className="text-lg font-bold text-white">Exporting 1080p MP4</h2>
            <p className="mt-1.5 text-xs text-[#9AA69B]">{statusMessage}</p>
            <div className="mt-4 h-2.5 w-full overflow-hidden rounded-full bg-[#221711]">
              <div
                style={{ width: `${Math.max(4, Math.min(100, progress))}%` }}
                className="h-full bg-[#FF5500] transition-all duration-200"
              />
            </div>
            <p className="mt-2 text-xs font-semibold text-[#FF8232] font-mono-tabular">
              {progress}%
            </p>
            <button
              type="button"
              onClick={onCancel}
              className="mt-5 min-h-[44px] w-full rounded-xl border border-[#2B352D] bg-[#221711] text-sm font-semibold text-white hover:border-[#FF5500]"
            >
              Cancel Export
            </button>
          </>
        )}
      </div>
    </div>
  );
}

export function FinishedScreen({
  exportedUrl,
  exportedFileName,
  onGoToHome,
}: {
  exportedUrl?: string;
  exportedFileName?: string;
  onGoToHome: () => void;
}) {
  return (
    <div className="relative flex min-h-screen w-full flex-col justify-between bg-[#0B0807] px-6 py-8">
      <div className="mx-auto w-full max-w-4xl">
        <TrisvaxBrandBadge />
      </div>

      <div className="mx-auto w-full max-w-[390px] rounded-[18px] border border-[#2B352D] bg-[#18110D] px-6 py-8 text-center">
        <div className="mx-auto flex h-16 w-16 items-center justify-center rounded-full bg-[#FF5500]">
          <Check className="h-8 w-8 stroke-[2.8] text-white" />
        </div>
        <h1 className="mt-5 text-xl font-bold text-white">Your project has been saved</h1>
        <p className="mt-2 text-xs leading-relaxed text-[#9AA69B]">
          1080p H.264/AAC MP4 exported locally with no watermark.
        </p>

        <div className="mt-6 space-y-2.5">
          {exportedUrl && (
            <a
              href={exportedUrl}
              download={exportedFileName || 'TRISVAX_1080p.mp4'}
              className="flex min-h-[46px] w-full items-center justify-center gap-2 rounded-xl border border-[#2B352D] bg-[#221711] px-4 text-sm font-semibold text-white hover:border-[#FF5500]"
            >
              <Download className="h-4 w-4 text-[#FF5500]" />
              <span>Download MP4</span>
            </a>
          )}

          <button
            type="button"
            onClick={onGoToHome}
            className="flex min-h-[50px] w-full items-center justify-center rounded-xl bg-[#FF5500] text-sm font-bold text-white transition-colors hover:bg-[#FF8232]"
          >
            Go to Home
          </button>

          <a
            href="/api/android-project.zip"
            download="trisvax-studio-android-cpp.zip"
            className="flex min-h-[44px] w-full items-center justify-center gap-2 rounded-xl border border-[#2B352D] bg-[#0B0807] px-4 text-xs font-medium text-[#9AA69B] hover:border-[#FF5500] hover:text-white"
          >
            <FolderCode className="h-4 w-4 text-[#FF5500]" />
            <span>Download C++20 Android NDK Project (.zip)</span>
          </a>
        </div>
      </div>

      <div />
    </div>
  );
}
