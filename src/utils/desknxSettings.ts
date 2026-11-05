import { DeskNXSettings, FontOption } from '../types/desknx';

export const DEFAULT_SETTINGS: DeskNXSettings = {
  theme: 'ulauncher_dark',
  iconPack: 'Default',
  backgroundPath: '',
  backgroundFit: 0,
  fontName: 'Inter (Clean Switch)',
  fontFamily: 'inter',
  fontSizeScale: 'large',
  showScanlines: false,
  soundEffects: true,
  showJoyconOverlay: false,
  systemMode: false,
  homeInstalled: false,
  labelOpacity: 196,
  ulauncherStyle: true,
  cacheWarmMode: true,
  bgBlur: 0,
  bgBrightness: 85,
  cardRadius: 'squircle',
  spotlightHotkey: 'Space / Y',
};

export const FONT_OPTIONS: Array<{ id: FontOption; name: string; css: string; description: string }> = [
  {
    id: 'inter',
    name: 'Nintendo Sans (Inter)',
    css: "'Inter', -apple-system, BlinkMacSystemFont, sans-serif",
    description: 'Crisp, high-legibility UI font optimized for small handheld screens',
  },
  {
    id: 'chakra',
    name: 'Chakra Petch (Console Gamer)',
    css: "'Chakra Petch', sans-serif",
    description: 'Futuristic sci-fi / esports typography with sharp chamfered numerals',
  },
  {
    id: 'jetbrains',
    name: 'JetBrains Mono (Developer)',
    css: "'JetBrains Mono', monospace",
    description: 'Ultra-readable monospace font with clear tabular numbers',
  },
  {
    id: 'rounded',
    name: 'Modern Soft Rounded',
    css: "'Nunito', system-ui, sans-serif",
    description: 'Friendly rounded curves matching the Nintendo Switch physical contours',
  },
  {
    id: 'system',
    name: 'System Default',
    css: 'system-ui, -apple-system, sans-serif',
    description: 'Native OS font rendering',
  },
];

export const THEME_CONFIGS = {
  ulauncher_dark: {
    name: 'uLauncher Obsidian',
    description: 'Ultra-clean modern dark glass with ambient neon edge glow',
    bgGradient: 'from-[#0b0b0e] via-[#0f0f14] to-[#121218]',
    cardBg: 'bg-zinc-900/80 hover:bg-zinc-800/90 border-white/10 hover:border-cyan-400/40 shadow-xl',
    tileSelected: 'ring-3 ring-cyan-400 shadow-[0_0_35px_rgba(34,211,238,0.5)]',
    textMain: 'text-white',
    textMuted: 'text-zinc-400',
    accent: '#06b6d4',
  },
  frosted_glass: {
    name: 'Frosted Glass',
    description: 'Light crystalline glass design',
    bgGradient: 'from-slate-900/95 via-slate-800/90 to-indigo-950/95',
    cardBg: 'bg-white/10 hover:bg-white/15 border-white/20',
    tileSelected: 'ring-3 ring-cyan-400 shadow-[0_0_25px_rgba(34,211,238,0.5)]',
    textMain: 'text-white',
    textMuted: 'text-slate-300',
    accent: '#38bdf8',
  },
  graphite: {
    name: 'Graphite',
    description: 'Neutral matte dark-grey stealth look',
    bgGradient: 'from-[#09090b] via-[#121215] to-[#18181b]',
    cardBg: 'bg-zinc-800/70 hover:bg-zinc-800/90 border-zinc-700/50',
    tileSelected: 'ring-3 ring-zinc-100 shadow-[0_0_25px_rgba(255,255,255,0.4)]',
    textMain: 'text-zinc-100',
    textMuted: 'text-zinc-400',
    accent: '#71717a',
  },
  violet: {
    name: 'Violet',
    description: 'Deep royal purple and neon amethyst',
    bgGradient: 'from-purple-950 via-[#0d0914] to-indigo-950',
    cardBg: 'bg-purple-900/40 hover:bg-purple-900/60 border-purple-500/30',
    tileSelected: 'ring-3 ring-purple-400 shadow-[0_0_25px_rgba(192,132,252,0.6)]',
    textMain: 'text-purple-100',
    textMuted: 'text-purple-300',
    accent: '#a855f7',
  },
  sunset: {
    name: 'Sunset',
    description: 'Warm dusk gradient with amber radiance',
    bgGradient: 'from-[#1a0808] via-[#120a0a] to-[#1a1208]',
    cardBg: 'bg-amber-950/40 hover:bg-amber-900/50 border-amber-500/30',
    tileSelected: 'ring-3 ring-amber-400 shadow-[0_0_25px_rgba(251,191,36,0.6)]',
    textMain: 'text-amber-100',
    textMuted: 'text-amber-300',
    accent: '#f59e0b',
  },
  ocean: {
    name: 'Ocean',
    description: 'Deep oceanic blue palette from original DeskNX',
    bgGradient: 'from-[#051329] via-[#091b36] to-[#040e1f]',
    cardBg: 'bg-blue-900/40 hover:bg-blue-900/60 border-blue-500/30',
    tileSelected: 'ring-3 ring-cyan-400 shadow-[0_0_25px_rgba(6,182,212,0.6)]',
    textMain: 'text-sky-100',
    textMuted: 'text-sky-300',
    accent: '#0284c7',
  },
};

