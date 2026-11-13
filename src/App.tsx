import React, { useState, useEffect, useCallback } from 'react';
import { 
  Candidate, 
  DeskNXSettings, 
  Tile, 
  Workspace,
  UserProfile 
} from './types/desknx';
import { INITIAL_WORKSPACES, sound } from './data/mockCatalog';
import { THEME_CONFIGS, DEFAULT_SETTINGS, FONT_OPTIONS, serializeSettingsIni, parseSettingsIni } from './utils/desknxSettings';
import { findFirstFree, cloneWorkspaces } from './utils/desknxLayout';
import { resolvePlayStats } from './utils/playStats';
import { SLauncherCacheEngine } from './utils/slauncherCache';
import { DeskNXGrid } from './components/DeskNXGrid';
import { TopBar } from './components/TopBar';
import { BottomBar } from './components/BottomBar';
import { UlauncherHub } from './components/UlauncherHub';
import { UnifiedDeskCustomizer } from './components/UnifiedDeskCustomizer';
import { SettingsModal } from './components/SettingsModal';
import { DiagnosticsModal } from './components/DiagnosticsModal';
import { AppLaunchModal } from './components/AppLaunchModal';
import { JoyconOverlay } from './components/JoyconOverlay';
import { UserProfileModal } from './components/UserProfileModal';
import { AlbumModal } from './components/AlbumModal';
import { SoftwareOptionsModal } from './components/SoftwareOptionsModal';
import { ControllersModal } from './components/ControllersModal';
import { Toaster } from './components/Toaster';
import { useSwitchGamepad } from './hooks/useSwitchGamepad';

import { notify } from './ui/toast';
// Each profile gets its own deep copy: sharing the module-level array let a
// rename or tile move on one desk mutate another user's default layout.
const DEFAULT_USERS: UserProfile[] = [
  {
    id: 'user_mario',
    name: 'Mario',
    avatar: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=200&auto=format&fit=crop&q=80',
    color: '#e60012',
    workspaces: cloneWorkspaces(INITIAL_WORKSPACES),
    activeDeskIndex: 0,
  },
  {
    id: 'user_link',
    name: 'Link',
    avatar: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=200&auto=format&fit=crop&q=80',
    color: '#10b981',
    workspaces: [
      {
        id: 'desk_adventure',
        name: 'Hyrule Games',
        backgroundFit: 0,
        artworkMode: 0,
        labelOpacity: 196,
        accentColor: 2,
        tiles: [
          INITIAL_WORKSPACES[0].tiles[3], // Zelda
          INITIAL_WORKSPACES[0].tiles[0], // Clock
          INITIAL_WORKSPACES[0].tiles[1], // Battery
        ],
      },
    ],
    activeDeskIndex: 0,
  },
];

