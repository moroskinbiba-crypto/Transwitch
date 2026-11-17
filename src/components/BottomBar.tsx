import React from 'react';
import {
  Wrench,
  Settings as SettingsIcon,
  Image as ImageIcon,
  Gamepad2,
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
  const actions = [
    {
      label: 'Tools',
      title: 'Каталог, виджеты, история и инструменты',
      icon: Wrench,
      gradient: 'from-[#0051d5] via-[#007aff] to-[#38a5ff]',
      shadow: 'rgba(0,122,255,0.42)',
      action: onOpenHub,
    },
    {
      label: 'Галерея',
      title: 'Скриншоты и видеозаписи Nintendo Switch',
      icon: ImageIcon,
      gradient: 'from-[#159957] via-[#30c878] to-[#62d8c0]',
      shadow: 'rgba(48,209,88,0.36)',
      action: onOpenAlbum,
    },
    {
      label: 'Настройки',
      title: 'Настройки Transwitch',
      icon: SettingsIcon,
      gradient: 'from-[#343a46] via-[#454e60] to-[#68758a]',
      shadow: 'rgba(70,85,110,0.36)',
      action: onOpenSettings,
    },
    {
      label: 'Pads',
      title: 'Подключение и настройка контроллеров',
      icon: Gamepad2,
      gradient: 'from-[#d8273f] via-[#7e355e] to-[#167ac0]',
      shadow: 'rgba(230,0,18,0.32)',
      action: onOpenControllers,
    },
  ] as const;

  return (
    <div className="w-full pb-3 px-4 flex items-center justify-center z-30 select-none pointer-events-none">
      <div className="pointer-events-auto flex items-center justify-center gap-3 sm:gap-5 px-4 sm:px-7 py-3 rounded-[30px] bg-white/[0.12] dark:bg-black/50 backdrop-blur-3xl border border-white/20 border-t-white/35 shadow-[0_20px_50px_rgba(0,0,0,0.7)]">
        {actions.map(({ label, title, icon: Icon, gradient, shadow, action }) => (
          <button
            key={label}
            type="button"
            onClick={() => {
              sound.playSelect();
              action();
            }}
            className="group relative flex min-w-[58px] flex-col items-center gap-1 rounded-2xl p-1.5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-sky-300"
            title={title}
            aria-label={title}
          >
            <span
              className={`flex h-12 w-12 sm:h-14 sm:w-14 items-center justify-center rounded-[19px] border border-white/25 border-t-white/40 bg-gradient-to-tr ${gradient} text-white transition-transform duration-200 group-hover:scale-105 group-active:scale-95`}
              style={{ boxShadow: `0 8px 22px ${shadow}` }}
            >
              <Icon className="h-6 w-6 sm:h-7 sm:w-7" strokeWidth={2.1} />
            </span>
            <span className="text-[10px] sm:text-[11px] font-extrabold text-white/90 drop-shadow-md">
              {label}
            </span>
          </button>
        ))}
      </div>
    </div>
  );
};
