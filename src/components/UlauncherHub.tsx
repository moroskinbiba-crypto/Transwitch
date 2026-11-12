import React, { useState, useEffect, useRef } from 'react';
import { 
  Search, 
  Gamepad2, 
  Terminal, 
  Layers, 
  Clock, 
  Plus, 
  Play, 
  X, 
  CheckCircle2, 
  Command, 
  Zap, 
  Sliders, 
  Power, 
  RotateCw,
  Sparkles,
  ArrowRight,
  Settings
} from 'lucide-react';
import { Candidate, Workspace } from '../types/desknx';
import { sound } from '../data/mockCatalog';
import { findFirstFree } from '../utils/desknxLayout';

import { notify } from '../ui/toast';
interface UlauncherHubProps {
  isOpen: boolean;
  onClose: () => void;
  catalog: Candidate[];
  activeWorkspace: Workspace;
  workspaces: Workspace[];
  activeWorkspaceIndex: number;
  onLaunchCandidate: (candidate: Candidate) => void;
  onAddTileToWorkspace: (candidate: Candidate, w: number, h: number) => void;
  onSelectWorkspace: (index: number) => void;
  onOpenSettings: () => void;
  onOpenDeskCustomizer: () => void;
  onOpenSoftwareOptions: (candidate: Candidate) => void;
}

