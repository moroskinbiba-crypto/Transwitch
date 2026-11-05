import React, { useState } from 'react';
import { DeskNXSettings, Workspace, FontOption } from '../types/desknx';
import { THEME_CONFIGS, FONT_OPTIONS, serializeSettingsIni, parseSettingsIni, DEFAULT_SETTINGS } from '../utils/desknxSettings';
import { serializeLayout, parseLayout, PALETTE } from '../utils/desknxLayout';
import { CATALOG_ITEMS, INITIAL_WORKSPACES, sound } from '../data/mockCatalog';
import { 
  X, 
  Settings as SettingsIcon, 
  Palette, 
  Image as ImageIcon, 
  Download, 
  Upload, 
  Volume2, 
  Monitor, 
  RotateCcw, 
  Check, 
  Gamepad2, 
  Type, 
  ShieldCheck, 
  ShieldAlert, 
  AlertTriangle,
  RotateCw
} from 'lucide-react';

interface SettingsModalProps {
  isOpen: boolean;
  onClose: () => void;
  settings: DeskNXSettings;
  onUpdateSettings: (newSettings: DeskNXSettings) => void;
  workspaces: Workspace[];
  activeWorkspaceIndex: number;
  onUpdateWorkspaces: (workspaces: Workspace[], activeIndex: number) => void;
}

