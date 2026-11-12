import React, { useState, useRef } from 'react';
import { Workspace, UserProfile } from '../types/desknx';
import { MAX_DESKS } from '../utils/desknxLayout';
import { sound } from '../data/mockCatalog';

import { notify } from '../ui/toast';
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
      notify(`Максимум ${MAX_DESKS} столов.`);
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

{"content":"﻿import React, { useState, useRef } from 'react';\r\nimport { Workspace, UserProfile } from '../types/desknx';\r\nimport { MAX_DESKS } from '../utils/desknxLayout';\r\nimport { sound } from '../data/mockCatalog';\r\n\r\nimport { notify } from '../ui/toast';\r\nimport { \r\n  Wifi, \r\n  Plus, \r\n  Trash2, \r\n  Check, \r\n  ChevronDown,\r\n  ShieldCheck,\r\n  Gamepad2,\r\n  Activity,\r\n  Sliders\r\n} from 'lucide-react';\r\n\r\ninterface TopBarProps {\r\n  workspaces: Workspace[];\r\n  activeWorkspaceIndex: number;\r\n  onSelectWorkspace: (index: number) => void;\r\n  onAddWorkspace: (name: string) => void;\r\n  onRenameWorkspace: (index: number, newName: string) => void;\r\n  onDeleteWorkspace: (index: number) => void;\r\n  onOpenUserProfiles: () => void;\r\n  onOpenDeskCustomizer?: () => void;\r\n  onOpenLauncherSettings?: () => void;\r\n  activeUser: UserProfile;\r\n  homeInstalled: boolean;\r\n}\r\n\r\nexport const TopBar: React.FC<TopBarProps> = ({\r\n  workspaces,\r\n  activeWorkspaceIndex,\r\n  onSelectWorkspace,\r\n  onAddWorkspace,\r\n  onRenameWorkspace,\r\n  onDeleteWorkspace,\r\n  onOpenUserProfiles,\r\n  onOpenDeskCustomizer,\r\n  onOpenLauncherSettings,\r\n  activeUser,\r\n  homeInstalled,\r\n}) => {\r\n  const [editingIndex, setEditingIndex] = useState<number | null>(null);\r\n  const [editName, setEditName] = useState('');\r\n  const [now, setNow] = useState(new Date());\r\n\r\n  // Hold timer ref for opening Desktop Customizer on chosen workspace tab\r\n  const holdTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);\r\n  const isHoldTriggeredRef = useRef(false);\r\n\r\n  React.useEffect(() => {\r\n    const timer = setInterval(() => setNow(new Date()), 1000);\r\n    return () => clearInterval(timer);\r\n  }, []);\r\n\r\n  const handleStartRename = (index: number, currentName: string, e: React.MouseEvent) => {\r\n    e.stopPropagation();\r\n    sound.playClick();\r\n    setEditingIndex(index);\r\n    setEditName(currentName);\r\n  };\r\n\r\n  const handleSaveRename = (index: number) => {\r\n    if (editName.trim()) {\r\n      sound.playSelect();\r\n      onRenameWorkspace(index, editName.trim());\r\n    }\r\n    setEditingIndex(null);\r\n  };\r\n\r\n  const handleCreateDesk = () => {\r\n    if (workspaces.length >= MAX_DESKS) {\r\n      sound.playBack();\r\n      notify(`Максимум ${MAX_DESKS} столов.`);\r\n      return;\r\n    }\r\n    sound.playSelect();\r\n    onAddWorkspace(`Страница ${workspaces.length + 1}`);\r\n  };\r\n\r\n  const handleDeleteDesk = (index: number, e: React.MouseEvent) => {\r\n    e.stopPropagation();\r\n    if (workspaces.length <= 1) return;\r\n    if (confirm(`Удалить рабочий стол \"${workspaces[index].name}\"?`)) {\r\n      sound.playBack();\r\n      onDeleteWorkspace(index);\r\n    }\r\n  };\r\n\r\n  const handleTabPointerDown = (index: number) => {\r\n    isHoldTriggeredRef.current = false;\r\n    if (index === activeWorkspaceIndex && onOpenDeskCustomizer) {\r\n      holdTimerRef.current = setTimeout(() => {\r\n        isHoldTriggeredRef.current = true;\r\n        sound.playSelect();\r\n        if (navigator.vibrate) navigator.vibrate(60);\r\n        onOpenDeskCustomizer();\r\n      }, 550);\r\n    }\r\n  };\r\n\r\n  const handleTabPointerUp = () => {\r\n    if (holdTimerRef.current) {\r\n      clearTimeout(holdTimerRef.current);\r\n      holdTimerRef.current = null;\r\n    }\r\n  };\r\n\r\n  const hours = now.getHours().toString().padStart(2, '0');\r\n  const minutes = now.getMinutes().toString().padStart(2, '0');\r\n\r\n  return (\r\n    <div className=\"w-full h-18 md:h-20 px-4 md:px-8 flex items-center justify-between z-30 select-none text-white pt-1.5\">\r\n      {/* Left: Status Bar Time & User Profile Pill */}\r\n      <div className=\"flex items-center gap-3.5 md:gap-4\">\r\n        {/* Apple / Switch Status Bar Time */}\r\n        <span className=\"text-xl md:text-2xl font-black tracking-tight font-sans text-white drop-shadow-md\">\r\n          {hours}:{minutes}\r\n        </span>\r\n\r\n        {/* User Switcher Pill - Significantly larger for 720p/1080p touch & docked */}\r\n        <button\r\n          onClick={() => {\r\n            sound.playSelect();\r\n            onOpenUserProfiles();\r\n          }}\r\n          className=\"flex items-center gap-2.5 px-3.5 py-1.5 md:px-4 md:py-2 rounded-full bg-white/10 hover:bg-white/20 backdrop-blur-2xl border border-white/20 shadow-md transition-all active:scale-95 group\"\r\n          title=\"Сменить профиль игрока\"\r\n        >\r\n          <div className=\"w-8 h-8 md:w-9 md:h-9 rounded-full overflow-hidden ring-2 ring-white/40 shadow-sm shrink-0\">\r\n            <img\r\n              src={activeUser.avatar}\r\n              alt={activeUser.name}\r\n              className=\"w-full h-full object-cover\"\r\n            />\r\n          </div>\r\n          <span className=\"text-sm md:text-base font-extrabold text-white group-hover:text-blue-300 transition-colors\">\r\n            {activeUser.name}\r\n          </span>\r\n          <ChevronDown className=\"w-4 h-4 text-zinc-400 group-hover:text-white\" />\r\n        </button>\r\n\r\n        {homeInstalled && (\r\n          <span className=\"hidden xl:flex items-center gap-1.5 text-xs font-extrabold bg-emerald-500/20 text-emerald-300 border border-emerald-400/30 px-3.5 py-1.5 rounded-full shadow-sm\">\r\n            <ShieldCheck className=\"w-4 h-4\" /> HOME Меню\r\n          </span>\r\n        )}\r\n      </div>\r\n\r\n      {/* Right: Workspaces Switcher, Network, Battery & Discreet Launcher Settings */}\r\n      <div className=\"flex items-center gap-2.5 md:gap-3.5\">\r\n        {/* iOS Page Switcher Pills with Long-Press Desktop Settings */}\r\n        <div className=\"hidden md:flex items-center gap-1.5 bg-black/50 backdrop-blur-3xl px-2.5 py-1.5 rounded-full border border-white/15 shadow-md\">\r\n          {workspaces.map((desk, idx) => {\r\n            const isActive = idx === activeWorkspaceIndex;\r\n            const isEditing = editingIndex === idx;\r\n\r\n            return (\r\n              <div\r\n                key={desk.id}\r\n                onPointerDown={() => handleTabPointerDown(idx)}\r\n                onPointerUp={handleTabPointerUp}\r\n                onPointerLeave={handleTabPointerUp}\r\n                onPointerCancel={handleTabPointerUp}\r\n                onContextMenu={(e) => {\r\n                  e.preventDefault();\r\n                  if (isActive && onOpenDeskCustomizer) {\r\n                    sound.playSelect();\r\n                    onOpenDeskCustomizer();\r\n                  }\r\n                }}\r\n                onClick={() => {\r\n                  if (isHoldTriggeredRef.current) return;\r\n                  if (!isActive) {\r\n                    sound.playNav();\r\n                    onSelectWorkspace(idx);\r\n                  }\r\n                }}\r\n                title={isActive ? 'Текущий стол (Удерживайте для настроек стола)' : `Перейти на ${desk.name}`}\r\n                className={`relative flex items-center gap-2 px-3.5 py-1.5 rounded-full cursor-pointer text-xs md:text-sm font-extrabold transition-all select-none ${\r\n                  isActive\r\n                    ? 'bg-white text-black shadow-lg'\r\n                    : 'text-zinc-400 hover:text-white hover:bg-white/10'\r\n                }`}\r\n              >\r\n                {isEditing ? (\r\n                  <div className=\"flex items-center gap-1.5\" onClick={(e) => e.stopPropagation()}>\r\n                    <input\r\n                      type=\"text\"\r\n                      value={editName}\r\n                      onChange={(e) => setEditName(e.target.value)}\r\n                      onKeyDown={(e) => {\r\n                        if (e.key === 'Enter') handleSaveRename(idx);\r\n                        if (e.key === 'Escape') setEditingIndex(null);\r\n                      }}\r\n                      autoFocus\r\n                      className=\"w-24 bg-black/80 text-white px-2 py-0.5 rounded text-xs outline-none\"\r\n                    />\r\n                    <button\r\n                      onClick={() => handleSaveRename(idx)}\r\n                      className=\"text-emerald-400 hover:text-emerald-300\"\r\n                    >\r\n                      <Check className=\"w-4 h-4\" />\r\n                    </button>\r\n                  </div>\r\n                ) : (\r\n                  <>\r\n                    <span onDoubleClick={(e) => handleStartRename(idx, desk.name, e)}>\r\n                      {desk.name}\r\n                    </span>\r\n                    {isActive && (\r\n                      <button\r\n                        type=\"button\"\r\n                        onClick={(e) => {\r\n                          e.stopPropagation();\r\n                          if (onOpenDeskCustomizer) {\r\n                            sound.playSelect();\r\n                            onOpenDeskCustomizer();\r\n                          }\r\n                        }}\r\n                        className=\"text-zinc-600 hover:text-black ml-1 transition-colors p-0.5 rounded\"\r\n                        title=\"Настройки стола\"\r\n                      >\r\n                        <Sliders className=\"w-3.5 h-3.5\" />\r\n                      </button>\r\n                    )}\r\n                    {workspaces.length > 1 && isActive && (\r\n                      <button\r\n                        onClick={(e) => handleDeleteDesk(idx, e)}\r\n                        className=\"hover:text-red-600 transition-colors ml-1\"\r\n                        title=\"Удалить страницу\"\r\n                      >\r\n                        <Trash2 className=\"w-3.5 h-3.5\" />\r\n                      </button>\r\n                    )}\r\n                  </>\r\n                )}\r\n              </div>\r\n            );\r\n          })}\r\n\r\n          {workspaces.length < MAX_DESKS && (\r\n            <button\r\n              onClick={handleCreateDesk}\r\n              className=\"p-1.5 rounded-full hover:bg-white/20 text-zinc-400 hover:text-white transition-all\"\r\n              title=\"Добавить страницу\"\r\n            >\r\n              <Plus className=\"w-4 h-4\" />\r\n            </button>\r\n          )}\r\n        </div>\r\n\r\n        {/* Network & Wi-Fi - Larger for Switch visibility */}\r\n        <div className=\"flex items-center gap-2 px-1\">\r\n          <div className=\"flex items-end gap-1 h-4 px-0.5\">\r\n            <span className=\"w-1 h-1.5 bg-white rounded-full\" />\r\n            <span className=\"w-1 h-2.5 bg-white rounded-full\" />\r\n            <span className=\"w-1 h-3.5 bg-white rounded-full\" />\r\n            <span className=\"w-1 h-4 bg-white rounded-full\" />\r\n          </div>\r\n          <Wifi className=\"w-5 h-5 text-white drop-shadow\" />\r\n        </div>\r\n\r\n        {/* Battery Capsule - Larger & crisp */}\r\n        <div className=\"flex items-center gap-2\">\r\n          <span className=\"text-sm md:text-base font-black font-sans text-white drop-shadow\">88%</span>\r\n          <div className=\"relative flex items-center\">\r\n            <div className=\"w-8 h-4.5 rounded-[5px] border-2 border-white/70 p-[2px] flex items-center bg-black/40 shadow-sm\">\r\n              <div \r\n                className=\"h-full bg-[#34c759] rounded-[2px] transition-all\"\r\n                style={{ width: '88%' }}\r\n              />\r\n            </div>\r\n            <div className=\"w-[2.5px] h-2 bg-white/70 rounded-r-[1px] ml-[1.5px]\" />\r\n          </div>\r\n        </div>\r\n\r\n        {/* Discreet Launcher Settings Button (не бросается в глаза, но удобно нажимать) */}\r\n        {onOpenLauncherSettings && (\r\n          <button\r\n            onClick={() => {\r\n              sound.playClick();\r\n              onOpenLauncherSettings();\r\n            }}\r\n            className=\"p-2 rounded-full text-white/40 hover:text-white hover:bg-white/15 transition-all active:scale-95 ml-1\"\r\n            title=\"Настройки DeskNX лаунчера\"\r\n          >\r\n            <Sliders className=\"w-5 h-5\" />\r\n          </button>\r\n        )}\r\n      </div>\r\n    </div>\r\n  );\r\n};\r\n"}