export const UlauncherHub: React.FC<UlauncherHubProps> = ({
  isOpen,
  onClose,
  catalog,
  activeWorkspace,
  workspaces,
  activeWorkspaceIndex,
  onLaunchCandidate,
  onAddTileToWorkspace,
  onSelectWorkspace,
  onOpenSettings,
  onOpenDeskCustomizer,
  onOpenSoftwareOptions,
}) => {
  const [query, setQuery] = useState('');
  const [activeTab, setActiveTab] = useState<'all' | 'games' | 'homebrew' | 'widgets' | 'commands'>('all');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [pinSize, setPinSize] = useState<{ w: number; h: number }>({ w: 1, h: 1 });
  const [pinnedNotice, setPinnedNotice] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (isOpen) {
      setQuery('');
      setSelectedIndex(0);
      sound.playSelect();
      setTimeout(() => inputRef.current?.focus(), 60);
    }
  }, [isOpen]);

  if (!isOpen) return null;

  const q = query.trim().toLowerCase();

  // Filter items based on active tab & search query
  let items = catalog.filter((item) => {
    if (activeTab === 'games' && item.kind !== 'Game') return false;
    if (activeTab === 'homebrew' && item.kind !== 'Nro') return false;
    if (activeTab === 'widgets' && !['Clock', 'Battery', 'Storage', 'System', 'Recent', 'Calendar'].includes(item.kind)) return false;
    if (activeTab === 'commands') return false;

    if (!q) return true;
    return (
      item.title.toLowerCase().includes(q) ||
      (item.author && item.author.toLowerCase().includes(q))
    );
  });

  // Commands list (when tab is commands or query starts with > or /)
  const isCommandMode = activeTab === 'commands' || q.startsWith('>') || q.startsWith('/');
  const rawCmd = q.startsWith('>') || q.startsWith('/') ? q.substring(1).trim() : q;

  const commandsList = [
    {
      id: 'cmd-desk-custom',
      title: 'Настройка текущего стола',
      subtitle: `Изменить фон, размытие, стиль карточек для "${activeWorkspace.name}"`,
      icon: Sparkles,
      color: 'text-amber-400',
      action: () => {
        onClose();
        onOpenDeskCustomizer();
      },
    },
    {
      id: 'cmd-settings',
      title: 'Настройки лаунчера',
      subtitle: 'Шрифты, темы оформления, системная интеграция HOME меню',
      icon: Sliders,
      color: 'text-cyan-400',
      action: () => {
        onClose();
        onOpenSettings();
      },
    },
    {
      id: 'cmd-sleep',
      title: 'Режим сна (Sleep Mode)',
      subtitle: 'Перевести консоль Nintendo Switch в энергосберегающий режим',
      icon: Power,
      color: 'text-purple-400',
      action: () => {
        sound.playBack();
        onClose();
        notify('Переход в режим сна...');
      },
    },
    {
      id: 'cmd-reboot',
      title: 'Перезагрузка консоли',
      subtitle: 'Мягкий перезапуск системы в Atmosphère bootloader',
      icon: RotateCw,
      color: 'text-rose-400',
      action: () => {
        sound.playLaunch();
        onClose();
        notify('Перезагрузка Nintendo Switch...');
      },
    },
    ...workspaces.map((w, idx) => ({
      id: `cmd-desk-jump-${idx}`,
      title: `Перейти на рабочий стол "${w.name}"`,
      subtitle: `Рабочий стол ${idx + 1} из ${workspaces.length} (${w.tiles.length} плиток)`,
      icon: Layers,
      color: 'text-emerald-400',
      action: () => {
        onClose();
        onSelectWorkspace(idx);
      },
    })),
  ].filter((c) => {
    if (!isCommandMode && !q) return false;
    if (!rawCmd) return true;
    return c.title.toLowerCase().includes(rawCmd) || c.subtitle.toLowerCase().includes(rawCmd);
  });

  const totalResultsCount = isCommandMode ? commandsList.length : items.length;
  const safeIndex = Math.min(selectedIndex, Math.max(0, totalResultsCount - 1));

  const handleLaunchCurrent = (candidate: Candidate) => {
    sound.playLaunch();
    onLaunchCandidate(candidate);
    onClose();
  };

  const handlePinCurrent = (candidate: Candidate, w: number, h: number) => {
    const free = findFirstFree(activeWorkspace, w, h);
    if (!free) {
      sound.playBack();
      notify(`На столе "${activeWorkspace.name}" нет свободного места под размер ${w}×${h}.`);
      return;
    }
    sound.playSelect();
    onAddTileToWorkspace(candidate, w, h);
    setPinnedNotice(`Плитка "${candidate.title}" добавлена на стол "${activeWorkspace.name}"`);
    setTimeout(() => setPinnedNotice(null), 2500);
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      sound.playNav();
      setSelectedIndex((i) => (i < totalResultsCount - 1 ? i + 1 : 0));
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      sound.playNav();
      setSelectedIndex((i) => (i > 0 ? i - 1 : Math.max(0, totalResultsCount - 1)));
    } else if (e.key === 'Enter') {
      e.preventDefault();
      if (isCommandMode) {
        if (commandsList[safeIndex]) commandsList[safeIndex].action();
      } else {
        if (items[safeIndex]) handleLaunchCurrent(items[safeIndex]);
      }
    } else if (e.key === '+' || e.key === '=' || e.key === '-') {
      if (!isCommandMode && items[safeIndex]) {
        e.preventDefault();
        sound.playSelect();
        onOpenSoftwareOptions(items[safeIndex]);
      }
    } else if (e.key === 'Escape') {
      e.preventDefault();
      sound.playBack();
      onClose();
    }
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-start justify-center pt-16 md:pt-24 px-4 bg-black/75 backdrop-blur-xl animate-in fade-in duration-150 select-none text-zinc-100"
      onClick={onClose}
    >
      {/* Floating uLauncher Master Container */}
      <div
        className="w-full max-w-4xl bg-[#0e0e13]/95 backdrop-blur-3xl border border-white/15 rounded-3xl shadow-[0_30px_90px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col transform transition-all animate-in zoom-in-95 duration-150 max-h-[82vh]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Search Header Bar (iOS Spotlight floating bar) */}
        <div className="relative flex items-center px-6 py-4.5 border-b border-white/10 bg-white/[0.04]">
          <Search className="w-6 h-6 text-[#007aff] mr-4 shrink-0" />
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e) => {
              setQuery(e.target.value);
              setSelectedIndex(0);
            }}
            onKeyDown={handleKeyDown}
            placeholder="Поиск по названию или команда (например: Zelda, Сон, >стол)..."
            className="w-full bg-transparent text-lg md:text-xl font-bold text-white placeholder-zinc-500 focus:outline-none"
          />

          <div className="flex items-center gap-2 ml-3">
            <span className="text-[11px] font-mono bg-white/10 text-zinc-300 px-2 py-0.5 rounded-lg border border-white/10">
              ESC
            </span>
            <button
              onClick={() => {
                sound.playBack();
                onClose();
              }}
              className="p-1 rounded-xl hover:bg-white/10 text-zinc-400 hover:text-white"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Unified Category Filter Tabs (iOS Segmented Control) */}
        <div className="px-6 py-2.5 bg-black/40 border-b border-white/10 flex items-center justify-between gap-2 overflow-x-auto">
          <div className="flex items-center gap-1.5">
            {[
              { id: 'all', label: `Все (${catalog.length})` },
              { id: 'games', label: 'Игры' },
              { id: 'homebrew', label: 'Homebrew' },
              { id: 'widgets', label: 'Виджеты' },
              { id: 'commands', label: 'Команды' },
            ].map((tab) => (
              <button
                key={tab.id}
                onClick={() => {
                  sound.playClick();
                  setActiveTab(tab.id as any);
                  setSelectedIndex(0);
                }}
                className={`px-3.5 py-1.5 rounded-full text-xs font-bold transition-all ${
                  activeTab === tab.id && !q.startsWith('>')
                    ? 'bg-[#007aff] text-white shadow-md'
                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
                }`}
              >
                {tab.label}
              </button>
            ))}
          </div>

          {/* Quick pin destination info */}
          <div className="text-xs text-zinc-400 truncate hidden sm:block">
            Страница: <span className="text-white font-bold">{activeWorkspace.name}</span>
          </div>
        </div>

        {/* Feedback banner */}
        {pinnedNotice && (
          <div className="px-6 py-2.5 bg-emerald-950/80 border-b border-emerald-500/40 text-emerald-300 text-xs font-bold flex items-center gap-2 animate-in fade-in">
            <CheckCircle2 className="w-4 h-4 shrink-0" />
            <span>{pinnedNotice}</span>
          </div>
        )}

        {/* Master Content: Results List + Quick Actions */}
        <div className="flex-1 overflow-y-auto divide-y divide-white/5 scrollbar-thin max-h-[52vh]">
          {isCommandMode ? (
            /* COMMANDS MODE */
            commandsList.length === 0 ? (
              <div className="p-10 text-center text-zinc-500 text-sm font-medium">
                Команды по запросу "{rawCmd}" не найдены.
              </div>
            ) : (
              commandsList.map((cmd, idx) => {
                const isSelected = idx === safeIndex;
                const Icon = cmd.icon;
                return (
                  <div
                    key={cmd.id}
                    onClick={() => cmd.action()}
                    onMouseEnter={() => setSelectedIndex(idx)}
                    className={`flex items-center justify-between px-6 py-3.5 cursor-pointer transition-all ${
                      isSelected
                        ? 'bg-gradient-to-r from-blue-600/25 via-blue-500/15 to-transparent border-l-4 border-[#007aff] pl-5'
                        : 'hover:bg-white/[0.04]'
                    }`}
                  >
                    <div className="flex items-center gap-3.5 min-w-0">
                      <div className="w-10 h-10 rounded-2xl bg-zinc-800/80 border border-white/10 flex items-center justify-center shrink-0">
                        <Icon className={`w-5 h-5 ${cmd.color}`} />
                      </div>
                      <div className="min-w-0">
                        <div className="text-sm font-bold text-white truncate">
                          {cmd.title}
                        </div>
                        <div className="text-xs text-zinc-400 truncate mt-0.5">
                          {cmd.subtitle}
                        </div>
                      </div>
                    </div>

                    <div className="flex items-center gap-2 text-blue-400 text-xs font-bold shrink-0 ml-3">
                      <span>Выполнить</span>
                      <ArrowRight className="w-4 h-4" />
                    </div>
                  </div>
                );
              })
            )
          ) : (
            /* CATALOG & APPS LIST */
            items.length === 0 ? (
              <div className="p-10 text-center text-zinc-500 text-sm font-medium">
                Приложения по запросу "{query}" не найдены.
              </div>
            ) : (
              items.map((item, idx) => {
                const isSelected = idx === safeIndex;
                const hours = Math.floor(item.playtimeSeconds / 3600);
                const playtimeLabel = hours > 0 ? `${hours} ч. сыграно` : 'Только начато';

                return (
                  <div
                    key={item.id}
                    onClick={() => handleLaunchCurrent(item)}
                    onMouseEnter={() => setSelectedIndex(idx)}
                    className={`flex items-center justify-between px-6 py-3.5 cursor-pointer transition-all group ${
                      isSelected
                        ? 'bg-gradient-to-r from-blue-600/25 via-blue-500/15 to-transparent border-l-4 border-[#007aff] pl-5'
                        : 'hover:bg-white/[0.04]'
                    }`}
                  >
                    {/* Item info */}
                    <div className="flex items-center gap-4 min-w-0 flex-1 pr-4">
                      {item.icon ? (
                        <div className="w-12 h-12 rounded-[18px] overflow-hidden bg-black/60 border border-white/15 shrink-0 shadow-md">
                          <img src={item.icon} alt={item.title} className="w-full h-full object-cover" />
                        </div>
                      ) : (
                        <div className="w-12 h-12 rounded-[18px] bg-blue-950/60 border border-blue-500/30 flex items-center justify-center shrink-0 text-blue-400">
                          {item.kind === 'Game' && <Gamepad2 className="w-6 h-6" />}
                          {item.kind === 'Nro' && <Terminal className="w-6 h-6" />}
                          {['Clock', 'Battery', 'Storage', 'System', 'Recent', 'Calendar'].includes(item.kind) && (
                            <Clock className="w-6 h-6" />
                          )}
                        </div>
                      )}

                      <div className="min-w-0 flex-1">
                        <div className="text-sm md:text-base font-bold text-white truncate flex items-center gap-2">
                          <span>{item.title}</span>
                          <span
                            className={`text-[9px] font-mono px-2 py-0.2 rounded-full font-bold uppercase ${
                              item.kind === 'Game'
                                ? 'bg-red-500/20 text-red-300 border border-red-500/30'
                                : item.kind === 'Nro'
                                ? 'bg-blue-500/20 text-blue-300 border border-blue-500/30'
                                : 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30'
                            }`}
                          >
                            {item.kind === 'Game' ? 'Игра' : item.kind === 'Nro' ? 'Приложение' : 'Виджет'}
                          </span>
                        </div>
                        <div className="text-xs text-zinc-400 truncate mt-0.5 flex items-center gap-2">
                          <span className="text-white/70 font-medium">{playtimeLabel}</span>
                          {item.author && <span className="text-zinc-500">· {item.author}</span>}
                        </div>
                      </div>
                    </div>

                    {/* Quick Action buttons (Options gear, Pin 1x1, Run) */}
                    <div className="flex items-center gap-2 shrink-0" onClick={(e) => e.stopPropagation()}>
                      {/* Real Switch + / - Software Options button ("Параметры программы") -> Gear Icon */}
                      <button
                        onClick={() => {
                          sound.playSelect();
                          onOpenSoftwareOptions(item);
                        }}
                        className="p-2 rounded-full bg-white/10 hover:bg-white/20 text-zinc-200 hover:text-white border border-white/15 transition-all active:scale-95 shadow-sm"
                        title="Параметры программы (+ / -)"
                      >
                        <Settings className="w-4 h-4" />
                      </button>

                      {/* Single + button that defaults to adding 1×1 to the current workspace */}
                      <button
                        onClick={() => handlePinCurrent(item, 1, 1)}
                        className="flex items-center gap-1.5 px-3 py-1.5 rounded-full bg-white/10 hover:bg-white/20 text-white font-bold text-xs border border-white/15 transition-all active:scale-95 shadow-sm"
                        title={`Добавить на стол "${activeWorkspace.name}" (1×1)`}
                      >
                        <Plus className="w-4 h-4 stroke-[2.5]" />
                        <span className="hidden sm:inline">1×1</span>
                      </button>

                      {/* Main Launch Button */}
                      <button
                        onClick={() => handleLaunchCurrent(item)}
                        className="flex items-center gap-1.5 px-4 py-2 rounded-full bg-[#007aff] hover:bg-[#0062cc] text-white font-bold text-xs shadow-md transition-all active:scale-95"
                      >
                        <Play className="w-3.5 h-3.5 fill-current" />
                        <span>Старт</span>
                      </button>
                    </div>
                  </div>
                );
              })
            )
          )}
        </div>

        {/* Footer info bar */}
        <div className="px-6 py-3 bg-black/60 border-t border-white/10 flex items-center justify-between text-xs text-zinc-400">
          <div className="flex items-center gap-4">
            <span><strong className="text-white">↑↓</strong> Навигация</span>
            <span><strong className="text-white">↵</strong> Запуск</span>
            <span className="flex items-center gap-1"><Settings className="w-3.5 h-3.5 inline text-zinc-300" /><strong className="text-white">/ ＋</strong> Параметры</span>
            <span><strong className="text-white">+ 1×1</strong> На экран</span>
          </div>
          <div className="text-zinc-400 font-medium">
            Spotlight Switch · Мгновенный поиск
          </div>
        </div>
      </div>
    </div>
  );
};

