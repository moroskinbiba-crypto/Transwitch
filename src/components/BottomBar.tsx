import React from 'react';
import { 
  Sparkles, 
  Settings as SettingsIcon, 
  Image as ImageIcon, 
  Gamepad2
} from 'lucide-react';
import { sound } from '../data/mockCatalog';

interface BottomBarProps {
  onOpenHub: () => void;
  onOpenAlbum: () => void;
  onOpenSettings: () => void;
  onOpenControllers: () => void;
}

export const BottomBar: React.FC<BottomBarProps> = ({
  onOpenHub,
  onOpenAlbum,
  onOpenSettings,
  onOpenControllers,
}) => {
  return (
    <div className="w-full pb-3 px-4 flex flex-col items-center justify-end z-30 select-none pointer-events-none">
      
      {/* Authentic Switch / iOS Floating Frosted Glass Dock */}
      <div className="pointer-events-auto flex items-center justify-center gap-4 md:gap-6 px-6 md:px-8 py-3 rounded-[34px] bg-white/[0.12] dark:bg-black/50 backdrop-blur-3xl border border-white/20 border-t-white/35 shadow-[0_20px_50px_rgba(0,0,0,0.7)]">
        
        {/* 1. Каталог (Иконка звёздочки) */}
        <button
          onClick={() => {
            sound.playSelect();
            onOpenHub();
          }}
          className="group relative flex flex-col items-center focus:outline-none"
          title="Каталог игр и приложений"
        >
          <div className="w-13 h-13 md:w-14 md:h-14 rounded-[22px] bg-gradient-to-tr from-[#0051d5] via-[#007aff] to-[#38a5ff] flex items-center justify-center text-white shadow-[0_8px_20px_rgba(0,122,255,0.45)] group-hover:scale-108 group-active:scale-92 transition-all duration-200 border-t border-white/35">
            <Sparkles className="w-6 h-6 fill-white/20 stroke-[2.2]" />
          </div>
          <span className="text-[10px] font-bold text-white/90 mt-1 drop-shadow-md">
            Каталог
          </span>
        </button>

        {/* 2. Галерея (Реальный Альбом Switch: скриншоты и видео) */}
        <button
          onClick={() => {
            sound.playSelect();
            onOpenAlbum();
          }}
          className="group relative flex flex-col items-center focus:outline-none"
          title="Альбом снимков и видеозаписей экрана"
        >
          <div className="w-13 h-13 md:w-14 md:h-14 rounded-[22px] bg-gradient-to-tr from-[#34c759] via-[#30d158] to-[#63e6e2] flex items-center justify-center text-white shadow-[0_8px_20px_rgba(52,199,89,0.45)] group-hover:scale-108 group-active:scale-92 transition-all duration-200 border-t border-white/35">
            <ImageIcon className="w-6 h-6" />
          </div>
          <span className="text-[10px] font-bold text-white/90 mt-1 drop-shadow-md">
            Галерея
          </span>
        </button>

        {/* 3. Системные Настройки Switch */}
        <button
          onClick={() => {
            sound.playClick();
            onOpenSettings();
          }}
          className="group relative flex flex-col items-center focus:outline-none"
          title="Системные настройки Nintendo Switch"
        >
          <div className="w-13 h-13 md:w-14 md:h-14 rounded-[22px] bg-gradient-to-tr from-[#1c1c1e] via-[#3a3a3c] to-[#636366] flex items-center justify-center text-white shadow-[0_8px_20px_rgba(0,0,0,0.5)] group-hover:scale-108 group-active:scale-92 transition-all duration-200 border border-white/20 border-t-white/40">
            <SettingsIcon className="w-6 h-6" />
          </div>
          <span className="text-[10px] font-bold text-white/90 mt-1 drop-shadow-md">
            Настройки
          </span>
        </button>

        {/* 4. Контроллеры (Системное меню контроллеров Switch) */}
        <button
          onClick={() => {
            sound.playSelect();
            onOpenControllers();
          }}
          className="group relative flex flex-col items-center focus:outline-none"
          title="Контроллеры Nintendo Switch"
        >
          <div className="w-13 h-13 md:w-14 md:h-14 rounded-[22px] bg-gradient-to-tr from-[#e60012] via-[#2c2c2e] to-[#00b4e6] flex items-center justify-center text-white shadow-[0_8px_20px_rgba(230,0,18,0.35)] group-hover:scale-108 group-active:scale-92 transition-all duration-200 border border-white/20 border-t-white/40">
            <Gamepad2 className="w-6 h-6 text-white" />
          </div>
          <span className="text-[10px] font-bold text-white/90 mt-1 drop-shadow-md">
            Контроллеры
          </span>
        </button>

      </div>
    </div>
  );
};