export function App() {
  // First-launch Warm Caching Engine
  useEffect(() => {
    SLauncherCacheEngine.init();
    const hasWarmBoot = localStorage.getItem('transwitch_warm_boot_v2');
    if (!hasWarmBoot) {
      // First run: warm up and cache all discovered games & workspaces
      const { items } = SLauncherCacheEngine.getWarmCatalog();
      localStorage.setItem('transwitch_warm_boot_v2', '1');
      console.log(`[Transwitch] First launch complete: cached ${items.length} titles`);
    }
  }, []);

  // Multiple User Profiles state
  const [users, setUsers] = useState<UserProfile[]>(() => {
    try {
      const saved = localStorage.getItem('transwitch_users_v2');
      if (saved) {
        const parsed = JSON.parse(saved);
        if (Array.isArray(parsed) && parsed.length > 0) return parsed;
      }
    } catch (e) {
      console.warn('Failed to load users', e);
    }
    return DEFAULT_USERS;
  });

  const [activeUserId, setActiveUserId] = useState<string>(() => {
    return localStorage.getItem('transwitch_active_user_id_v2') || DEFAULT_USERS[0].id;
  });

  const activeUser = users.find((u) => u.id === activeUserId) || users[0];

  // Workspaces for active user
  const workspaces = activeUser.workspaces || INITIAL_WORKSPACES;
  const activeWorkspaceIndex = activeUser.activeDeskIndex || 0;

  // Global Settings
  const [settings, setSettings] = useState<DeskNXSettings>(() => {
    try {
      const saved = localStorage.getItem('desknx_settings_ini');
      if (saved) {
        return parseSettingsIni(saved);
      }
    } catch (e) {
      console.warn('Failed to load saved settings.ini', e);
    }
    return DEFAULT_SETTINGS;
  });

  // Modals state - unified hub and single customizer
  const [isHubOpen, setIsHubOpen] = useState(false);
  const [isDeskCustomizerOpen, setIsDeskCustomizerOpen] = useState(false);
  const [isAlbumOpen, setIsAlbumOpen] = useState(false);
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [isDiagnosticsOpen, setIsDiagnosticsOpen] = useState(false);
  const [isUserProfilesOpen, setIsUserProfilesOpen] = useState(false);
  const [isControllersOpen, setIsControllersOpen] = useState(false);
  const [runningCandidate, setRunningCandidate] = useState<Candidate | null>(null);
  const [softwareOptionsCandidate, setSoftwareOptionsCandidate] = useState<Candidate | null>(null);

  const [warmCatalog] = useState<Candidate[]>(() => {
    const { items } = SLauncherCacheEngine.getWarmCatalog();
    return items;
  });

  // Focused tile for controller navigation
  const [focusedTileId, setFocusedTileId] = useState<string | null>(null);

  // Active workspace & theme configuration
  const activeDesk = workspaces[activeWorkspaceIndex] || workspaces[0];
  const themeConfig = THEME_CONFIGS[settings.theme] || THEME_CONFIGS.ulauncher_dark;

  // Selected Font Configuration
  const currentFont = FONT_OPTIONS.find((f) => f.id === settings.fontFamily) || FONT_OPTIONS[0];

  // Helper to persist users & active user's workspaces
  const updateActiveUserWorkspaces = (newWorkspaces: Workspace[], newActiveIndex?: number) => {
    setUsers((prevUsers) => {
      const updated = prevUsers.map((u) => {
        if (u.id === activeUserId) {
          return {
            ...u,
            workspaces: newWorkspaces,
            activeDeskIndex: newActiveIndex !== undefined ? newActiveIndex : u.activeDeskIndex,
          };
        }
        return u;
      });
      try {
        localStorage.setItem('transwitch_users_v2', JSON.stringify(updated));
      } catch (e) {
        console.error(e);
      }
      return updated;
    });
  };

  // Switch User Profile
  const handleSelectUser = (user: UserProfile) => {
    setActiveUserId(user.id);
    localStorage.setItem('transwitch_active_user_id_v2', user.id);
    if (user.workspaces && user.workspaces.length > 0) {
      setFocusedTileId(user.workspaces[user.activeDeskIndex || 0]?.tiles[0]?.id || null);
    }
  };

  const handleCreateUser = (name: string, avatarUrl: string) => {
    const newUser: UserProfile = {
      id: `user_${Date.now()}`,
      name,
      avatar: avatarUrl,
      color: '#00b4e6',
      workspaces: cloneWorkspaces(INITIAL_WORKSPACES),
      activeDeskIndex: 0,
    };
    const updated = [...users, newUser];
    setUsers(updated);
    localStorage.setItem('transwitch_users_v2', JSON.stringify(updated));
    handleSelectUser(newUser);
  };

  const handleDeleteUser = (userId: string) => {
    if (users.length <= 1) return;
    const updated = users.filter((u) => u.id !== userId);
    setUsers(updated);
    localStorage.setItem('transwitch_users_v2', JSON.stringify(updated));
    if (activeUserId === userId) {
      handleSelectUser(updated[0]);
    }
  };

  // Persist settings to localStorage
  useEffect(() => {
    try {
      const ini = serializeSettingsIni(settings);
      localStorage.setItem('desknx_settings_ini', ini);
    } catch (e) {
      console.error(e);
    }
  }, [settings]);

  // Sync sound engine enabled state
  useEffect(() => {
    sound.enabled = settings.soundEffects;
  }, [settings.soundEffects]);

  // Workspace Operations for Active User
  const handleSelectWorkspace = useCallback((index: number) => {
    const safeIdx = Math.max(0, Math.min(index, workspaces.length - 1));
    updateActiveUserWorkspaces(workspaces, safeIdx);
  }, [workspaces]);

  const handleAddWorkspace = (name: string) => {
    const newDesk: Workspace = {
      id: `desk_${Date.now()}`,
      name,
      tiles: [],
      backgroundFit: 0,
      artworkMode: 0,
      labelOpacity: 196,
      accentColor: -1,
      bgBlur: 0,
      bgBrightness: 85,
      cardStyle: 'glass',
    };
    const updated = [...workspaces, newDesk];
    updateActiveUserWorkspaces(updated, updated.length - 1);
  };

  const handleDuplicateWorkspace = (desk: Workspace) => {
    const cloned: Workspace = {
      ...desk,
      id: `desk_${Date.now()}`,
      name: `${desk.name} (Копия)`,
      tiles: desk.tiles.map((t) => ({ ...t, id: `tile_${Date.now()}_${Math.random().toString(36).substring(2, 6)}` })),
    };
    const updated = [...workspaces, cloned];
    updateActiveUserWorkspaces(updated, updated.length - 1);
  };

  const handleReorderWorkspace = (fromIndex: number, toIndex: number) => {
    if (toIndex < 0 || toIndex >= workspaces.length) return;
    const item = workspaces[fromIndex];
    const newWorkspaces = [...workspaces];
    newWorkspaces.splice(fromIndex, 1);
    newWorkspaces.splice(toIndex, 0, item);
    updateActiveUserWorkspaces(newWorkspaces, toIndex);
  };

  const handleRenameWorkspace = (index: number, newName: string) => {
    const updated = workspaces.map((desk, idx) =>
      idx === index ? { ...desk, name: newName } : desk
    );
    updateActiveUserWorkspaces(updated);
  };

  const handleDeleteWorkspace = (index: number) => {
    if (workspaces.length <= 1) return;
    const updated = workspaces.filter((_, idx) => idx !== index);
    updateActiveUserWorkspaces(updated, Math.max(0, index - 1));
  };

  const handleUpdateActiveWorkspace = (updated: Workspace) => {
    const updatedWorkspaces = workspaces.map((d, idx) => (idx === activeWorkspaceIndex ? updated : d));
    updateActiveUserWorkspaces(updatedWorkspaces);
  };

  const handleMoveTileToWorkspace = (tileId: string, targetDeskIndex: number) => {
    if (targetDeskIndex < 0 || targetDeskIndex >= workspaces.length) return;
    const tile = activeDesk.tiles.find((t) => t.id === tileId);
    if (!tile) return;

    const targetDesk = workspaces[targetDeskIndex];
    const free = findFirstFree(targetDesk, tile.w, tile.h);
    if (!free) {
      sound.playBack();
      notify(`Нет свободного места на столе "${targetDesk.name}" для плитки ${tile.w}×${tile.h}.`);
      return;
    }

    sound.playSelect();
    const updatedSourceTiles = activeDesk.tiles.filter((t) => t.id !== tileId);
    const updatedTargetTiles = [...targetDesk.tiles, { ...tile, x: free.x, y: free.y }];

    const updatedWorkspaces = workspaces.map((d, idx) => {
      if (idx === activeWorkspaceIndex) return { ...d, tiles: updatedSourceTiles };
      if (idx === targetDeskIndex) return { ...d, tiles: updatedTargetTiles };
      return d;
    });
    updateActiveUserWorkspaces(updatedWorkspaces);
  };

  const handleAddTileToWorkspace = (candidate: Candidate, w: number, h: number) => {
    const free = findFirstFree(activeDesk, w, h);
    if (!free) return;

    const newTile: Tile = {
      id: `tile_${Date.now()}_${Math.random().toString(36).substring(2, 6)}`,
      kind: candidate.kind,
      title: candidate.title,
      data: candidate.data,
      x: free.x,
      y: free.y,
      w,
      h,
      color: candidate.color,
      icon: candidate.icon,
      banner: candidate.banner,
    };

    handleUpdateActiveWorkspace({
      ...activeDesk,
      tiles: [...activeDesk.tiles, newTile],
    });
  };

  // Controller / Keyboard Navigation with D-pad cross-workspace seamless switching
  const handleNavigate = useCallback(
    (direction: 'up' | 'down' | 'left' | 'right') => {
      if (!activeDesk || activeDesk.tiles.length === 0) {
        // Empty desktop: allow D-pad left/right to move to adjacent screen
        if (direction === 'left' && workspaces.length > 1) {
          const prevIdx = activeWorkspaceIndex > 0 ? activeWorkspaceIndex - 1 : workspaces.length - 1;
          handleSelectWorkspace(prevIdx);
          sound.playNav();
        } else if (direction === 'right' && workspaces.length > 1) {
          const nextIdx = activeWorkspaceIndex < workspaces.length - 1 ? activeWorkspaceIndex + 1 : 0;
          handleSelectWorkspace(nextIdx);
          sound.playNav();
        }
        return;
      }

      const current = activeDesk.tiles.find((t) => t.id === focusedTileId);
      if (!current) {
        setFocusedTileId(activeDesk.tiles[0].id);
        return;
      }

      let candidates = activeDesk.tiles.filter((t) => t.id !== current.id);
      if (direction === 'right') {
        candidates = candidates.filter((t) => t.x >= current.x + current.w);
      } else if (direction === 'left') {
        candidates = candidates.filter((t) => t.x + t.w <= current.x);
      } else if (direction === 'down') {
        candidates = candidates.filter((t) => t.y >= current.y + current.h);
      } else if (direction === 'up') {
        candidates = candidates.filter((t) => t.y + t.h <= current.y);
      }

      if (candidates.length > 0) {
        candidates.sort((a, b) => {
          const distA = Math.hypot(a.x - current.x, a.y - current.y);
          const distB = Math.hypot(b.x - current.x, b.y - current.y);
          return distA - distB;
        });
        setFocusedTileId(candidates[0].id);
      } else {
        // D-pad boundary reached: switch between workspaces!
        if (direction === 'left' && workspaces.length > 1) {
          const prevIdx = activeWorkspaceIndex > 0 ? activeWorkspaceIndex - 1 : workspaces.length - 1;
          handleSelectWorkspace(prevIdx);
          sound.playNav();
          const targetDesk = workspaces[prevIdx];
          if (targetDesk && targetDesk.tiles.length > 0) {
            // Select rightmost tile on previous desk
            const sorted = [...targetDesk.tiles].sort((a, b) => (b.x + b.w) - (a.x + a.w));
            setFocusedTileId(sorted[0].id);
          }
        } else if (direction === 'right' && workspaces.length > 1) {
          const nextIdx = activeWorkspaceIndex < workspaces.length - 1 ? activeWorkspaceIndex + 1 : 0;
          handleSelectWorkspace(nextIdx);
          sound.playNav();
          const targetDesk = workspaces[nextIdx];
          if (targetDesk && targetDesk.tiles.length > 0) {
            // Select leftmost tile on next desk
            const sorted = [...targetDesk.tiles].sort((a, b) => a.x - b.x);
            setFocusedTileId(sorted[0].id);
          }
        }
      }
    },
    [activeDesk, focusedTileId, workspaces, activeWorkspaceIndex, handleSelectWorkspace]
  );

  const handleButtonA = useCallback(() => {
    if (!focusedTileId || !activeDesk) return;
    const tile = activeDesk.tiles.find((t) => t.id === focusedTileId);
    if (tile) {
      sound.playLaunch();
      setRunningCandidate({
        id: tile.id,
        kind: tile.kind,
        title: tile.title,
        data: tile.data,
        w: tile.w,
        h: tile.h,
        color: tile.color,
        ...resolvePlayStats(tile),
        icon: tile.icon || '',
        banner: tile.banner || '',
        category: tile.kind === 'Game' ? 'Game' : tile.kind === 'Nro' ? 'Homebrew' : 'Widget',
      });
    }
  }, [focusedTileId, activeDesk]);

  const handleButtonB = useCallback(() => {
    sound.playBack();
    if (isHubOpen) setIsHubOpen(false);
    else if (isDeskCustomizerOpen) setIsDeskCustomizerOpen(false);
    else if (isAlbumOpen) setIsAlbumOpen(false);
    else if (isSettingsOpen) setIsSettingsOpen(false);
    else if (isDiagnosticsOpen) setIsDiagnosticsOpen(false);
    else if (isUserProfilesOpen) setIsUserProfilesOpen(false);
    else if (isControllersOpen) setIsControllersOpen(false);
    else if (runningCandidate) setRunningCandidate(null);
  }, [isHubOpen, isDeskCustomizerOpen, isAlbumOpen, isSettingsOpen, isDiagnosticsOpen, isUserProfilesOpen, isControllersOpen, runningCandidate]);

  // Global Keyboard listener for Switch & uLauncher shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement).tagName)) {
        if (e.key === 'Escape') {
          handleButtonB();
        }
        return;
      }

      if (e.key === ' ' || e.key === '/' || e.key === 'y' || e.key === 'Y') {
        e.preventDefault();
        setIsHubOpen((prev) => !prev);
      } else if (e.key === 'x' || e.key === 'X') {
        e.preventDefault();
        setIsDeskCustomizerOpen((prev) => !prev);
      } else if (e.key === 'ArrowUp' || e.key === 'w' || e.key === 'W') {
        e.preventDefault();
        handleNavigate('up');
      } else if (e.key === 'ArrowDown' || e.key === 's' || e.key === 'S') {
        e.preventDefault();
        handleNavigate('down');
      } else if (e.key === 'ArrowLeft' || e.key === 'a' || e.key === 'A') {
        e.preventDefault();
        handleNavigate('left');
      } else if (e.key === 'ArrowRight' || e.key === 'd' || e.key === 'D') {
        e.preventDefault();
        handleNavigate('right');
      } else if (e.key === 'Enter') {
        e.preventDefault();
        handleButtonA();
      } else if (e.key === 'Escape' || e.key === 'Backspace') {
        e.preventDefault();
        handleButtonB();
      } else if (e.key === 'u' || e.key === 'U') {
        e.preventDefault();
        setIsUserProfilesOpen(true);
      } else if (e.key === 'q' || e.key === 'Q' || e.key === 'l' || e.key === 'L') {
        e.preventDefault();
        handleSelectWorkspace(activeWorkspaceIndex > 0 ? activeWorkspaceIndex - 1 : workspaces.length - 1);
      } else if (e.key === 'e' || e.key === 'E' || e.key === 'r' || e.key === 'R') {
        e.preventDefault();
        handleSelectWorkspace(activeWorkspaceIndex < workspaces.length - 1 ? activeWorkspaceIndex + 1 : 0);
      } else if (e.key === '+' || e.key === '=') {
        e.preventDefault();
        setIsSettingsOpen(true);
      } else if (e.key === '-') {
        e.preventDefault();
        setIsDiagnosticsOpen(true);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [handleNavigate, handleButtonA, handleButtonB, activeWorkspaceIndex, workspaces.length, handleSelectWorkspace]);

  // Physical Nintendo Switch Joy-Con & Pro Controller input via HTML5 Gamepad API
  useSwitchGamepad({
    onNavigate: handleNavigate,
    onButtonA: handleButtonA,
    onButtonB: handleButtonB,
    onButtonX: () => setIsDeskCustomizerOpen((prev) => !prev),
    onButtonY: () => setIsHubOpen((prev) => !prev),
    onShoulderL: () => handleSelectWorkspace(activeWorkspaceIndex > 0 ? activeWorkspaceIndex - 1 : workspaces.length - 1),
    onShoulderR: () => handleSelectWorkspace(activeWorkspaceIndex < workspaces.length - 1 ? activeWorkspaceIndex + 1 : 0),
    onButtonPlus: () => setIsSettingsOpen(true),
    onButtonMinus: () => setIsDiagnosticsOpen(true),
  });

  // Compute wallpaper style
  const effectiveBg = activeDesk?.backgroundPath || settings.backgroundPath;
  const effectiveBlur = activeDesk?.bgBlur ?? settings.bgBlur ?? 0;
  const effectiveBrightness = (activeDesk?.bgBrightness ?? settings.bgBrightness ?? 85) / 100;

  return (
    <div
      className={`relative w-screen h-screen overflow-hidden flex flex-col justify-between bg-gradient-to-br ${themeConfig.bgGradient} transition-colors duration-500`}
      style={{
        fontFamily: currentFont.css,
      }}
    >
      {/* Background Layer with blur & brightness filters */}
      {effectiveBg && (
        <div
          className="absolute inset-0 z-0 transition-all duration-700 pointer-events-none"
          style={{
            backgroundImage: `url(${effectiveBg})`,
            backgroundSize:
              settings.backgroundFit === 1
                ? 'contain'
                : settings.backgroundFit === 2
                ? '100% 100%'
                : 'cover',
            backgroundPosition: 'center',
            backgroundRepeat: 'no-repeat',
            filter: `blur(${effectiveBlur}px) brightness(${effectiveBrightness})`,
            transform: effectiveBlur > 0 ? 'scale(1.04)' : 'none',
          }}
        />
      )}

      {/* Subtle Ambient Vignette Overlay */}
      <div className="absolute inset-0 bg-radial from-transparent via-black/20 to-black/60 pointer-events-none z-1" />

      {/* Optional Scanlines Effect */}
      {settings.showScanlines && <div className="absolute inset-0 scanlines-overlay z-40" />}

      {/* Top Bar Navigation (Clean Switch User Profile, Tabs, Hold to Customize) */}
      <TopBar
        workspaces={workspaces}
        activeWorkspaceIndex={activeWorkspaceIndex}
        onSelectWorkspace={handleSelectWorkspace}
        onAddWorkspace={handleAddWorkspace}
        onRenameWorkspace={handleRenameWorkspace}
        onDeleteWorkspace={handleDeleteWorkspace}
        onOpenUserProfiles={() => setIsUserProfilesOpen(true)}
        onOpenDeskCustomizer={() => setIsDeskCustomizerOpen(true)}
        onOpenLauncherSettings={() => setIsSettingsOpen(true)}
        activeUser={activeUser}
        homeInstalled={settings.homeInstalled}
      />

      {/* Main 6x3 Desktop Grid Canvas */}
      <div className="flex-1 relative overflow-hidden flex items-center justify-center z-10">
        <DeskNXGrid
          workspace={activeDesk}
          allWorkspaces={workspaces}
          activeDeskIndex={activeWorkspaceIndex}
          focusedTileId={focusedTileId}
          onSelectTile={(tile) => setFocusedTileId(tile.id)}
          onLaunchCandidate={(candidate) => setRunningCandidate(candidate)}
          onUpdateWorkspace={handleUpdateActiveWorkspace}
          onMoveTileToWorkspace={handleMoveTileToWorkspace}
          onSelectWorkspace={handleSelectWorkspace}
          onOpenDesktopCustomizer={() => setIsDeskCustomizerOpen(true)}
          onOpenHub={() => setIsHubOpen(true)}
          onOpenSoftwareOptions={(candidate) => setSoftwareOptionsCandidate(candidate)}
          themeConfig={themeConfig}
        />
      </div>

      {/* Virtual Joy-Con Controller Overlay */}
      <JoyconOverlay
        visible={settings.showJoyconOverlay}
        onNavigate={handleNavigate}
        onButtonA={handleButtonA}
        onButtonB={handleButtonB}
        onButtonX={() => setIsDeskCustomizerOpen(true)}
        onButtonY={() => setIsHubOpen(true)}
        onShoulderL={() => handleSelectWorkspace(activeWorkspaceIndex > 0 ? activeWorkspaceIndex - 1 : workspaces.length - 1)}
        onShoulderR={() => handleSelectWorkspace(activeWorkspaceIndex < workspaces.length - 1 ? activeWorkspaceIndex + 1 : 0)}
        onButtonPlus={() => setIsSettingsOpen(true)}
        onButtonMinus={() => setIsDiagnosticsOpen(true)}
        onButtonHome={() => {
          sound.playClick();
          setIsHubOpen(false);
          setIsDeskCustomizerOpen(false);
          setIsAlbumOpen(false);
          setIsSettingsOpen(false);
          setIsDiagnosticsOpen(false);
          setIsUserProfilesOpen(false);
          setIsControllersOpen(false);
          setRunningCandidate(null);
          setSoftwareOptionsCandidate(null);
        }}
      />

      {/* Bottom Bar Tools (Каталог, Галерея, Настройки, Контроллеры) */}
      <BottomBar
        onOpenHub={() => setIsHubOpen(true)}
        onOpenAlbum={() => setIsAlbumOpen(true)}
        onOpenSettings={() => setIsSettingsOpen(true)}
        onOpenControllers={() => setIsControllersOpen(true)}
      />

      {/* Unified uLauncher Hub (Instant Catalog + Spotlight Search + Pinning + Commands) */}
      <UlauncherHub
        isOpen={isHubOpen}
        onClose={() => setIsHubOpen(false)}
        catalog={warmCatalog}
        activeWorkspace={activeDesk}
        workspaces={workspaces}
        activeWorkspaceIndex={activeWorkspaceIndex}
        onLaunchCandidate={(c) => setRunningCandidate(c)}
        onAddTileToWorkspace={handleAddTileToWorkspace}
        onSelectWorkspace={handleSelectWorkspace}
        onOpenSettings={() => setIsSettingsOpen(true)}
        onOpenDeskCustomizer={() => setIsDeskCustomizerOpen(true)}
        onOpenSoftwareOptions={(candidate) => setSoftwareOptionsCandidate(candidate)}
      />

      {/* Unified Desktop Customizer (Settings, Themes, Wallpapers, Blur, Layout, Tiles) */}
      <UnifiedDeskCustomizer
        isOpen={isDeskCustomizerOpen}
        onClose={() => setIsDeskCustomizerOpen(false)}
        workspace={activeDesk}
        workspaceIndex={activeWorkspaceIndex}
        totalWorkspaces={workspaces.length}
        onUpdateWorkspace={handleUpdateActiveWorkspace}
        onDuplicateWorkspace={handleDuplicateWorkspace}
        onReorderWorkspace={handleReorderWorkspace}
      />

      {/* Modal: Multi-User Console Profiles Switcher */}
      <UserProfileModal
        isOpen={isUserProfilesOpen}
        onClose={() => setIsUserProfilesOpen(false)}
        users={users}
        activeUserId={activeUserId}
        onSelectUser={handleSelectUser}
        onCreateUser={handleCreateUser}
        onDeleteUser={handleDeleteUser}
      />

      {/* Modal: System Settings & HOME Integration */}
      <SettingsModal
        isOpen={isSettingsOpen}
        onClose={() => setIsSettingsOpen(false)}
        settings={settings}
        onUpdateSettings={setSettings}
        workspaces={workspaces}
        activeWorkspaceIndex={activeWorkspaceIndex}
        onUpdateWorkspaces={(ws, activeIdx) => {
          updateActiveUserWorkspaces(ws, activeIdx);
        }}
      />

      {/* Modal: System Diagnostics Lab */}
      <DiagnosticsModal
        isOpen={isDiagnosticsOpen}
        onClose={() => setIsDiagnosticsOpen(false)}
        systemMode={settings.systemMode}
      />

      {/* Modal: Switch Album & Gallery */}
      <AlbumModal
        isOpen={isAlbumOpen}
        onClose={() => setIsAlbumOpen(false)}
      />

      {/* Modal: Switch Controllers Applet */}
      <ControllersModal
        isOpen={isControllersOpen}
        onClose={() => setIsControllersOpen(false)}
      />

      {/* Modal: Real Switch Software Options (+ / -) */}
      <SoftwareOptionsModal
        isOpen={softwareOptionsCandidate !== null}
        onClose={() => setSoftwareOptionsCandidate(null)}
        candidate={softwareOptionsCandidate}
        onLaunchCandidate={(c) => {
          setSoftwareOptionsCandidate(null);
          setRunningCandidate(c);
        }}
      />

      {/* Fullscreen Running App View */}
      <AppLaunchModal
        candidate={runningCandidate}
        onClose={() => setRunningCandidate(null)}
        systemMode={settings.homeInstalled}
      />

      {/* Non-blocking notifications (notify() is unusable in the WebKit applet) */}
      <Toaster />
    </div>
  );
}

export default App;
