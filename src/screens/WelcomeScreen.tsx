import { ArrowRight, ShieldCheck } from 'lucide-react';
import { TrisvaxBrandBadge } from '../components/TrisvaxBrand';

export function WelcomeScreen({ onOpenStudio }: { onOpenStudio: () => void }) {
  return (
    <div className="relative flex min-h-screen w-full flex-col items-center justify-between bg-[#0B0807] px-6 py-8">
      <div className="w-full max-w-4xl">
        <TrisvaxBrandBadge />
      </div>

      <div className="w-full max-w-[380px] rounded-[18px] border border-[#2B352D] bg-[#18110D] px-7 py-8">
        <h1 className="text-[28px] font-bold tracking-tight text-white">Start creating</h1>
        <p className="mt-2.5 text-sm leading-relaxed text-[#9AA69B]">
          Trim clips, layer music, color grade, and export crisp 1080p edits locally with the C++20 engine.
        </p>
        <button
          type="button"
          onClick={onOpenStudio}
          className="mt-7 flex min-h-[52px] w-full items-center justify-center gap-2 rounded-xl bg-[#FF5500] px-5 py-3 text-[15px] font-bold text-white transition-colors hover:bg-[#FF8232]"
        >
          <span>Open your studio</span>
          <ArrowRight className="h-4 w-4" />
        </button>
      </div>

      <div className="flex w-full max-w-[380px] items-center justify-center gap-2.5 rounded-xl border border-[#2B352D] bg-[#18110D] px-4 py-3 text-center">
        <ShieldCheck className="h-4 w-4 shrink-0 text-[#FF5500]" />
        <p className="text-xs text-[#9AA69B]">
          No account. No uploads. Your media stays on your device.
        </p>
      </div>
    </div>
  );
}
