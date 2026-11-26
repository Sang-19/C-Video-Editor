import { Clapperboard } from 'lucide-react';

export function TrisvaxBrandBadge() {
  return (
    <div className="inline-flex items-center gap-2.5 rounded-[10px] border border-[#2B352D] bg-[#18110D] px-2.5 py-1.5">
      <div className="flex h-7 w-7 items-center justify-center rounded-[7px] bg-[#FF5500] text-[17px] font-extrabold text-[#0B0807]">
        T
      </div>
      <div className="flex items-center gap-1.5 text-sm tracking-[0.08em]">
        <span className="font-bold text-white">TRISVAX</span>
        <span className="font-medium text-[#9AA69B]">STUDIO</span>
      </div>
    </div>
  );
}

export function TrisvaxClapperIcon() {
  return (
    <div className="flex h-[34px] w-[34px] items-center justify-center rounded-[9px] bg-[#FF5500] text-[#0B0807]">
      <Clapperboard className="h-5 w-5 stroke-[2.2]" />
    </div>
  );
}