export function serializeSettingsIni(settings: DeskNXSettings): string {
  return `; Transwitch / DeskNX Configuration File
; Saved to sdmc:/switch/DeskNX/settings.ini

[General]
theme = ${settings.theme}
icon_pack = ${settings.iconPack}
background_path = ${settings.backgroundPath}
background_fit = ${settings.backgroundFit}
font_name = ${settings.fontName}
font_family = ${settings.fontFamily}
font_size_scale = ${settings.fontSizeScale}
label_opacity = ${settings.labelOpacity}
card_radius = ${settings.cardRadius}

[Display]
show_scanlines = ${settings.showScanlines ? '1' : '0'}
controller_overlay = ${settings.showJoyconOverlay ? '1' : '0'}
sound_effects = ${settings.soundEffects ? '1' : '0'}
bg_blur = ${settings.bgBlur}
bg_brightness = ${settings.bgBrightness}

[System]
home_installed = ${settings.homeInstalled ? '1' : '0'}
system_mode = ${settings.systemMode ? '1' : '0'}
cache_warm_mode = ${settings.cacheWarmMode ? '1' : '0'}
ulauncher_style = ${settings.ulauncherStyle ? '1' : '0'}
ipc_version = 2
`;
}

export function parseSettingsIni(iniContent: string): DeskNXSettings {
  const settings: DeskNXSettings = { ...DEFAULT_SETTINGS };
  const lines = iniContent.split(/\r?\n/);

  for (const rawLine of lines) {
    const line = rawLine.trim();
    if (!line || line.startsWith(';') || line.startsWith('#') || line.startsWith('[')) continue;
    const eqIdx = line.indexOf('=');
    if (eqIdx === -1) continue;

    const key = line.substring(0, eqIdx).trim().toLowerCase();
    const val = line.substring(eqIdx + 1).trim();

    if (key === 'theme') {
      if (['ulauncher_dark', 'frosted_glass', 'graphite', 'violet', 'sunset', 'ocean'].includes(val)) {
        settings.theme = val as any;
      }
    } else if (key === 'font_family') {
      if (['inter', 'chakra', 'jetbrains', 'system', 'rounded'].includes(val)) {
        settings.fontFamily = val as any;
      }
    } else if (key === 'font_size_scale') {
      if (['normal', 'large', 'xlarge'].includes(val)) {
        settings.fontSizeScale = val as any;
      }
    } else if (key === 'home_installed') {
      settings.homeInstalled = val === '1' || val === 'true';
    } else if (key === 'icon_pack') {
      settings.iconPack = val;
    } else if (key === 'background_path') {
      settings.backgroundPath = val;
    } else if (key === 'background_fit') {
      settings.backgroundFit = parseInt(val, 10) || 0;
    } else if (key === 'font_name') {
      settings.fontName = val;
    } else if (key === 'label_opacity') {
      settings.labelOpacity = Math.max(0, Math.min(255, parseInt(val, 10) || 196));
    } else if (key === 'show_scanlines') {
      settings.showScanlines = val === '1' || val === 'true';
    } else if (key === 'controller_overlay') {
      settings.showJoyconOverlay = val === '1' || val === 'true';
    } else if (key === 'sound_effects') {
      settings.soundEffects = val === '1' || val === 'true';
    } else if (key === 'system_mode') {
      settings.systemMode = val === '1' || val === 'true';
    } else if (key === 'cache_warm_mode') {
      settings.cacheWarmMode = val === '1' || val === 'true';
    } else if (key === 'bg_blur') {
      settings.bgBlur = parseInt(val, 10) || 0;
    } else if (key === 'bg_brightness') {
      settings.bgBrightness = parseInt(val, 10) || 85;
    } else if (key === 'card_radius') {
      if (['squircle', 'rounded', 'sharp'].includes(val)) {
        settings.cardRadius = val as any;
      }
    }
  }

  return settings;
}