{"content":"﻿import React, { useState, useEffect, useRef } from 'react';\r\nimport { \r\n  Search, \r\n  Gamepad2, \r\n  Terminal, \r\n  Layers, \r\n  Clock, \r\n  Plus, \r\n  Play, \r\n  X, \r\n  CheckCircle2, \r\n  Command, \r\n  Zap, \r\n  Sliders, \r\n  Power, \r\n  RotateCw,\r\n  Sparkles,\r\n  ArrowRight,\r\n  Settings\r\n} from 'lucide-react';\r\nimport { Candidate, Workspace } from '../types/desknx';\r\nimport { sound } from '../data/mockCatalog';\r\nimport { findFirstFree } from '../utils/desknxLayout';\r\n\r\nimport { notify } from '../ui/toast';\r\ninterface UlauncherHubProps {\r\n  isOpen: boolean;\r\n  onClose: () => void;\r\n  catalog: Candidate[];\r\n  activeWorkspace: Workspace;\r\n  workspaces: Workspace[];\r\n  activeWorkspaceIndex: number;\r\n  onLaunchCandidate: (candidate: Candidate) => void;\r\n  onAddTileToWorkspace: (candidate: Candidate, w: number, h: number) => void;\r\n  onSelectWorkspace: (index: number) => void;\r\n  onOpenSettings: () => void;\r\n  onOpenDeskCustomizer: () => void;\r\n  onOpenSoftwareOptions: (candidate: Candidate) => void;\r\n}\r\n\r\nexport const UlauncherHub: React.FC<UlauncherHubProps> = ({\r\n  isOpen,\r\n  onClose,\r\n  catalog,\r\n  activeWorkspace,\r\n  workspaces,\r\n  activeWorkspaceIndex,\r\n  onLaunchCandidate,\r\n  onAddTileToWorkspace,\r\n  onSelectWorkspace,\r\n  onOpenSettings,\r\n  onOpenDeskCustomizer,\r\n  onOpenSoftwareOptions,\r\n}) => {\r\n  const [query, setQuery] = useState('');\r\n  const [activeTab, setActiveTab] = useState<'all' | 'games' | 'homebrew' | 'widgets' | 'commands'>('all');\r\n  const [selectedIndex, setSelectedIndex] = useState(0);\r\n  const [pinSize, setPinSize] = useState<{ w: number; h: number }>({ w: 1, h: 1 });\r\n  const [pinnedNotice, setPinnedNotice] = useState<string | null>(null);\r\n  const inputRef = useRef<HTMLInputElement>(null);\r\n\r\n  useEffect(() => {\r\n    if (isOpen) {\r\n      setQuery('');\r\n      setSelectedIndex(0);\r\n      sound.playSelect();\r\n      setTimeout(() => inputRef.current?.focus(), 60);\r\n    }\r\n  }, [isOpen]);\r\n\r\n  if (!isOpen) return null;\r\n\r\n  const q = query.trim().toLowerCase();\r\n\r\n  // Filter items based on active tab & search query\r\n  let items = catalog.filter((item) => {\r\n    if (activeTab === 'games' && item.kind !== 'Game') return false;\r\n    if (activeTab === 'homebrew' && item.kind !== 'Nro') return false;\r\n    if (activeTab === 'widgets' && !['Clock', 'Battery', 'Storage', 'System', 'Recent', 'Calendar'].includes(item.kind)) return false;\r\n    if (activeTab === 'commands') return false;\r\n\r\n    if (!q) return true;\r\n    return (\r\n      item.title.toLowerCase().includes(q) ||\r\n      (item.author && item.author.toLowerCase().includes(q))\r\n    );\r\n  });\r\n\r\n  // Commands list (when tab is commands or query starts with > or /)\r\n  const isCommandMode = activeTab === 'commands' || q.startsWith('>') || q.startsWith('/');\r\n  const rawCmd = q.startsWith('>') || q.startsWith('/') ? q.substring(1).trim() : q;\r\n\r\n  const commandsList = [\r\n    {\r\n      id: 'cmd-desk-custom',\r\n      title: 'Настройка текущего стола',\r\n      subtitle: `Изменить фон, размытие, стиль карточек для \"${activeWorkspace.name}\"`,\r\n      icon: Sparkles,\r\n      color: 'text-amber-400',\r\n      action: () => {\r\n        onClose();\r\n        onOpenDeskCustomizer();\r\n      },\r\n    },\r\n    {\r\n      id: 'cmd-settings',\r\n      title: 'Настройки лаунчера',\r\n      subtitle: 'Шрифты, темы оформления, системная интеграция HOME меню',\r\n      icon: Sliders,\r\n      color: 'text-cyan-400',\r\n      action: () => {\r\n        onClose();\r\n        onOpenSettings();\r\n      },\r\n    },\r\n    {\r\n      id: 'cmd-sleep',\r\n      title: 'Режим сна (Sleep Mode)',\r\n      subtitle: 'Перевести консоль Nintendo Switch в энергосберегающий режим',\r\n      icon: Power,\r\n      color: 'text-purple-400',\r\n      action: () => {\r\n        sound.playBack();\r\n        onClose();\r\n        notify('Переход в режим сна...');\r\n      },\r\n    },\r\n    {\r\n      id: 'cmd-reboot',\r\n      title: 'Перезагрузка консоли',\r\n      subtitle: 'Мягкий перезапуск системы в Atmosphère bootloader',\r\n      icon: RotateCw,\r\n      color: 'text-rose-400',\r\n      action: () => {\r\n        sound.playLaunch();\r\n        onClose();\r\n        notify('Перезагрузка Nintendo Switch...');\r\n      },\r\n    },\r\n    ...workspaces.map((w, idx) => ({\r\n      id: `cmd-desk-jump-${idx}`,\r\n      title: `Перейти на рабочий стол \"${w.name}\"`,\r\n      subtitle: `Рабочий стол ${idx + 1} из ${workspaces.length} (${w.tiles.length} плиток)`,\r\n      icon: Layers,\r\n      color: 'text-emerald-400',\r\n      action: () => {\r\n        onClose();\r\n        onSelectWorkspace(idx);\r\n      },\r\n    })),\r\n  ].filter((c) => {\r\n    if (!isCommandMode && !q) return false;\r\n    if (!rawCmd) return true;\r\n    return c.title.toLowerCase().includes(rawCmd) || c.subtitle.toLowerCase().includes(rawCmd);\r\n  });\r\n\r\n  const totalResultsCount = isCommandMode ? commandsList.length : items.length;\r\n  const safeIndex = Math.min(selectedIndex, Math.max(0, totalResultsCount - 1));\r\n\r\n  const handleLaunchCurrent = (candidate: Candidate) => {\r\n    sound.playLaunch();\r\n    onLaunchCandidate(candidate);\r\n    onClose();\r\n  };\r\n\r\n  const handlePinCurrent = (candidate: Candidate, w: number, h: number) => {\r\n    const free = findFirstFree(activeWorkspace, w, h);\r\n    if (!free) {\r\n      sound.playBack();\r\n      notify(`На столе \"${activeWorkspace.name}\" нет свободного места под размер ${w}×${h}.`);\r\n      return;\r\n    }\r\n    sound.playSelect();\r\n    onAddTileToWorkspace(candidate, w, h);\r\n    setPinnedNotice(`Плитка \"${candidate.title}\" добавлена на стол \"${activeWorkspace.name}\"`);\r\n    setTimeout(() => setPinnedNotice(null), 2500);\r\n  };\r\n\r\n  const handleKeyDown = (e: React.KeyboardEvent) => {\r\n    if (e.key === 'ArrowDown') {\r\n      e.preventDefault();\r\n      sound.playNav();\r\n      setSelectedIndex((i) => (i < totalResultsCount - 1 ? i + 1 : 0));\r\n    } else if (e.key === 'ArrowUp') {\r\n      e.preventDefault();\r\n      sound.playNav();\r\n      setSelectedIndex((i) => (i > 0 ? i - 1 : Math.max(0, totalResultsCount - 1)));\r\n    } else if (e.key === 'Enter') {\r\n      e.preventDefault();\r\n      if (isCommandMode) {\r\n        if (commandsList[safeIndex]) commandsList[safeIndex].action();\r\n      } else {\r\n        if (items[safeIndex]) handleLaunchCurrent(items[safeIndex]);\r\n      }\r\n    } else if (e.key === '+' || e.key === '=' || e.key === '-') {\r\n      if (!isCommandMode && items[safeIndex]) {\r\n        e.preventDefault();\r\n        sound.playSelect();\r\n        onOpenSoftwareOptions(items[safeIndex]);\r\n      }\r\n    } else if (e.key === 'Escape') {\r\n      e.preventDefault();\r\n      sound.playBack();\r\n      onClose();\r\n    }\r\n  };\r\n\r\n  return (\r\n    <div\r\n      className=\"fixed inset-0 z-50 flex items-start justify-center pt-16 md:pt-24 px-4 bg-black/75 backdrop-blur-xl animate-in fade-in duration-150 select-none text-zinc-100\"\r\n      onClick={onClose}\r\n    >\r\n      {/* Floating uLauncher Master Container */}\r\n      <div\r\n        className=\"w-full max-w-4xl bg-[#0e0e13]/95 backdrop-blur-3xl border border-white/15 rounded-3xl shadow-[0_30px_90px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col transform transition-all animate-in zoom-in-95 duration-150 max-h-[82vh]\"\r\n        onClick={(e) => e.stopPropagation()}\r\n      >\r\n        {/* Search Header Bar (iOS Spotlight floating bar) */}\r\n        <div className=\"relative flex items-center px-6 py-4.5 border-b border-white/10 bg-white/[0.04]\">\r\n          <Search className=\"w-6 h-6 text-[#007aff] mr-4 shrink-0\" />\r\n          <input\r\n            ref={inputRef}\r\n            type=\"text\"\r\n            value={query}\r\n            onChange={(e) => {\r\n              setQuery(e.target.value);\r\n              setSelectedIndex(0);\r\n            }}\r\n            onKeyDown={handleKeyDown}\r\n            placeholder=\"Поиск по названию или команда (например: Zelda, Сон, >стол)...\"\r\n            className=\"w-full bg-transparent text-lg md:text-xl font-bold text-white placeholder-zinc-500 focus:outline-none\"\r\n          />\r\n\r\n          <div className=\"flex items-center gap-2 ml-3\">\r\n            <span className=\"text-[11px] font-mono bg-white/10 text-zinc-300 px-2 py-0.5 rounded-lg border border-white/10\">\r\n              ESC\r\n            </span>\r\n            <button\r\n              onClick={() => {\r\n                sound.playBack();\r\n                onClose();\r\n              }}\r\n              className=\"p-1 rounded-xl hover:bg-white/10 text-zinc-400 hover:text-white\"\r\n            >\r\n              <X className=\"w-5 h-5\" />\r\n            </button>\r\n          </div>\r\n        </div>\r\n\r\n        {/* Unified Category Filter Tabs (iOS Segmented Control) */}\r\n        <div className=\"px-6 py-2.5 bg-black/40 border-b border-white/10 flex items-center justify-between gap-2 overflow-x-auto\">\r\n          <div className=\"flex items-center gap-1.5\">\r\n            {[\r\n              { id: 'all', label: `Все (${catalog.length})` },\r\n              { id: 'games', label: 'Игры' },\r\n              { id: 'homebrew', label: 'Homebrew' },\r\n              { id: 'widgets', label: 'Виджеты' },\r\n              { id: 'commands', label: 'Команды' },\r\n            ].map((tab) => (\r\n              <button\r\n                key={tab.id}\r\n                onClick={() => {\r\n                  sound.playClick();\r\n                  setActiveTab(tab.id as any);\r\n                  setSelectedIndex(0);\r\n                }}\r\n                className={`px-3.5 py-1.5 rounded-full text-xs font-bold transition-all ${\r\n                  activeTab === tab.id && !q.startsWith('>')\r\n                    ? 'bg-[#007aff] text-white shadow-md'\r\n                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'\r\n                }`}\r\n              >\r\n                {tab.label}\r\n              </button>\r\n            ))}\r\n          </div>\r\n\r\n          {/* Quick pin destination info */}\r\n          <div className=\"text-xs text-zinc-400 truncate hidden sm:block\">\r\n            Страница: <span className=\"text-white font-bold\">{activeWorkspace.name}</span>\r\n          </div>\r\n        </div>\r\n\r\n        {/* Feedback banner */}\r\n        {pinnedNotice && (\r\n          <div className=\"px-6 py-2.5 bg-emerald-950/80 border-b border-emerald-500/40 text-emerald-300 text-xs font-bold flex items-center gap-2 animate-in fade-in\">\r\n            <CheckCircle2 className=\"w-4 h-4 shrink-0\" />\r\n            <span>{pinnedNotice}</span>\r\n          </div>\r\n        )}\r\n\r\n        {/* Master Content: Results List + Quick Actions */}\r\n        <div className=\"flex-1 overflow-y-auto divide-y divide-white/5 scrollbar-thin max-h-[52vh]\">\r\n          {isCommandMode ? (\r\n            /* COMMANDS MODE */\r\n            commandsList.length === 0 ? (\r\n              <div className=\"p-10 text-center text-zinc-500 text-sm font-medium\">\r\n                Команды по запросу \"{rawCmd}\" не найдены.\r\n              </div>\r\n            ) : (\r\n              commandsList.map((cmd, idx) => {\r\n                const isSelected = idx === safeIndex;\r\n                const Icon = cmd.icon;\r\n                return (\r\n                  <div\r\n                    key={cmd.id}\r\n                    onClick={() => cmd.action()}\r\n                    onMouseEnter={() => setSelectedIndex(idx)}\r\n                    className={`flex items-center justify-between px-6 py-3.5 cursor-pointer transition-all ${\r\n                      isSelected\r\n                        ? 'bg-gradient-to-r from-blue-600/25 via-blue-500/15 to-transparent border-l-4 border-[#007aff] pl-5'\r\n                        : 'hover:bg-white/[0.04]'\r\n                    }`}\r\n                  >\r\n                    <div className=\"flex items-center gap-3.5 min-w-0\">\r\n                      <div className=\"w-10 h-10 rounded-2xl bg-zinc-800/80 border border-white/10 flex items-center justify-center shrink-0\">\r\n                        <Icon className={`w-5 h-5 ${cmd.color}`} />\r\n                      </div>\r\n                      <div className=\"min-w-0\">\r\n                        <div className=\"text-sm font-bold text-white truncate\">\r\n                          {cmd.title}\r\n                        </div>\r\n                        <div className=\"text-xs text-zinc-400 truncate mt-0.5\">\r\n                          {cmd.subtitle}\r\n                        </div>\r\n                      </div>\r\n                    </div>\r\n\r\n                    <div className=\"flex items-center gap-2 text-blue-400 text-xs font-bold shrink-0 ml-3\">\r\n                      <span>Выполнить</span>\r\n                      <ArrowRight className=\"w-4 h-4\" />\r\n                    </div>\r\n                  </div>\r\n                );\r\n              })\r\n            )\r\n          ) : (\r\n            /* CATALOG & APPS LIST */\r\n            items.length === 0 ? (\r\n              <div className=\"p-10 text-center text-zinc-500 text-sm font-medium\">\r\n                Приложения по запросу \"{query}\" не найдены.\r\n              </div>\r\n            ) : (\r\n              items.map((item, idx) => {\r\n                const isSelected = idx === safeIndex;\r\n                const hours = Math.floor(item.playtimeSeconds / 3600);\r\n                const playtimeLabel = hours > 0 ? `${hours} ч. сыграно` : 'Только начато';\r\n\r\n                return (\r\n                  <div\r\n                    key={item.id}\r\n                    onClick={() => handleLaunchCurrent(item)}\r\n                    onMouseEnter={() => setSelectedIndex(idx)}\r\n                    className={`flex items-center justify-between px-6 py-3.5 cursor-pointer transition-all group ${\r\n                      isSelected\r\n                        ? 'bg-gradient-to-r from-blue-600/25 via-blue-500/15 to-transparent border-l-4 border-[#007aff] pl-5'\r\n                        : 'hover:bg-white/[0.04]'\r\n                    }`}\r\n                  >\r\n                    {/* Item info */}\r\n                    <div className=\"flex items-center gap-4 min-w-0 flex-1 pr-4\">\r\n                      {item.icon ? (\r\n                        <div className=\"w-12 h-12 rounded-[18px] overflow-hidden bg-black/60 border border-white/15 shrink-0 shadow-md\">\r\n                          <img src={item.icon} alt={item.title} className=\"w-full h-full object-cover\" />\r\n                        </div>\r\n                      ) : (\r\n                        <div className=\"w-12 h-12 rounded-[18px] bg-blue-950/60 border border-blue-500/30 flex items-center justify-center shrink-0 text-blue-400\">\r\n                          {item.kind === 'Game' && <Gamepad2 className=\"w-6 h-6\" />}\r\n                          {item.kind === 'Nro' && <Terminal className=\"w-6 h-6\" />}\r\n                          {['Clock', 'Battery', 'Storage', 'System', 'Recent', 'Calendar'].includes(item.kind) && (\r\n                            <Clock className=\"w-6 h-6\" />\r\n                          )}\r\n                        </div>\r\n                      )}\r\n\r\n                      <div className=\"min-w-0 flex-1\">\r\n                        <div className=\"text-sm md:text-base font-bold text-white truncate flex items-center gap-2\">\r\n                          <span>{item.title}</span>\r\n                          <span\r\n                            className={`text-[9px] font-mono px-2 py-0.2 rounded-full font-bold uppercase ${\r\n                              item.kind === 'Game'\r\n                                ? 'bg-red-500/20 text-red-300 border border-red-500/30'\r\n                                : item.kind === 'Nro'\r\n                                ? 'bg-blue-500/20 text-blue-300 border border-blue-500/30'\r\n                                : 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30'\r\n                            }`}\r\n                          >\r\n                            {item.kind === 'Game' ? 'Игра' : item.kind === 'Nro' ? 'Приложение' : 'Виджет'}\r\n                          </span>\r\n                        </div>\r\n                        <div className=\"text-xs text-zinc-400 truncate mt-0.5 flex items-center gap-2\">\r\n                          <span className=\"text-white/70 font-medium\">{playtimeLabel}</span>\r\n                          {item.author && <span className=\"text-zinc-500\">· {item.author}</span>}\r\n                        </div>\r\n                      </div>\r\n                    </div>\r\n\r\n                    {/* Quick Action buttons (Options gear, Pin 1x1, Run) */}\r\n                    <div className=\"flex items-center gap-2 shrink-0\" onClick={(e) => e.stopPropagation()}>\r\n                      {/* Real Switch + / - Software Options button (\"Параметры программы\") -> Gear Icon */}\r\n                      <button\r\n                        onClick={() => {\r\n                          sound.playSelect();\r\n                          onOpenSoftwareOptions(item);\r\n                        }}\r\n                        className=\"p-2 rounded-full bg-white/10 hover:bg-white/20 text-zinc-200 hover:text-white border border-white/15 transition-all active:scale-95 shadow-sm\"\r\n                        title=\"Параметры программы (+ / -)\"\r\n                      >\r\n                        <Settings className=\"w-4 h-4\" />\r\n                      </button>\r\n\r\n                      {/* Single + button that defaults to adding 1×1 to the current workspace */}\r\n                      <button\r\n                        onClick={() => handlePinCurrent(item, 1, 1)}\r\n                        className=\"flex items-center gap-1.5 px-3 py-1.5 rounded-full bg-white/10 hover:bg-white/20 text-white font-bold text-xs border border-white/15 transition-all active:scale-95 shadow-sm\"\r\n                        title={`Добавить на стол \"${activeWorkspace.name}\" (1×1)`}\r\n                      >\r\n                        <Plus className=\"w-4 h-4 stroke-[2.5]\" />\r\n                        <span className=\"hidden sm:inline\">1×1</span>\r\n                      </button>\r\n\r\n                      {/* Main Launch Button */}\r\n                      <button\r\n                        onClick={() => handleLaunchCurrent(item)}\r\n                        className=\"flex items-center gap-1.5 px-4 py-2 rounded-full bg-[#007aff] hover:bg-[#0062cc] text-white font-bold text-xs shadow-md transition-all active:scale-95\"\r\n                      >\r\n                        <Play className=\"w-3.5 h-3.5 fill-current\" />\r\n                        <span>Старт</span>\r\n                      </button>\r\n                    </div>\r\n                  </div>\r\n                );\r\n              })\r\n            )\r\n          )}\r\n        </div>\r\n\r\n        {/* Footer info bar */}\r\n        <div className=\"px-6 py-3 bg-black/60 border-t border-white/10 flex items-center justify-between text-xs text-zinc-400\">\r\n          <div className=\"flex items-center gap-4\">\r\n            <span><strong className=\"text-white\">↑↓</strong> Навигация</span>\r\n            <span><strong className=\"text-white\">↵</strong> Запуск</span>\r\n            <span className=\"flex items-center gap-1\"><Settings className=\"w-3.5 h-3.5 inline text-zinc-300\" /><strong className=\"text-white\">/ ＋</strong> Параметры</span>\r\n            <span><strong className=\"text-white\">+ 1×1</strong> На экран</span>\r\n          </div>\r\n          <div className=\"text-zinc-400 font-medium\">\r\n            Spotlight Switch · Мгновенный поиск\r\n          </div>\r\n        </div>\r\n      </div>\r\n    </div>\r\n  );\r\n};\r\n"}