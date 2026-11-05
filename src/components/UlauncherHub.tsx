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
        alert('Переход в режим сна...');
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
        alert('Перезагрузка Nintendo Switch...');
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
      alert(`На столе "${activeWorkspace.name}" нет свободного места под размер ${w}×${h}.`);
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