export const SettingsModal: React.FC<SettingsModalProps> = ({
  isOpen,
  onClose,
  settings,
  onUpdateSettings,
  workspaces,
  activeWorkspaceIndex,
  onUpdateWorkspaces,
}) => {
  const [activeTab, setActiveTab] = useState<'themes' | 'fonts' | 'wallpaper' | 'audio_display' | 'system' | 'backup'>('system');
  const [customBgInput, setCustomBgInput] = useState(settings.backgroundPath);
  const [importStatus, setImportStatus] = useState<string | null>(null);

  if (!isOpen) return null;

  const handleThemeChange = (themeKey: DeskNXSettings['theme']) => {
    sound.playSelect();
    onUpdateSettings({ ...settings, theme: themeKey });
  };

  const handleFontChange = (fontId: FontOption) => {
    sound.playSelect();
    const found = FONT_OPTIONS.find((f) => f.id === fontId);
    onUpdateSettings({ 
      ...settings, 
      fontFamily: fontId, 
      fontName: found?.name || 'System' 
    });
  };

  const handleApplyBg = (path: string) => {
    sound.playClick();
    setCustomBgInput(path);
    onUpdateSettings({ ...settings, backgroundPath: path });
  };

  const handleInstallAsHome = () => {
    sound.playLaunch();
    onUpdateSettings({
      ...settings,
      homeInstalled: true,
      systemMode: true,
    });
    setImportStatus('Лаунчер успешно установлен как основной HOME лаунчер (qlaunch override active)');
    setTimeout(() => setImportStatus(null), 4000);
  };

  const handleUninstallHome = () => {
    sound.playBack();
    onUpdateSettings({
      ...settings,
      homeInstalled: false,
      systemMode: false,
    });
    setImportStatus('Лаунчер безопасно отключен. Восстановлено стандартное меню Nintendo.');
    setTimeout(() => setImportStatus(null), 4000);
  };

  const handleExportLayout = () => {
    sound.playSelect();
    const tsv = serializeLayout(workspaces, activeWorkspaceIndex);
    const blob = new Blob([tsv], { type: 'text/tab-separated-values' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'layout.tsv';
    a.click();
    URL.revokeObjectURL(url);
  };

  const handleExportSettings = () => {
    sound.playSelect();
    const ini = serializeSettingsIni(settings);
    const blob = new Blob([ini], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'settings.ini';
    a.click();
    URL.revokeObjectURL(url);
  };

  const handleImportLayoutFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (event) => {
      const text = event.target?.result as string;
      const parsed = parseLayout(text, (data, kind) => {
        const found = CATALOG_ITEMS.find((c) => c.data === data || c.title === data);
        return { icon: found?.icon, banner: found?.banner };
      });

      if (parsed) {
        sound.playSelect();
        onUpdateWorkspaces(parsed.workspaces, parsed.activeIndex);
        setImportStatus('Layout successfully imported!');
      } else {
        sound.playBack();
        setImportStatus('Error: Malformed layout.tsv format');
      }
      setTimeout(() => setImportStatus(null), 3000);
    };
    reader.readAsText(file);
  };

  const handleImportSettingsFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (event) => {
      const text = event.target?.result as string;
      try {
        const parsed = parseSettingsIni(text);
        sound.playSelect();
        onUpdateSettings(parsed);
        setImportStatus('Settings INI loaded successfully!');
      } catch (err) {
        sound.playBack();
        setImportStatus('Failed to parse settings.ini');
      }
      setTimeout(() => setImportStatus(null), 3000);
    };
    reader.readAsText(file);
  };

  const handleResetDefaults = () => {
    if (confirm('Reset Transwitch layout and configuration to defaults?')) {
      sound.playSelect();
      onUpdateWorkspaces(INITIAL_WORKSPACES, 0);
      onUpdateSettings(DEFAULT_SETTINGS);
      setImportStatus('Restored factory layout.');
    }
  };

  const wallpaperPresets = [
    {
      name: 'uLauncher Obsidian',
      path: '',
      thumb: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=200&auto=format&fit=crop&q=80',
    },
    {
      name: 'Hyrule Kingdom',
      path: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=1600&auto=format&fit=crop&q=80',
      thumb: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=200&auto=format&fit=crop&q=80',
    },
    {
      name: 'Neon Cyberpunk',
      path: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=1600&auto=format&fit=crop&q=80',
      thumb: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=200&auto=format&fit=crop&q=80',
    },
    {
      name: 'Deep Cosmos',
      path: 'https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=1600&auto=format&fit=crop&q=80',
      thumb: 'https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=200&auto=format&fit=crop&q=80',
    },
  ];

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 md:p-8 bg-black/85 backdrop-blur-xl animate-in fade-in select-none text-zinc-100">
      <div className="relative w-full max-w-4xl h-[86vh] bg-[#111116] border border-white/20 rounded-3xl shadow-[0_25px_80px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col">
        
        {/* Header */}
        <div className="px-6 py-4 bg-zinc-900/80 border-b border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-2xl bg-cyan-500/20 border border-cyan-400/30 flex items-center justify-center text-cyan-400 shadow">
              <SettingsIcon className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-base font-bold text-white tracking-tight">System Settings & Integration</h2>
              <p className="text-xs text-zinc-400">
                Customization · Typography · Atmosphère HOME Integration · Backup
              </p>
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

        {/* Navigation Tabs */}
        <div className="px-6 py-2.5 bg-black/60 border-b border-white/10 flex items-center gap-2 overflow-x-auto">
          {[
            { id: 'system', label: 'HOME Menu Integration', icon: ShieldCheck },
            { id: 'fonts', label: 'Fonts & Screen Size', icon: Type },
            { id: 'themes', label: 'Color Themes', icon: Palette },
            { id: 'wallpaper', label: 'Wallpaper & Blur', icon: ImageIcon },
            { id: 'audio_display', label: 'Audio & Joy-Con', icon: Monitor },
            { id: 'backup', label: 'Import / Export', icon: Download },
          ].map((tab) => {
            const Icon = tab.icon;
            return (
              <button
                key={tab.id}
                onClick={() => {
                  sound.playClick();
                  setActiveTab(tab.id as any);
                }}
                className={`flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-bold whitespace-nowrap transition-all ${
                  activeTab === tab.id
                    ? 'bg-cyan-500 text-black shadow-md'
                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
                }`}
              >
                <Icon className="w-4 h-4" />
                {tab.label}
              </button>
            );
          })}
        </div>

        {/* Tab Content */}
        <div className="flex-1 p-6 overflow-y-auto">
          {importStatus && (
            <div className="mb-4 p-3.5 rounded-2xl bg-cyan-950/80 border border-cyan-500/40 text-cyan-300 text-xs font-semibold flex items-center gap-2">
              <Check className="w-4 h-4" /> {importStatus}
            </div>
          )}

          {/* TAB 1: SYSTEM & HOME INTEGRATION (USER REQUEST: INSTALL / SAFELY UNINSTALL BUTTONS) */}
          {activeTab === 'system' && (
            <div className="space-y-6 max-w-2xl">
              {/* Current Status Box */}
              <div className={`p-5 rounded-2xl border transition-all ${
                settings.homeInstalled
                  ? 'bg-emerald-950/30 border-emerald-500/40'
                  : 'bg-zinc-900/60 border-white/10'
              }`}>
                <div className="flex items-center justify-between mb-3">
                  <div className="flex items-center gap-3">
                    <div className={`w-3.5 h-3.5 rounded-full ${
                      settings.homeInstalled ? 'bg-emerald-400 animate-pulse' : 'bg-zinc-500'
                    }`} />
                    <div>
                      <h4 className="text-base font-bold text-white">
                        {settings.homeInstalled 
                          ? 'Лаунчер установлен как основной HOME лаунчер' 
                          : 'Лаунчер работает в тестовом режиме (Standalone)'}
                      </h4>
                      <p className="text-xs text-zinc-400 mt-0.5">
                        {settings.homeInstalled
                          ? 'Перехват qlaunch активен. Нажатие кнопки HOME возвращает в этот рабочий стол.'
                          : 'Консоль использует оригинальное меню Nintendo. Лаунчер запускается через HBMenu.'}
                      </p>
                    </div>
                  </div>
                </div>

                <div className="pt-2 flex flex-col sm:flex-row gap-3">
                  {!settings.homeInstalled ? (
                    <button
                      onClick={handleInstallAsHome}
                      className="flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl bg-cyan-500 hover:bg-cyan-400 text-black font-bold text-xs shadow-lg transition-all active:scale-95"
                    >
                      <ShieldCheck className="w-4 h-4" /> Установить как основной HOME лаунчер
                    </button>
                  ) : (
                    <button
                      onClick={handleUninstallHome}
                      className="flex-1 flex items-center justify-center gap-2 py-3 rounded-2xl bg-red-600 hover:bg-red-500 text-white font-bold text-xs shadow-lg transition-all active:scale-95"
                    >
                      <RotateCcw className="w-4 h-4" /> Безопасно удалить и вернуть стандартное меню Nintendo
                    </button>
                  )}
                </div>
              </div>

              {/* Emergency Recovery Reminder */}
              <div className="p-4 rounded-2xl bg-amber-950/30 border border-amber-500/30 text-amber-200 text-xs leading-relaxed flex items-start gap-3">
                <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" />
                <div>
                  <strong className="block text-amber-300 font-bold mb-1">
                    Аварийный откат (Hardware Safety Override):
                  </strong>
                  Если лаунчер когда-либо зависнет на реальной консоли, зажатие комбинации 
                  <span className="text-white bg-black/50 px-1.5 py-0.5 rounded font-mono mx-1">
                    L + R + ZL + ZR + Plus + Minus + D-Pad Down
                  </span>
                  на 3 секунды аппаратно отключает оверрайд qlaunch и возвращает стандартную прошивку.
                </div>
              </div>
            </div>
          )}

          {/* TAB 2: FONTS & SCALE (USER REQUEST: ENLARGE FONTS, FONT PICKER) */}
          {activeTab === 'fonts' && (
            <div className="space-y-6 max-w-2xl">
              <div>
                <label className="block text-sm font-bold text-white mb-2">
                  Системный шрифт лаунчера (Font Selector):
                </label>
                <div className="space-y-2.5">
                  {FONT_OPTIONS.map((font) => {
                    const isSelected = settings.fontFamily === font.id;
                    return (
                      <div
                        key={font.id}
                        onClick={() => handleFontChange(font.id)}
                        className={`p-3.5 rounded-2xl cursor-pointer border transition-all flex items-center justify-between ${
                          isSelected
                            ? 'bg-cyan-500/15 border-cyan-400/60 ring-2 ring-cyan-400/30'
                            : 'bg-zinc-900/60 hover:bg-zinc-800 border-white/10'
                        }`}
                      >
                        <div>
                          <div className="text-base font-bold text-white" style={{ fontFamily: font.css }}>
                            {font.name}
                          </div>
                          <div className="text-xs text-zinc-400 mt-0.5">
                            {font.description}
                          </div>
                        </div>
                        {isSelected && (
                          <div className="w-6 h-6 rounded-full bg-cyan-500 text-black flex items-center justify-center font-bold">
                            <Check className="w-4 h-4" />
                          </div>
                        )}
                      </div>
                    );
                  })}
                </div>
              </div>

              {/* Font Size Scaling */}
              <div className="pt-2 border-t border-white/10">
                <label className="block text-sm font-bold text-white mb-1.5">
                  Размер шрифта для экрана Switch (6.2" / 7"):
                </label>
                <div className="grid grid-cols-3 gap-2.5 mt-2">
                  {[
                    { id: 'normal', label: 'Обычный (Normal)' },
                    { id: 'large', label: 'Крупный (Рекомендуется)' },
                    { id: 'xlarge', label: 'Максимальный (X-Large)' },
                  ].map((scale) => (
                    <button
                      key={scale.id}
                      onClick={() => {
                        sound.playSelect();
                        onUpdateSettings({ ...settings, fontSizeScale: scale.id as any });
                      }}
                      className={`py-3 rounded-2xl text-xs font-bold border transition-all ${
                        settings.fontSizeScale === scale.id
                          ? 'bg-cyan-500 text-black border-cyan-400 shadow-md'
                          : 'bg-zinc-900/70 text-zinc-300 hover:bg-zinc-800 border-white/10'
                      }`}
                    >
                      {scale.label}
                    </button>
                  ))}
                </div>
              </div>
            </div>
          )}

          {/* TAB 3: COLOR THEMES */}
          {activeTab === 'themes' && (
            <div className="space-y-4 max-w-2xl">
              <div className="text-sm font-bold text-white mb-2">
                uLauncher & Console Themes:
              </div>
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3.5">
                {(Object.keys(THEME_CONFIGS) as Array<keyof typeof THEME_CONFIGS>).map((key) => {
                  const item = THEME_CONFIGS[key];
                  const isSelected = settings.theme === key;
                  return (
                    <div
                      key={key}
                      onClick={() => handleThemeChange(key)}
                      className={`p-4 rounded-2xl cursor-pointer border transition-all ${
                        isSelected
                          ? 'border-cyan-400 ring-2 ring-cyan-400/50 bg-white/10'
                          : 'border-white/10 bg-black/40 hover:bg-white/5'
                      }`}
                    >
                      <div className="flex items-center justify-between mb-2">
                        <span className="font-bold text-sm text-white">{item.name}</span>
                        {isSelected && (
                          <span className="text-[10px] bg-cyan-500 text-black font-bold px-2 py-0.5 rounded-full">
                            ACTIVE
                          </span>
                        )}
                      </div>
                      <p className="text-xs text-zinc-400 mb-3">{item.description}</p>
                      <div
                        className={`w-full h-8 rounded-xl bg-gradient-to-r ${item.bgGradient} border border-white/20`}
                      />
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          {/* TAB 4: WALLPAPER & BLUR */}
          {activeTab === 'wallpaper' && (
            <div className="space-y-6 max-w-2xl">
              <div>
                <label className="block text-sm font-bold text-white mb-2">
                  Wallpapers (sdmc:/switch/DeskNX/media/):
                </label>
                <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
                  {wallpaperPresets.map((wp) => (
                    <div
                      key={wp.name}
                      onClick={() => handleApplyBg(wp.path)}
                      className={`rounded-2xl overflow-hidden cursor-pointer border transition-all ${
                        settings.backgroundPath === wp.path
                          ? 'border-cyan-400 ring-2 ring-cyan-400/50 scale-[1.02]'
                          : 'border-white/10 hover:border-white/30'
                      }`}
                    >
                      <img src={wp.thumb} alt={wp.name} className="w-full h-24 object-cover" />
                      <div className="p-2 bg-black/80 text-center text-xs font-semibold text-white truncate">
                        {wp.name}
                      </div>
                    </div>
                  ))}
                </div>
              </div>

              <div>
                <label className="block text-xs font-bold text-zinc-300 mb-1.5">
                  Custom Wallpaper URL:
                </label>
                <div className="flex gap-2">
                  <input
                    type="text"
                    value={customBgInput}
                    onChange={(e) => setCustomBgInput(e.target.value)}
                    placeholder="https://... or sdmc:/switch/DeskNX/media/wallpaper.jpg"
                    className="flex-1 bg-black/50 border border-white/10 rounded-2xl px-4 py-2.5 text-xs text-white placeholder-zinc-500 focus:outline-none focus:border-cyan-400"
                  />
                  <button
                    onClick={() => handleApplyBg(customBgInput)}
                    className="px-5 py-2.5 bg-cyan-500 text-black font-bold text-xs rounded-2xl hover:bg-cyan-400"
                  >
                    Set
                  </button>
                </div>
              </div>
            </div>
          )}

          {/* TAB 5: AUDIO & CONTROLLER */}
          {activeTab === 'audio_display' && (
            <div className="space-y-4 max-w-xl">
              <div className="flex items-center justify-between p-4 rounded-2xl bg-zinc-900/60 border border-white/10">
                <div className="flex items-center gap-3">
                  <Volume2 className="w-5 h-5 text-cyan-400" />
                  <div>
                    <div className="text-sm font-bold text-white">Nintendo UI Audio Effects</div>
                    <div className="text-xs text-zinc-400">Switch navigation beeps, snaps, and clicks</div>
                  </div>
                </div>
                <input
                  type="checkbox"
                  checked={settings.soundEffects}
                  onChange={(e) => {
                    const checked = e.target.checked;
                    sound.enabled = checked;
                    onUpdateSettings({ ...settings, soundEffects: checked });
                  }}
                  className="w-5 h-5 accent-cyan-400 cursor-pointer"
                />
              </div>

              <div className="flex items-center justify-between p-4 rounded-2xl bg-zinc-900/60 border border-white/10">
                <div className="flex items-center gap-3">
                  <Gamepad2 className="w-5 h-5 text-rose-400" />
                  <div>
                    <div className="text-sm font-bold text-white">Virtual Joy-Con Controller Overlay</div>
                    <div className="text-xs text-zinc-400">Touch and keyboard navigable Nintendo Switch Joy-Con bar</div>
                  </div>
                </div>
                <input
                  type="checkbox"
                  checked={settings.showJoyconOverlay}
                  onChange={(e) =>
                    onUpdateSettings({ ...settings, showJoyconOverlay: e.target.checked })
                  }
                  className="w-5 h-5 accent-cyan-400 cursor-pointer"
                />
              </div>
            </div>
          )}

          {/* TAB 6: BACKUP & CONFIG */}
          {activeTab === 'backup' && (
            <div className="space-y-6 max-w-xl">
              <div>
                <h4 className="text-xs font-bold text-zinc-200 uppercase tracking-wider mb-2">
                  Export Layout & Settings
                </h4>
                <div className="grid grid-cols-2 gap-3 mb-4">
                  <button
                    onClick={handleExportLayout}
                    className="flex items-center justify-center gap-2 p-3.5 rounded-2xl bg-zinc-800 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10"
                  >
                    <Download className="w-4 h-4 text-cyan-400" /> Export layout.tsv
                  </button>

                  <button
                    onClick={handleExportSettings}
                    className="flex items-center justify-center gap-2 p-3.5 rounded-2xl bg-zinc-800 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10"
                  >
                    <Download className="w-4 h-4 text-emerald-400" /> Export settings.ini
                  </button>
                </div>

                <div className="grid grid-cols-2 gap-3">
                  <label className="flex items-center justify-center gap-2 p-3.5 rounded-2xl bg-zinc-800/80 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10 cursor-pointer">
                    <Upload className="w-4 h-4 text-amber-400" /> Import layout.tsv
                    <input
                      type="file"
                      accept=".tsv,.txt"
                      onChange={handleImportLayoutFile}
                      className="hidden"
                    />
                  </label>

                  <label className="flex items-center justify-center gap-2 p-3.5 rounded-2xl bg-zinc-800/80 hover:bg-zinc-700 text-xs font-bold text-white border border-white/10 cursor-pointer">
                    <Upload className="w-4 h-4 text-purple-400" /> Import settings.ini
                    <input
                      type="file"
                      accept=".ini,.txt"
                      onChange={handleImportSettingsFile}
                      className="hidden"
                    />
                  </label>
                </div>
              </div>

              <div className="pt-4 border-t border-white/10">
                <button
                  onClick={handleResetDefaults}
                  className="flex items-center gap-2 px-4 py-3 rounded-2xl bg-red-950/40 hover:bg-red-900/60 border border-red-500/30 text-xs font-bold text-red-300 transition-all"
                >
                  <RotateCcw className="w-4 h-4" /> Reset All to Default Switch Layout
                </button>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
