import React, { useState } from 'react';
import { Workspace, Tile } from '../types/desknx';
import { PALETTE, COLUMNS, ROWS, findFirstFree, isSupportedTileSize } from '../utils/desknxLayout';
import { sound } from '../data/mockCatalog';
import { 
  X, 
  Palette, 
  Sparkles, 
  Image as ImageIcon, 
  Grid, 
  Copy, 
  ArrowLeft, 
  ArrowRight, 
  Check, 
  Trash2, 
  Maximize2,
  Sliders,
  Layers
} from 'lucide-react';

interface UnifiedDeskCustomizerProps {
  isOpen: boolean;
  onClose: () => void;
  workspace: Workspace;
  workspaceIndex: number;
  totalWorkspaces: number;
  onUpdateWorkspace: (updated: Workspace) => void;
  onDuplicateWorkspace: (workspace: Workspace) => void;
  onReorderWorkspace: (fromIndex: number, toIndex: number) => void;
}

export const UnifiedDeskCustomizer: React.FC<UnifiedDeskCustomizerProps> = ({
  isOpen,
  onClose,
  workspace,
  workspaceIndex,
  totalWorkspaces,
  onUpdateWorkspace,
  onDuplicateWorkspace,
  onReorderWorkspace,
}) => {
  const [activeSection, setActiveSection] = useState<'style' | 'wallpaper' | 'tiles'>('style');
  const [name, setName] = useState(workspace.name);
  const [bgPath, setBgPath] = useState(workspace.backgroundPath || '');
  const [bgBlur, setBgBlur] = useState(workspace.bgBlur ?? 0);
  const [bgBrightness, setBgBrightness] = useState(workspace.bgBrightness ?? 85);
  const [accentColor, setAccentColor] = useState(workspace.accentColor ?? -1);
  const [cardStyle, setCardStyle] = useState<'glass' | 'matte' | 'border' | 'cyber'>(workspace.cardStyle ?? 'glass');
  const [labelOpacity, setLabelOpacity] = useState(workspace.labelOpacity ?? 196);
  const [backgroundFit, setBackgroundFit] = useState(workspace.backgroundFit ?? 0);

  if (!isOpen) return null;

  const handleSave = () => {
    sound.playSelect();
    onUpdateWorkspace({
      ...workspace,
      name: name.trim() || `Desk ${workspaceIndex + 1}`,
      backgroundPath: bgPath.trim(),
      bgBlur,
      bgBrightness,
      accentColor,
      cardStyle,
      labelOpacity,
      backgroundFit,
    });
    onClose();
  };

  const handleCompactTiles = () => {
    sound.playClick();
    const sorted = [...workspace.tiles].sort((a, b) => a.y * COLUMNS + a.x - (b.y * COLUMNS + b.x));
    const compactedDesk: Workspace = { ...workspace, tiles: [] };

    for (const t of sorted) {
      const free = findFirstFree(compactedDesk, t.w, t.h);
      if (free) {
        compactedDesk.tiles.push({ ...t, x: free.x, y: free.y });
      } else {
        compactedDesk.tiles.push(t);
      }
    }
    onUpdateWorkspace(compactedDesk);
  };

  const handleRemoveTileFromDesk = (tileId: string) => {
    sound.playBack();
    const updatedTiles = workspace.tiles.filter((t) => t.id !== tileId);
    onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
  };

  const wallpaperPresets = [
    { name: 'По умолчанию (Тема)', url: '' },
    { name: 'Hyrule Kingdom', url: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=1600&auto=format&fit=crop&q=80' },
    { name: 'Neon Cyberpunk', url: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=1600&auto=format&fit=crop&q=80' },
    { name: 'Deep Cosmos', url: 'https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=1600&auto=format&fit=crop&q=80' },
  ];

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-xl animate-in fade-in select-none text-zinc-100">
      <div className="relative w-full max-w-2xl bg-[#0e0e13]/95 backdrop-blur-3xl border border-white/15 rounded-3xl shadow-[0_25px_80px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col">
        
        {/* Header */}
        <div className="px-6 py-4 bg-zinc-900/80 border-b border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-2xl bg-cyan-500/20 border border-cyan-400/40 flex items-center justify-center text-cyan-400 shadow">
              <Sparkles className="w-5 h-5" />
            </div>
            <div>
              <h3 className="text-base font-bold text-white">Настройка рабочего стола</h3>
              <p className="text-xs text-zinc-400">"{workspace.name}" · Стол {workspaceIndex + 1} из {totalWorkspaces}</p>
            </div>
          </div>
          <button
            onClick={() => {
              sound.playBack();
              onClose();
            }}
            className="p-2 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-zinc-300 hover:text-white transition-all"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Section Tabs */}
        <div className="px-6 py-2.5 bg-black/40 border-b border-white/10 flex items-center gap-2">
          {[
            { id: 'style', label: 'Оформление и стиль', icon: Palette },
            { id: 'wallpaper', label: 'Фон и размытие', icon: ImageIcon },
            { id: 'tiles', label: `Плитки (${workspace.tiles.length})`, icon: Grid },
          ].map((sec) => {
            const Icon = sec.icon;
            return (
              <button
                key={sec.id}
                onClick={() => {
                  sound.playClick();
                  setActiveSection(sec.id as any);
                }}
                className={`flex items-center gap-2 px-3.5 py-1.5 rounded-xl text-xs font-bold transition-all ${
                  activeSection === sec.id
                    ? 'bg-cyan-500 text-black shadow-md'
                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
                }`}
              >
                <Icon className="w-4 h-4" />
                {sec.label}
              </button>
            );
          })}
        </div>

        {/* Form Body */}
        <div className="p-6 space-y-6 overflow-y-auto max-h-[64vh]">
          {/* SECTION 1: STYLE */}
          {activeSection === 'style' && (
            <div className="space-y-5">
              {/* Workspace Name */}
              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-1.5">
                  Название этого стола:
                </label>
                <input
                  type="text"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  className="w-full bg-zinc-900/90 border border-white/15 rounded-2xl px-4 py-2.5 text-sm text-white focus:outline-none focus:border-cyan-400 font-bold"
                />
              </div>

              {/* Card Style */}
              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-2">
                  Визуальный стиль карточек:
                </label>
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-2.5">
                  {[
                    { id: 'glass', label: 'Obsidian Glass' },
                    { id: 'matte', label: 'Matte Titanium' },
                    { id: 'border', label: 'Neon Border' },
                    { id: 'cyber', label: 'Cyberpunk' },
                  ].map((style) => (
                    <button
                      key={style.id}
                      onClick={() => setCardStyle(style.id as any)}
                      className={`py-3 px-3 rounded-2xl text-xs font-bold border transition-all text-center ${
                        cardStyle === style.id
                          ? 'bg-cyan-500 text-black border-cyan-400 shadow-md'
                          : 'bg-zinc-900/60 text-zinc-300 hover:bg-zinc-800 border-white/10'
                      }`}
                    >
                      {style.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* Accent Color */}
              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-2">
                  Цвет акцентного свечения:
                </label>
                <div className="flex flex-wrap gap-2 items-center">
                  <button
                    onClick={() => setAccentColor(-1)}
                    className={`px-3 py-1.5 rounded-xl text-xs font-mono font-bold border transition-all ${
                      accentColor === -1
                        ? 'bg-white text-black border-white'
                        : 'bg-black/40 text-zinc-300 border-white/10 hover:bg-white/10'
                    }`}
                  >
                    Auto
                  </button>
                  {PALETTE.map((pal, idx) => (
                    <button
                      key={idx}
                      onClick={() => setAccentColor(idx)}
                      className={`w-7 h-7 rounded-full transition-transform ${
                        accentColor === idx ? 'scale-125 ring-2 ring-white shadow-lg' : 'hover:scale-110'
                      }`}
                      style={{ backgroundColor: pal.hex }}
                      title={pal.name}
                    />
                  ))}
                </div>
              </div>

              {/* Label Opacity */}
              <div>
                <div className="flex justify-between text-xs text-zinc-300 mb-1.5 font-bold">
                  <span>Непрозрачность подложки названий:</span>
                  <span className="font-mono text-cyan-400">{Math.round((labelOpacity / 255) * 100)}%</span>
                </div>
                <input
                  type="range"
                  min="0"
                  max="255"
                  value={labelOpacity}
                  onChange={(e) => setLabelOpacity(parseInt(e.target.value, 10))}
                  className="w-full accent-cyan-400"
                />
              </div>

              {/* Workspace Actions: Duplicate & Reorder */}
              <div className="pt-3 border-t border-white/10 flex flex-wrap gap-2">
                <button
                  onClick={() => {
                    sound.playSelect();
                    onDuplicateWorkspace(workspace);
                    onClose();
                  }}
                  className="flex items-center gap-1.5 px-3 py-2 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10"
                >
                  <Copy className="w-3.5 h-3.5 text-emerald-400" /> Дублировать стол
                </button>

                {workspaceIndex > 0 && (
                  <button
                    onClick={() => {
                      sound.playClick();
                      onReorderWorkspace(workspaceIndex, workspaceIndex - 1);
                    }}
                    className="flex items-center gap-1 px-3 py-2 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10"
                  >
                    <ArrowLeft className="w-3.5 h-3.5" /> Влево
                  </button>
                )}

                {workspaceIndex < totalWorkspaces - 1 && (
                  <button
                    onClick={() => {
                      sound.playClick();
                      onReorderWorkspace(workspaceIndex, workspaceIndex + 1);
                    }}
                    className="flex items-center gap-1 px-3 py-2 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10"
                  >
                    Вправо <ArrowRight className="w-3.5 h-3.5" />
                  </button>
                )}
              </div>
            </div>
          )}

          {/* SECTION 2: WALLPAPER & BLUR */}
          {activeSection === 'wallpaper' && (
            <div className="space-y-5">
              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-2">
                  Фоновые обои для стола "{workspace.name}":
                </label>
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-2.5">
                  {wallpaperPresets.map((wp) => (
                    <button
                      key={wp.name}
                      onClick={() => setBgPath(wp.url)}
                      className={`p-3 rounded-2xl border text-center text-xs font-bold transition-all ${
                        bgPath === wp.url
                          ? 'bg-cyan-500/20 border-cyan-400 text-cyan-300 ring-2 ring-cyan-400/30'
                          : 'bg-zinc-900/60 hover:bg-zinc-800 border-white/10 text-zinc-300'
                      }`}
                    >
                      {wp.name}
                    </button>
                  ))}
                </div>
              </div>

              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-1.5">
                  Или ссылка на собственную картинку (JPG/PNG):
                </label>
                <input
                  type="text"
                  value={bgPath}
                  onChange={(e) => setBgPath(e.target.value)}
                  placeholder="https://... или оставьте пустым"
                  className="w-full bg-zinc-900/90 border border-white/15 rounded-2xl px-4 py-2.5 text-xs text-white focus:outline-none focus:border-cyan-400"
                />
              </div>

              {/* Blur & Brightness Sliders */}
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 pt-1">
                <div>
                  <div className="flex justify-between text-xs font-bold text-zinc-300 mb-1.5">
                    <span>Размытие обоев (Blur):</span>
                    <span className="font-mono text-cyan-400">{bgBlur}px</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="20"
                    value={bgBlur}
                    onChange={(e) => setBgBlur(parseInt(e.target.value, 10))}
                    className="w-full accent-cyan-400"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs font-bold text-zinc-300 mb-1.5">
                    <span>Яркость обоев:</span>
                    <span className="font-mono text-cyan-400">{bgBrightness}%</span>
                  </div>
                  <input
                    type="range"
                    min="20"
                    max="100"
                    value={bgBrightness}
                    onChange={(e) => setBgBrightness(parseInt(e.target.value, 10))}
                    className="w-full accent-cyan-400"
                  />
                </div>
              </div>

              {/* Background Fit */}
              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-1.5">
                  Масштабирование фона:
                </label>
                <div className="grid grid-cols-3 gap-2">
                  {[
                    { id: 0, label: 'Cover (Заполнение)' },
                    { id: 1, label: 'Contain (Целиком)' },
                    { id: 2, label: 'Stretch (Растянуть)' },
                  ].map((mode) => (
                    <button
                      key={mode.id}
                      onClick={() => setBackgroundFit(mode.id)}
                      className={`py-2 rounded-xl text-xs font-bold border transition-all ${
                        backgroundFit === mode.id
                          ? 'bg-cyan-500 text-black border-cyan-400'
                          : 'bg-zinc-900/60 text-zinc-300 hover:bg-zinc-800 border-white/10'
                      }`}
                    >
                      {mode.label}
                    </button>
                  ))}
                </div>
              </div>
            </div>
          )}

          {/* SECTION 3: TILES LIST & QUICK REORGANIZE */}
          {activeSection === 'tiles' && (
            <div className="space-y-4">
              <div className="flex items-center justify-between">
                <span className="text-xs font-bold text-zinc-300">
                  Плитки на этом столе ({workspace.tiles.length}):
                </span>
                <button
                  onClick={handleCompactTiles}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded-xl bg-cyan-600/30 hover:bg-cyan-600/50 text-cyan-200 border border-cyan-400/30 text-xs font-bold transition-all"
                  title="Автоматически убрать пустые дыры между плитками"
                >
                  <Grid className="w-3.5 h-3.5" /> Компактизировать сетку
                </button>
              </div>

              <div className="space-y-2 max-h-72 overflow-y-auto scrollbar-thin">
                {workspace.tiles.length === 0 ? (
                  <div className="p-8 text-center text-zinc-500 text-xs font-medium">
                    На этом столе пока нет плиток. Откройте «Каталог и поиск» и нажмите «+», чтобы закрепить игры.
                  </div>
                ) : (
                  workspace.tiles.map((tile) => (
                    <div
                      key={tile.id}
                      className="p-3 rounded-2xl bg-zinc-900/70 border border-white/10 flex items-center justify-between gap-3"
                    >
                      <div className="flex items-center gap-3 min-w-0">
                        {tile.icon && (
                          <img src={tile.icon} alt={tile.title} className="w-9 h-9 rounded-xl object-cover shrink-0 shadow" />
                        )}
                        <div className="min-w-0">
                          <div className="text-xs font-bold text-white truncate">{tile.title}</div>
                          <div className="text-[10px] text-zinc-400 font-mono">
                            Позиция: ({tile.x}, {tile.y}) · Размер: {tile.w}×{tile.h}
                          </div>
                        </div>
                      </div>

                      <button
                        onClick={() => handleRemoveTileFromDesk(tile.id)}
                        className="p-2 rounded-xl bg-zinc-800 hover:bg-red-500/30 text-zinc-400 hover:text-red-300 transition-all shrink-0"
                        title="Удалить со стола"
                      >
                        <Trash2 className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  ))
                )}
              </div>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-3.5 bg-zinc-950 border-t border-white/10 flex justify-end gap-2.5">
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-2xl text-xs font-bold text-zinc-400 hover:text-white"
          >
            Отмена
          </button>
          <button
            onClick={handleSave}
            className="flex items-center gap-2 px-5 py-2.5 rounded-2xl bg-cyan-500 hover:bg-cyan-400 text-black font-bold text-xs shadow-lg transition-all active:scale-95"
          >
            <Check className="w-4 h-4" /> Применить настройки
          </button>
        </div>
      </div>
    </div>
  );
};
