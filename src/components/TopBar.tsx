import React, { useState, useRef } from 'react';
import { Workspace, UserProfile } from '../types/desknx';
import { MAX_DESKS } from '../utils/desknxLayout';
import { sound } from '../data/mockCatalog';
import { 
  Wifi, 
  Plus, 
  Trash2, 
  Check, 
  ChevronDown,
  ShieldCheck,
  Gamepad2,
  Activity,
  Sliders
} from 'lucide-react';

interface TopBarProps {
  workspaces: Workspace[];
  activeWorkspaceIndex: number;
  onSelectWorkspace: (index: number) => void;
  onAddWorkspace: (name: string) => void;
  onRenameWorkspace: (index: number, newName: string) => void;
  onDeleteWorkspace: (index: number) => void;
  onOpenUserProfiles: () => void;
  onOpenDeskCustomizer?: () => void;
  onOpenLauncherSettings?: () => void;
  activeUser: UserProfile;
  homeInstalled: boolean;
}

export const TopBar: React.FC<TopBarProps> = ({
  workspaces,
  activeWorkspaceIndex,
  onSelectWorkspace,
  onAddWorkspace,
  onRenameWorkspace,
  onDeleteWorkspace,
  onOpenUserProfiles,
  onOpenDeskCustomizer,
  onOpenLauncherSettings,
  activeUser,
  homeInstalled,
}) => {
  const [editingIndex, setEditingIndex] = useState<number | null>(null);
  const [editName, setEditName] = useState('');
  const [now, setNow] = useState(new Date());

  // Hold timer ref for opening Desktop Customizer on chosen workspace tab
  const holdTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const isHoldTriggeredRef = useRef(false);

  React.useEffect(() => {
    const timer = setInterval(() => setNow(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const handleStartRename = (index: number, currentName: string, e: React.MouseEvent) => {
    e.stopPropagation();
    sound.playClick();
    setEditingIndex(index);
    setEditName(currentName);
  };

  const handleSaveRename = (index: number) => {
    if (editName.trim()) {
      sound.playSelect();
      onRenameWorkspace(index, editName.trim());
    }
    setEditingIndex(null);
  };

  const handleCreateDesk = () => {
    if (workspaces.length >= MAX_DESKS) {
      sound.playBack();
      alert(`Максимум ${MAX_DESKS} столов.`);
      return;
    }
    sound.playSelect();
    onAddWorkspace(`Страница ${workspaces.length + 1}`);
  };

  const handleDeleteDesk = (index: number, e: React.MouseEvent) => {
    e.stopPropagation();
    if (workspaces.length <= 1) return;
    if (confirm(`Удалить рабочий стол "${workspaces[index].name}"?`)) {
      sound.playBack();
      onDeleteWorkspace(index);
    }
  };

  const handleTabPointerDown = (index: number) => {
    isHoldTriggeredRef.current = false;
    if (index === activeWorkspaceIndex && onOpenDeskCustomizer) {
      holdTimerRef.current = setTimeout(() => {
        isHoldTriggeredRef.current = true;
        sound.playSelect();
        if (navigator.vibrate) navigator.vibrate(60);
        onOpenDeskCustomizer();
      }, 550);
    }
  };

  const handleTabPointerUp = () => {
    if (holdTimerRef.current) {
      clearTimeout(holdTimerRef.current);
      holdTimerRef.current = null;
    }
  };

  const hours = now.getHours().toString().padStart(2, '0');
  const minutes = now.getMinutes().toString().padStart(2, '0');

  return (
    <div className="w-full h-18 md:h-20 px-4 md:px-8 flex items-center justify-between z-30 select-none text-white pt-1.5">
      {/* Left: Status Bar Time & User Profile Pill */}
      <div className="flex items-center gap-3.5 md:gap-4">
        {/* Apple / Switch Status Bar Time */}
        <span className="text-xl md:text-2xl font-black tracking-tight font-sans text-white drop-shadow-md">
          {hours}:{minutes}
        </span>

        {/* User Switcher Pill - Significantly larger for 720p/1080p touch & docked */}
        <button
          onClick={() => {
            sound.playSelect();
            onOpenUserProfiles();
          }}
          className="flex items-center gap-2.5 px-3.5 py-1.5 md:px-4 md:py-2 rounded-full bg-white/10 hover:bg-white/20 backdrop-blur-2xl border border-white/20 shadow-md transition-all active:scale-95 group"
          title="Сменить профиль игрока"
        >
          <div className="w-8 h-8 md:w-9 md:h-9 rounded-full overflow-hidden ring-2 ring-white/40 shadow-sm shrink-0">
            <img
              src={activeUser.avatar}
              alt={activeUser.name}
              className="w-full h-full object-cover"
            />
          </div>
          <span className="text-sm md:text-base font-extrabold text-white group-hover:text-blue-300 transition-colors">
            {activeUser.name}
          </span>
          <ChevronDown className="w-4 h-4 text-zinc-400 group-hover:text-white" />
        </button>

        {homeInstalled && (
          <span className="hidden xl:flex items-center gap-1.5 text-xs font-extrabold bg-emerald-500/20 text-emerald-300 border border-emerald-400/30 px-3.5 py-1.5 rounded-full shadow-sm">
            <ShieldCheck className="w-4 h-4" /> HOME Меню
          </span>
        )}
      </div>

      {/* Right: Workspaces Switcher, Network, Battery & Discreet Launcher Settings */}
      <div className="flex items-center gap-2.5 md:gap-3.5">
        {/* iOS Page Switcher Pills with Long-Press Desktop Settings */}
        <div className="hidden md:flex items-center gap-1.5 bg-black/50 backdrop-blur-3xl px-2.5 py-1.5 rounded-full border border-white/15 shadow-md">
          {workspaces.map((desk, idx) => {
            const isActive = idx === activeWorkspaceIndex;
            const isEditing = editingIndex === idx;

            return (
              <div
                key={desk.id}
                onPointerDown={() => handleTabPointerDown(idx)}
                onPointerUp={handleTabPointerUp}
                onPointerLeave={handleTabPointerUp}
                onPointerCancel={handleTabPointerUp}
                onContextMenu={(e) => {
                  e.preventDefault();
                  if (isActive && onOpenDeskCustomizer) {
                    sound.playSelect();
                    onOpenDeskCustomizer();
                  }
                }}
                onClick={() => {
                  if (isHoldTriggeredRef.current) return;
                  if (!isActive) {
                    sound.playNav();
                    onSelectWorkspace(idx);
                  }
                }}
                title={isActive ? 'Текущий стол (Удерживайте для настроек стола)' : `Перейти на ${desk.name}`}
                className={`relative flex items-center gap-2 px-3.5 py-1.5 rounded-full cursor-pointer text-xs md:text-sm font-extrabold transition-all select-none ${
                  isActive
                    ? 'bg-white text-black shadow-lg'
                    : 'text-zinc-400 hover:text-white hover:bg-white/10'
                }`}
              >
                {isEditing ? (
                  <div className="flex items-center gap-1.5" onClick={(e) => e.stopPropagation()}>
                    <input
                      type="text"
                      value={editName}
                      onChange={(e) => setEditName(e.target.value)}
                      onKeyDown={(e) => {
                        if (e.key === 'Enter') handleSaveRename(idx);
                        if (e.key === 'Escape') setEditingIndex(null);
                      }}
                      autoFocus
                      className="w-24 bg-black/80 text-white px-2 py-0.5 rounded text-xs outline-none"
                    />
                    <button
                      onClick={() => handleSaveRename(idx)}
                      className="text-emerald-400 hover:text-emerald-300"
                    >
                      <Check className="w-4 h-4" />
                    </button>
                  </div>
                ) : (
                  <>
                    <span onDoubleClick={(e) => handleStartRename(idx, desk.name, e)}>
                      {desk.name}
                    </span>
                    {isActive && (
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          if (onOpenDeskCustomizer) {
                            sound.playSelect();
                            onOpenDeskCustomizer();
                          }
                        }}
                        className="text-zinc-600 hover:text-black ml-1 transition-colors p-0.5 rounded"
                        title="Настройки стола"
                      >
                        <Sliders className="w-3.5 h-3.5" />
                      </button>
                    )}
                    {workspaces.length > 1 && isActive && (
                      <button
                        onClick={(e) => handleDeleteDesk(idx, e)}
                        className="hover:text-red-600 transition-colors ml-1"
                        title="Удалить страницу"
                      >
                        <Trash2 className="w-3.5 h-3.5" />
                      </button>
                    )}
                  </>
                )}
              </div>
            );
          })}

          {workspaces.length < MAX_DESKS && (
            <button
              onClick={handleCreateDesk}
              className="p-1.5 rounded-full hover:bg-white/20 text-zinc-400 hover:text-white transition-all"
              title="Добавить страницу"
            >
              <Plus className="w-4 h-4" />
            </button>
          )}
        </div>

        {/* Network & Wi-Fi - Larger for Switch visibility */}
        <div className="flex items-center gap-2 px-1">
          <div className="flex items-end gap-1 h-4 px-0.5">
            <span className="w-1 h-1.5 bg-white rounded-full" />
            <span className="w-1 h-2.5 bg-white rounded-full" />
            <span className="w-1 h-3.5 bg-white rounded-full" />
            <span className="w-1 h-4 bg-white rounded-full" />
          </div>
          <Wifi className="w-5 h-5 text-white drop-shadow" />
        </div>

        {/* Battery Capsule - Larger & crisp */}
        <div className="flex items-center gap-2">
          <span className="text-sm md:text-base font-black font-sans text-white drop-shadow">88%</span>
          <div className="relative flex items-center">
            <div className="w-8 h-4.5 rounded-[5px] border-2 border-white/70 p-[2px] flex items-center bg-black/40 shadow-sm">
              <div 
                className="h-full bg-[#34c759] rounded-[2px] transition-all"
                style={{ width: '88%' }}
              />
            </div>
            <div className="w-[2.5px] h-2 bg-white/70 rounded-r-[1px] ml-[1.5px]" />
          </div>
        </div>

        {/* Discreet Launcher Settings Button (не бросается в глаза, но удобно нажимать) */}
        {onOpenLauncherSettings && (
          <button
            onClick={() => {
              sound.playClick();
              onOpenLauncherSettings();
            }}
            className="p-2 rounded-full text-white/40 hover:text-white hover:bg-white/15 transition-all active:scale-95 ml-1"
            title="Настройки DeskNX лаунчера"
          >
            <Sliders className="w-5 h-5" />
          </button>
        )}
      </div>
    </div>
  );
};
