export type Kind =
  | 'Game'
  | 'Nro'
  | 'Clock'
  | 'Battery'
  | 'System'
  | 'Recent'
  | 'Storage'
  | 'Calendar'
  | 'Unsupported';

export interface Tile {
  id: string;
  kind: Kind;
  title: string;
  data: string; // TitleID for Game, path/filename for NRO, widget key for widgets
  x: number; // 0..5
  y: number; // 0..2
  w: number; // 1, 2, or 3
  h: number; // 1 or 2
  color: number; // 0..7
  unknownTypeCode?: string;
  icon?: string;
  banner?: string;
  badge?: string;
}

export interface Workspace {
  id: string;
  name: string;
  tiles: Tile[];
  backgroundPath?: string;
  backgroundFit: number; // 0 = cover, 1 = contain, 2 = stretch
  artworkMode: number; // 0 = cover, 1 = contain, 2 = stretch
  labelOpacity: number; // 0..255
  accentColor: number; // -1 = default, 0..7 = palette entry
  bgBlur?: number; // 0..20 px blur
  bgBrightness?: number; // 20..100%
  cardStyle?: 'glass' | 'matte' | 'border' | 'cyber';
}

export interface Candidate {
  id: string;
  kind: Kind;
  title: string;
  data: string; // Title ID or file path
  author?: string;
  version?: string;
  size?: string;
  w: number;
  h: number;
  color: number;
  playtimeSeconds: number;
  lastPlayed: number; // timestamp
  launchCount: number;
  icon: string;
  banner?: string;
  description?: string;
  category?: 'Game' | 'Homebrew' | 'Widget';
  cachedAt?: number;
  tags?: string[];
}

export type FontOption = 'inter' | 'chakra' | 'jetbrains' | 'system' | 'rounded';

export interface DeskNXSettings {
  theme: 'frosted_glass' | 'graphite' | 'violet' | 'sunset' | 'ocean' | 'ulauncher_dark';
  iconPack: string;
  backgroundPath: string;
  backgroundFit: number;
  fontName: string;
  fontFamily: FontOption;
  fontSizeScale: 'normal' | 'large' | 'xlarge';
  showScanlines: boolean;
  soundEffects: boolean;
  showJoyconOverlay: boolean;
  systemMode: boolean; // Standalone vs sLauncher qlaunch backend
  homeInstalled: boolean; // installed as primary qlaunch override
  labelOpacity: number;
  ulauncherStyle: boolean;
  cacheWarmMode: boolean;
  donorGameId?: string;
  bgBlur: number;
  bgBrightness: number;
  cardRadius: 'squircle' | 'rounded' | 'sharp';
  spotlightHotkey: string;
}

export interface UserProfile {
  id: string;
  name: string;
  avatar: string;
  color: string;
  workspaces: Workspace[];
  activeDeskIndex: number;
  settings?: Partial<DeskNXSettings>;
}

export interface IPCMessage {
  timestamp: string;
  magic: string; // DKNX or SLNC
  version: number;
  command: string;
  payload: string;
  direction: 'IN' | 'OUT';
  status: 'OK' | 'BUSY' | 'ERROR';
  latencyUs?: number;
}

export interface LogEntry {
  id: string;
  timestamp: string;
  level: 'INFO' | 'WARN' | 'ERROR' | 'DEBUG';
  source: 'catalogue' | 'diagnostics' | 'ipc' | 'backend' | 'cache';
  message: string;
}

export interface CacheStats {
  totalItems: number;
  cacheSizeKb: number;
  lastSyncTimestamp: number;
  hitCount: number;
  missCount: number;
  averageRetrievalMs: number;
  isWarm: boolean;
}
