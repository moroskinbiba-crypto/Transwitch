import React, { useState, useRef, useEffect, useCallback } from 'react';
import { 
  Candidate, 
  Tile, 
  Workspace 
} from '../types/desknx';
import { 
  COLUMNS, 
  ROWS, 
  PALETTE, 
  placeable, 
  findFirstFree,
  isSupportedTileSize 
} from '../utils/desknxLayout';
import { 
  WidgetClock, 
  WidgetBattery, 
  WidgetStorage, 
  WidgetSystemTools, 
  WidgetRecent, 
  WidgetCalendar 
} from './Widgets';
import { CATALOG_ITEMS, sound } from '../data/mockCatalog';
import { 
  MoreVertical, 
  Trash2, 
  Move, 
  Maximize2, 
  Palette, 
  Play, 
  Gamepad, 
  Terminal,
  Clock,
  Check,
  Plus,
  Sliders,
  Minus
} from 'lucide-react';

interface DeskNXGridProps {
  workspace: Workspace;
  allWorkspaces: Workspace[];
  activeDeskIndex: number;
  focusedTileId: string | null;
  onSelectTile: (tile: Tile) => void;
  onLaunchCandidate: (candidate: Candidate) => void;
  onUpdateWorkspace: (updated: Workspace) => void;
  onMoveTileToWorkspace: (tileId: string, targetDeskIndex: number) => void;
  onSelectWorkspace?: (index: number) => void;
  onOpenDesktopCustomizer?: () => void;
  onOpenHub?: () => void;
  onOpenSoftwareOptions?: (candidate: Candidate) => void;
  themeConfig: {
    cardBg: string;
    textMain: string;
    textMuted: string;
    tileSelected: string;
  };
}

interface PointerDragState {
  tile: Tile;
  pointerId: number;
  startX: number;
  startY: number;
  currentX: number;
  currentY: number;
  offsetX: number;
  offsetY: number;
  width: number;
  height: number;
  targetSlot: { x: number; y: number } | null;
  isValidDrop: boolean;
  isDragging: boolean;
}

export const DeskNXGrid: React.FC<DeskNXGridProps> = ({
  workspace,
  allWorkspaces,
  activeDeskIndex,
  focusedTileId,
  onSelectTile,
  onLaunchCandidate,
  onUpdateWorkspace,
  onMoveTileToWorkspace,
  onSelectWorkspace,
  onOpenDesktopCustomizer,
  onOpenHub,
  onOpenSoftwareOptions,
  themeConfig,
}) => {
  // iPhone iOS Springboard Edit Mode
  const [isEditMode, setIsEditMode] = useState(false);
  const [contextTile, setContextTile] = useState<Tile | null>(null);
  const [contextPos, setContextPos] = useState<{ x: number; y: number } | null>(null);
  const [hoveredCell, setHoveredCell] = useState<{ x: number; y: number } | null>(null);

  // Direct Smartphone Touch / Pointer Dragging state
  const [activePointerDrag, setActivePointerDrag] = useState<PointerDragState | null>(null);
  const pointerDragRef = useRef<PointerDragState | null>(null);
  const tileHoldTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const gridRef = useRef<HTMLDivElement>(null);

  // Seamless Workspace Sliding Transition
  const [prevDeskIndex, setPrevDeskIndex] = useState(activeDeskIndex);
  const [slideAnimClass, setSlideAnimClass] = useState('');

  useEffect(() => {
    if (activeDeskIndex !== prevDeskIndex) {
      const isForward = activeDeskIndex > prevDeskIndex;
      setSlideAnimClass(isForward ? 'animate-slide-from-right' : 'animate-slide-from-left');
      setPrevDeskIndex(activeDeskIndex);
      const timer = setTimeout(() => setSlideAnimClass(''), 330);
      return () => clearTimeout(timer);
    }
  }, [activeDeskIndex, prevDeskIndex]);

  // Long-press detection timer ref for iOS hold-to-jiggle
  const longPressTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const touchStartPosRef = useRef<{ x: number; y: number } | null>(null);
  const isDraggingGestureRef = useRef(false);

  // Edge drag cooldown for flipping pages
  const edgeFlipCooldownRef = useRef(false);

  // Close context menu
  const closeContextMenu = () => {
    setContextTile(null);
    setContextPos(null);
  };

  // Exit edit mode on Escape or controller B
  useEffect(() => {
    const handleKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && isEditMode) {
        setIsEditMode(false);
        sound.playBack();
      }
    };
    window.addEventListener('keydown', handleKey);
    return () => window.removeEventListener('keydown', handleKey);
  }, [isEditMode]);

  // Enter edit mode handler
  const enterEditMode = useCallback(() => {
    if (isEditMode) return;
    sound.playSelect();
    if (navigator.vibrate) navigator.vibrate(50);
    setIsEditMode(true);
    closeContextMenu();
  }, [isEditMode]);

  // Global Pointer tracking for smooth smartphone-style dragging
  useEffect(() => {
    const handleGlobalPointerMove = (e: PointerEvent) => {
      const cur = pointerDragRef.current;
      if (!cur || e.pointerId !== cur.pointerId) return;

      const dx = Math.abs(e.clientX - cur.startX);
      const dy = Math.abs(e.clientY - cur.startY);

      // If user moved finger > 6px in edit mode, activate drag immediately
      if (isEditMode && (dx > 6 || dy > 6) && !cur.isDragging) {
        cur.isDragging = true;
      }

      // If not yet dragging and moved too much before hold timer, cancel hold (regular swipe)
      if (!cur.isDragging) {
        if (dx > 12 || dy > 12) {
          if (tileHoldTimerRef.current) {
            clearTimeout(tileHoldTimerRef.current);
            tileHoldTimerRef.current = null;
          }
        }
        return;
      }

      // Active dragging! Compute grid target slot based on dragged center
      let targetSlot = cur.targetSlot;
      let isValidDrop = cur.isValidDrop;

      if (gridRef.current) {
        const gRect = gridRef.current.getBoundingClientRect();
        const cellW = gRect.width / COLUMNS;
        const cellH = gRect.height / ROWS;

        const tileCenterX = e.clientX - cur.offsetX + cur.width / 2;
        const tileCenterY = e.clientY - cur.offsetY + cur.height / 2;

        let col = Math.floor((tileCenterX - gRect.left) / cellW);
        let row = Math.floor((tileCenterY - gRect.top) / cellH);

        col = Math.max(0, Math.min(COLUMNS - cur.tile.w, col));
        row = Math.max(0, Math.min(ROWS - cur.tile.h, row));

        targetSlot = { x: col, y: row };
        isValidDrop = placeable(workspace, cur.tile, col, row, cur.tile.id);
      }

      // Edge flipping: If dragging close to screen edges, flip to next/previous workspace
      if (onSelectWorkspace && !edgeFlipCooldownRef.current) {
        const screenW = window.innerWidth;
        if (e.clientX < 55 && activeDeskIndex > 0) {
          edgeFlipCooldownRef.current = true;
          sound.playNav();
          onSelectWorkspace(activeDeskIndex - 1);
          setTimeout(() => { edgeFlipCooldownRef.current = false; }, 900);
        } else if (e.clientX > screenW - 55 && activeDeskIndex < allWorkspaces.length - 1) {
          edgeFlipCooldownRef.current = true;
          sound.playNav();
          onSelectWorkspace(activeDeskIndex + 1);
          setTimeout(() => { edgeFlipCooldownRef.current = false; }, 900);
        }
      }

      const updated: PointerDragState = {
        ...cur,
        currentX: e.clientX,
        currentY: e.clientY,
        targetSlot,
        isValidDrop,
      };
      pointerDragRef.current = updated;
      setActivePointerDrag(updated);
    };

    const handleGlobalPointerUp = (e: PointerEvent) => {
      if (tileHoldTimerRef.current) {
        clearTimeout(tileHoldTimerRef.current);
        tileHoldTimerRef.current = null;
      }

      const cur = pointerDragRef.current;
      if (!cur || e.pointerId !== cur.pointerId) return;

      if (cur.isDragging && cur.targetSlot) {
        const { x, y } = cur.targetSlot;
        if (cur.isValidDrop) {
          if (x !== cur.tile.x || y !== cur.tile.y) {
            const updatedTiles = workspace.tiles.map((t) =>
              t.id === cur.tile.id ? { ...t, x, y } : t
            );
            onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
            sound.playClick();
          }
        } else {
          sound.playBack();
        }
      }

      pointerDragRef.current = null;
      setActivePointerDrag(null);
    };

    window.addEventListener('pointermove', handleGlobalPointerMove, { passive: true });
    window.addEventListener('pointerup', handleGlobalPointerUp);
    window.addEventListener('pointercancel', handleGlobalPointerUp);
    return () => {
      window.removeEventListener('pointermove', handleGlobalPointerMove);
      window.removeEventListener('pointerup', handleGlobalPointerUp);
      window.removeEventListener('pointercancel', handleGlobalPointerUp);
    };
  }, [workspace, activeDeskIndex, allWorkspaces.length, onSelectWorkspace, onUpdateWorkspace, isEditMode]);

  // Pointer down on a specific tile for direct touch-dragging
  const handleTilePointerDown = (e: React.PointerEvent, tile: Tile) => {
    if ((e.target as HTMLElement).closest('button')) return;

    const tileEl = e.currentTarget as HTMLElement;
    const rect = tileEl.getBoundingClientRect();
    const offsetX = e.clientX - rect.left;
    const offsetY = e.clientY - rect.top;

    const dragInit: PointerDragState = {
      tile,
      pointerId: e.pointerId,
      startX: e.clientX,
      startY: e.clientY,
      currentX: e.clientX,
      currentY: e.clientY,
      offsetX,
      offsetY,
      width: rect.width,
      height: rect.height,
      targetSlot: { x: tile.x, y: tile.y },
      isValidDrop: true,
      isDragging: false,
    };

    pointerDragRef.current = dragInit;
    setActivePointerDrag(dragInit);

    if (tileHoldTimerRef.current) clearTimeout(tileHoldTimerRef.current);

    // Short hold (120ms if already in edit mode, 350ms if holding on tile in normal mode to pick it up)
    tileHoldTimerRef.current = setTimeout(() => {
      if (!isEditMode) {
        sound.playSelect();
        if (navigator.vibrate) navigator.vibrate(50);
        setIsEditMode(true);
      }
      if (pointerDragRef.current && pointerDragRef.current.tile.id === tile.id) {
        pointerDragRef.current.isDragging = true;
        setActivePointerDrag({ ...pointerDragRef.current, isDragging: true });
      }
    }, isEditMode ? 120 : 350);
  };

  // Long press start (mouse or touch)
  const handlePointerDown = (e: React.PointerEvent) => {
    const target = e.target as HTMLElement;
    const isInteractive = target.closest('button') || target.closest('[draggable="true"]');

    touchStartPosRef.current = { x: e.clientX, y: e.clientY };
    isDraggingGestureRef.current = false;

    // If holding on empty background area, open Desktop Customizer directly
    if (!isInteractive && onOpenDesktopCustomizer) {
      longPressTimerRef.current = setTimeout(() => {
        sound.playSelect();
        if (navigator.vibrate) navigator.vibrate(60);
        onOpenDesktopCustomizer();
      }, 550);
    } else {
      // If on a tile, trigger iOS Springboard jiggle mode
      longPressTimerRef.current = setTimeout(() => {
        enterEditMode();
      }, 550);
    }
  };

  const handlePointerMove = (e: React.PointerEvent) => {
    if (!touchStartPosRef.current) return;
    const dx = Math.abs(e.clientX - touchStartPosRef.current.x);
    const dy = Math.abs(e.clientY - touchStartPosRef.current.y);

    // If moved > 10px, cancel long-press
    if (dx > 10 || dy > 10) {
      if (longPressTimerRef.current) {
        clearTimeout(longPressTimerRef.current);
        longPressTimerRef.current = null;
      }
      isDraggingGestureRef.current = true;
    }
  };

  const handlePointerUp = (e: React.PointerEvent) => {
    if (longPressTimerRef.current) {
      clearTimeout(longPressTimerRef.current);
      longPressTimerRef.current = null;
    }

    // Horizontal Swipe Gesture across empty desktop to flip workspaces (like iPhone home screen)
    if (touchStartPosRef.current && isDraggingGestureRef.current && !isEditMode && onSelectWorkspace) {
      const diffX = e.clientX - touchStartPosRef.current.x;
      if (Math.abs(diffX) > 80) {
        if (diffX < 0 && activeDeskIndex < allWorkspaces.length - 1) {
          // Swipe Left -> Next Page
          sound.playNav();
          onSelectWorkspace(activeDeskIndex + 1);
        } else if (diffX > 0 && activeDeskIndex > 0) {
          // Swipe Right -> Previous Page
          sound.playNav();
          onSelectWorkspace(activeDeskIndex - 1);
        }
      }
    }
    touchStartPosRef.current = null;
  };

  const handleTileContextMenu = (e: React.MouseEvent, tile: Tile) => {
    e.preventDefault();
    e.stopPropagation();
    sound.playSelect();
    setContextTile(tile);
    setContextPos({ 
      x: Math.min(e.clientX, window.innerWidth - 260), 
      y: Math.min(e.clientY, window.innerHeight - 380) 
    });
  };

  const handleLaunch = (tile: Tile) => {
    if (isEditMode) return; // In edit mode, clicking doesn't launch
    sound.playLaunch();
    const found = CATALOG_ITEMS.find((c) => c.data === tile.data || c.title === tile.title);
    onLaunchCandidate(found || {
      id: tile.id,
      kind: tile.kind,
      title: tile.title,
      data: tile.data,
      author: 'Nintendo Switch',
      w: tile.w,
      h: tile.h,
      color: tile.color,
      playtimeSeconds: 3600 * 12,
      lastPlayed: Date.now(),
      launchCount: 24,
      icon: tile.icon || '',
      banner: tile.banner || '',
      category: tile.kind === 'Game' ? 'Game' : tile.kind === 'Nro' ? 'Homebrew' : 'Widget',
    });
  };

  const handleChangeTileSize = (newW: number, newH: number) => {
    if (!contextTile) return;
    if (!isSupportedTileSize(newW, newH)) return;

    if (placeable(workspace, { x: contextTile.x, y: contextTile.y, w: newW, h: newH }, contextTile.x, contextTile.y, contextTile.id)) {
      const updatedTiles = workspace.tiles.map((t) =>
        t.id === contextTile.id ? { ...t, w: newW, h: newH } : t
      );
      onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
      sound.playClick();
    } else {
      // If current slot doesn't have room to expand, find first free spot that fits
      const otherTilesWorkspace = {
        ...workspace,
        tiles: workspace.tiles.filter((t) => t.id !== contextTile.id),
      };
      const free = findFirstFree(otherTilesWorkspace, newW, newH);
      if (free) {
        const updatedTiles = workspace.tiles.map((t) =>
          t.id === contextTile.id ? { ...t, x: free.x, y: free.y, w: newW, h: newH } : t
        );
        onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
        sound.playClick();
      } else {
        sound.playBack();
        alert(`Недостаточно свободного места на этом столе для плитки ${newW}×${newH}.`);
      }
    }
    closeContextMenu();
  };

  const handleChangeTileColor = (colorIdx: number) => {
    if (!contextTile) return;
    const updatedTiles = workspace.tiles.map((t) =>
      t.id === contextTile.id ? { ...t, color: colorIdx } : t
    );
    onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
    sound.playClick();
    closeContextMenu();
  };

  const handleRemoveTile = (tileId: string) => {
    const updatedTiles = workspace.tiles.filter((t) => t.id !== tileId);
    onUpdateWorkspace({ ...workspace, tiles: updatedTiles });
    sound.playBack();
    closeContextMenu();
  };

  // Drag over empty slot
  const handleCellDragOver = (e: React.DragEvent, x: number, y: number) => {
    e.preventDefault();
    setHoveredCell({ x, y });

    // Edge flipping: If dragging close to screen edges, flip to next/previous workspace
    if (onSelectWorkspace && !edgeFlipCooldownRef.current) {
      const screenW = window.innerWidth;
      if (e.clientX < 70 && activeDeskIndex > 0) {
        edgeFlipCooldownRef.current = true;
        sound.playNav();
        onSelectWorkspace(activeDeskIndex - 1);
        setTimeout(() => { edgeFlipCooldownRef.current = false; }, 900);
      } else if (e.clientX > screenW - 70 && activeDeskIndex < allWorkspaces.length - 1) {
        edgeFlipCooldownRef.current = true;
        sound.playNav();
        onSelectWorkspace(activeDeskIndex + 1);
        setTimeout(() => { edgeFlipCooldownRef.current = false; }, 900);
      }
    }
  };

  // Drop tile on empty slot
  const handleCellDrop = (x: number, y: number) => {
    const tile = activePointerDrag?.tile;
    if (!tile) return;

    if (placeable(workspace, tile, x, y, tile.id)) {
      const updated = workspace.tiles.map((t) =>
        t.id === tile.id ? { ...t, x, y } : t
      );
      onUpdateWorkspace({ ...workspace, tiles: updated });
      sound.playClick();
    } else {
      sound.playBack();
    }
    setActivePointerDrag(null);
    pointerDragRef.current = null;
    setHoveredCell(null);
  };

  // Render individual tile content based on kind (iOS SQUIRCLE, PLAYTIME DISPLAYED, LARGE SF FONTS)
  const renderTileContent = (tile: Tile) => {
    if (tile.kind === 'Clock') return <WidgetClock tile={tile} />;
    if (tile.kind === 'Battery') return <WidgetBattery tile={tile} />;
    if (tile.kind === 'Storage') return <WidgetStorage tile={tile} />;
    if (tile.kind === 'System') return <WidgetSystemTools tile={tile} />;
    if (tile.kind === 'Recent') return <WidgetRecent tile={tile} onLaunchItem={onLaunchCandidate} />;
    if (tile.kind === 'Calendar') return <WidgetCalendar tile={tile} />;

    const matchedCatalog = CATALOG_ITEMS.find((c) => c.data === tile.data || c.title === tile.title);
    const hours = matchedCatalog ? Math.floor(matchedCatalog.playtimeSeconds / 3600) : 0;
    const playtimeText = hours > 0 ? `${hours} ч. сыграно` : 'Только начато';

    const is2x3 = tile.w === 2 && tile.h === 3;

    if (is2x3) {
      return (
        <div className="relative w-full h-full flex flex-col justify-between overflow-hidden group select-none rounded-[28px] border border-white/20">
          {tile.icon ? (
            <img
              src={tile.icon}
              alt={tile.title}
              className="absolute inset-0 w-full h-full object-cover transition-all duration-300 group-hover:scale-105 group-hover:brightness-105"
            />
          ) : (
            <div className="absolute inset-0 bg-gradient-to-br from-zinc-800 to-zinc-950 flex items-center justify-center">
              <Gamepad className="w-20 h-20 text-zinc-600" />
            </div>
          )}

          {/* Top Bar on 2x3: Vertical Switch Case Header & Options button */}
          <div className="relative z-10 p-3 flex items-center justify-between bg-gradient-to-b from-black/85 via-black/40 to-transparent">
            <span className="px-2.5 py-1 rounded-full bg-[#e60012]/90 backdrop-blur-md text-[10px] font-black uppercase tracking-wider text-white border border-red-400/40 shadow-sm flex items-center gap-1.5">
              <span className="w-1.5 h-1.5 rounded-full bg-white animate-pulse" />
              <span>2×3 Формат</span>
            </span>
            <div className="flex items-center gap-1.5">
              {onOpenSoftwareOptions && matchedCatalog && (
                <button
                  onClick={(e) => {
                    e.stopPropagation();
                    sound.playSelect();
                    onOpenSoftwareOptions(matchedCatalog);
                  }}
                  className="px-2.5 py-1 rounded-full bg-black/60 backdrop-blur-md text-white/90 hover:text-white font-extrabold text-[11px] border border-white/20 shadow-sm active:scale-95 transition-all"
                  title="Параметры программы (+ / -)"
                >
                  ＋ Опции
                </button>
              )}
              {!isEditMode && (
                <button
                  onClick={(e) => handleTileContextMenu(e, tile)}
                  className="p-1.5 rounded-full bg-black/60 backdrop-blur-md text-white/90 hover:text-white border border-white/20 shadow-sm"
                  title="Опции плитки"
                >
                  <MoreVertical className="w-3.5 h-3.5" />
                </button>
              )}
            </div>
          </div>

          {/* Bottom Bar on 2x3: Icon thumbnail, Title, semi-transparent white Playtime, and Quick Play */}
          <div className="relative z-10 p-3.5 bg-gradient-to-t from-black/95 via-black/80 to-transparent flex flex-col gap-2">
            <div className="min-w-0">
              <div className="text-sm md:text-base font-black text-white tracking-tight truncate drop-shadow-md font-sans">
                {tile.title}
              </div>
              <div className="text-xs text-white/70 font-medium flex items-center gap-2 mt-0.5">
                <span className="flex items-center gap-1 text-white/70 font-semibold drop-shadow-sm">
                  <Clock className="w-3.5 h-3.5 text-white/60" />
                  <span>{playtimeText}</span>
                </span>
                {matchedCatalog?.author && <span className="text-white/50 truncate">· {matchedCatalog.author}</span>}
              </div>
            </div>

            <button
              onClick={() => handleLaunch(tile)}
              className="w-full flex items-center justify-center gap-1.5 py-2 rounded-2xl bg-[#007aff] hover:bg-[#0062cc] text-white font-black text-xs shadow-lg active:scale-95 transition-all"
            >
              <Play className="w-3.5 h-3.5 fill-current" />
              <span>Старт</span>
            </button>
          </div>
        </div>
      );
    }

    return (
      <div className="relative w-full h-full flex flex-col justify-end overflow-hidden group select-none">
        {tile.icon ? (
          <img
            src={tile.icon}
            alt={tile.title}
            className={`absolute inset-0 w-full h-full ${
              workspace.artworkMode === 1
                ? 'object-contain'
                : workspace.artworkMode === 2
                ? 'object-fill'
                : 'object-cover'
            } transition-all duration-300 group-hover:scale-105 group-hover:brightness-105`}
          />
        ) : (
          <div className="absolute inset-0 bg-gradient-to-br from-zinc-800 to-zinc-950 flex items-center justify-center">
            {tile.kind === 'Game' ? (
              <Gamepad className="w-16 h-16 text-zinc-600" />
            ) : (
              <Terminal className="w-16 h-16 text-zinc-600" />
            )}
          </div>
        )}

        {/* Quick Options Button (outside edit mode) */}
        {!isEditMode && (
          <button
            onClick={(e) => handleTileContextMenu(e, tile)}
            className="absolute top-2 right-2 z-10 p-2 rounded-full bg-black/60 backdrop-blur-md text-white/90 hover:text-white hover:bg-black opacity-0 group-hover:opacity-100 transition-all active:scale-95 border border-white/20 shadow-lg"
            title="Опции плитки"
          >
            <MoreVertical className="w-3.5 h-3.5" />
          </button>
        )}

        {/* Minimal Bottom Gradient Overlay: Cover art is 90%+ visible! */}
        <div className="absolute inset-x-0 bottom-0 z-10 px-3 py-2.5 bg-gradient-to-t from-black/95 via-black/45 to-transparent flex flex-col justify-end pointer-events-none transition-all">
          <div className="text-xs md:text-sm font-black text-white tracking-tight truncate drop-shadow-[0_2px_4px_rgba(0,0,0,0.9)] font-sans">
            {tile.title}
          </div>
          <div className="text-[10px] md:text-xs font-semibold text-white/90 mt-0.5 flex items-center justify-between">
            <span className="flex items-center gap-1 text-white/70 font-semibold drop-shadow-sm">
              <Clock className="w-3 h-3 text-white/60" />
              <span>{playtimeText}</span>
            </span>
            <span className="opacity-0 group-hover:opacity-100 transition-opacity font-bold bg-[#007aff] text-white px-2 py-0.5 rounded-full text-[10px] shadow">
              Запуск ↵
            </span>
          </div>
        </div>
      </div>
    );
  };

  return (
    <div 
      className="relative w-full h-full p-3 md:p-6 flex flex-col items-center justify-center overflow-hidden"
      onClick={closeContextMenu}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerCancel={handlePointerUp}
    >
      {/* Authentic Apple iPhone Jiggle Mode Header Bar */}
      {isEditMode && (
        <div className="absolute top-2 z-40 w-full max-w-5xl px-4 flex items-center justify-between animate-in fade-in slide-in-from-top-3 duration-200">
          {/* iOS [+] Button to add widgets/games */}
          <button
            onClick={() => {
              sound.playSelect();
              if (onOpenHub) onOpenHub();
            }}
            className="flex items-center gap-1.5 px-4 py-2 rounded-full bg-white/15 hover:bg-white/25 backdrop-blur-3xl text-white font-bold text-sm border border-white/20 shadow-lg active:scale-95 transition-all"
            title="Добавить игру или виджет"
          >
            <Plus className="w-4 h-4 stroke-[3]" />
            <span>Добавить</span>
          </button>

          {/* iOS Page Scrubbing Dots */}
          <div className="flex items-center gap-1.5 px-4 py-2 bg-black/50 backdrop-blur-3xl rounded-full border border-white/15 shadow-lg">
            {allWorkspaces.map((_, idx) => (
              <button
                key={idx}
                onClick={() => onSelectWorkspace && onSelectWorkspace(idx)}
                className={`h-2 rounded-full transition-all ${
                  idx === activeDeskIndex 
                    ? 'w-6 bg-white shadow-[0_0_10px_rgba(255,255,255,0.8)]' 
                    : 'w-2 bg-white/30 hover:bg-white/60'
                }`}
                title={`Страница ${idx + 1}`}
              />
            ))}
          </div>

          {/* Right: Desk Settings & Done Button */}
          <div className="flex items-center gap-2">
            {onOpenDesktopCustomizer && (
              <button
                onClick={() => {
                  sound.playClick();
                  onOpenDesktopCustomizer();
                }}
                className="flex items-center gap-1.5 px-3 py-2 rounded-full bg-white/15 hover:bg-white/25 backdrop-blur-3xl text-xs font-bold text-white transition-all border border-white/20 shadow-lg"
              >
                <Sliders className="w-3.5 h-3.5 text-amber-400" />
                <span className="hidden sm:inline">Обои</span>
              </button>
            )}

            {/* Apple iOS [Готово] Blue Capsule Button */}
            <button
              onClick={() => {
                sound.playSelect();
                setIsEditMode(false);
              }}
              className="flex items-center gap-1.5 px-5 py-2 rounded-full bg-[#007aff] hover:bg-[#0062cc] text-white font-extrabold text-sm active:scale-95 transition-all shadow-[0_8px_25px_rgba(0,122,255,0.5)] border-t border-white/30"
            >
              <Check className="w-4 h-4 stroke-[3]" />
              <span>Готово</span>
            </button>
          </div>
        </div>
      )}

      {/* 6x3 Grid Canvas with Apple Continuous Curvature */}
      <div 
        ref={gridRef}
        key={workspace.id}
        className={`w-full max-w-7xl h-full max-h-[680px] grid gap-4 relative transition-all duration-300 ${slideAnimClass}`}
        style={{
          gridTemplateColumns: `repeat(${COLUMNS}, minmax(0, 1fr))`,
          gridTemplateRows: `repeat(${ROWS}, minmax(0, 1fr))`,
        }}
      >
        {/* Drop Target Preview Slot when Dragging */}
        {activePointerDrag?.isDragging && activePointerDrag.targetSlot && (
          <div
            className={`rounded-[28px] border-2 border-dashed flex items-center justify-center font-bold text-xs pointer-events-none transition-all duration-100 z-20 ${
              activePointerDrag.isValidDrop
                ? 'border-[#007aff] bg-[#007aff]/25 text-white shadow-[0_0_35px_rgba(0,122,255,0.6)]'
                : 'border-rose-500 bg-rose-500/25 text-rose-200 shadow-[0_0_25px_rgba(244,63,94,0.4)]'
            }`}
            style={{
              gridColumnStart: activePointerDrag.targetSlot.x + 1,
              gridColumnEnd: `span ${activePointerDrag.tile.w}`,
              gridRowStart: activePointerDrag.targetSlot.y + 1,
              gridRowEnd: `span ${activePointerDrag.tile.h}`,
            }}
          >
            <div className="flex items-center gap-1.5 px-3 py-1.5 rounded-full bg-black/70 backdrop-blur-md shadow-md">
              {activePointerDrag.isValidDrop ? (
                <>
                  <Check className="w-4 h-4 text-emerald-400 stroke-[3]" />
                  <span>Переместить</span>
                </>
              ) : (
                <>
                  <Minus className="w-4 h-4 text-rose-400 stroke-[3]" />
                  <span>Занято</span>
                </>
              )}
            </div>
          </div>
        )}

        {/* 
          USER REQUIREMENT:
          "вне режима редактирования чтобы не было видно слоты незанятые приложениями."
          -> Empty slots rendered ONLY when isEditMode === true!
        */}
        {isEditMode && !activePointerDrag?.isDragging &&
          Array.from({ length: ROWS }).map((_, r) =>
            Array.from({ length: COLUMNS }).map((_, c) => {
              const isHovered = hoveredCell?.x === c && hoveredCell?.y === r;
              return (
                <div
                  key={`empty-cell-${c}-${r}`}
                  onDragOver={(e) => handleCellDragOver(e, c, r)}
                  onDrop={() => handleCellDrop(c, r)}
                  className={`rounded-[28px] border-2 border-dashed transition-all duration-200 flex flex-col items-center justify-center relative select-none ${
                    isHovered
                      ? 'border-[#007aff] bg-[#007aff]/20 scale-98 shadow-[inset_0_0_30px_rgba(0,122,255,0.4)]'
                      : 'border-white/15 bg-black/20 hover:border-[#007aff]/40 hover:bg-[#007aff]/5'
                  }`}
                  style={{
                    gridColumnStart: c + 1,
                    gridRowStart: r + 1,
                  }}
                >
                  <Plus className={`w-5 h-5 transition-all ${
                    isHovered ? 'text-[#007aff] scale-125' : 'text-white/20'
                  }`} />
                  <span className="text-[10px] font-mono text-zinc-500 mt-0.5">
                    {c + 1}:{r + 1}
                  </span>
                </div>
              );
            })
          )}

        {/* Render Active Tiles with Apple Springboard Squircles */}
        {workspace.tiles.map((tile, tileIdx) => {
          const isFocused = focusedTileId === tile.id;
          const colorDef = PALETTE[tile.color] || PALETTE[0];
          const hasCustomAccent = workspace.accentColor >= 0;
          const deskAccent = hasCustomAccent ? PALETTE[workspace.accentColor] : colorDef;

          const isBeingDragged = activePointerDrag?.tile.id === tile.id && activePointerDrag.isDragging;

          // Apple Squircle & Frosted Glass material
          const cardStyleClass = workspace.cardStyle === 'matte'
            ? 'bg-zinc-900 border-zinc-700/60 shadow-xl'
            : workspace.cardStyle === 'border'
            ? 'bg-black/60 border-2 border-[#007aff]/40 shadow-[0_0_20px_rgba(0,122,255,0.3)]'
            : workspace.cardStyle === 'cyber'
            ? 'bg-purple-950/40 border border-purple-500/40 shadow-[0_0_25px_rgba(168,85,247,0.35)]'
            : 'bg-zinc-900/80 backdrop-blur-3xl border border-white/20 border-t-white/35 shadow-[0_15px_35px_rgba(0,0,0,0.55),inset_0_1px_0_rgba(255,255,255,0.2)]';

          // Apple Springboard Wiggle class in edit mode
          const jiggleClass = isEditMode && !isBeingDragged
            ? tileIdx % 2 === 0
              ? 'tile-jiggle cursor-grab active:cursor-grabbing'
              : 'tile-jiggle-alt cursor-grab active:cursor-grabbing'
            : '';

          return (
            <div
              key={tile.id}
              data-tile-id={tile.id}
              onPointerDown={(e) => handleTilePointerDown(e, tile)}
              onClick={(e) => {
                e.stopPropagation();
                if (isEditMode) {
                  onSelectTile(tile);
                  sound.playClick();
                } else {
                  sound.playNav();
                  onSelectTile(tile);
                  handleLaunch(tile);
                }
              }}
              onContextMenu={(e) => handleTileContextMenu(e, tile)}
              className={`relative rounded-[28px] overflow-hidden select-none transition-all duration-200 touch-none ${cardStyleClass} ${jiggleClass} ${
                isBeingDragged
                  ? 'opacity-20 scale-95 pointer-events-none'
                  : isFocused && !isEditMode
                  ? 'switch-focused ring-4 ring-[#007aff] shadow-[0_0_40px_rgba(0,122,255,0.8)] scale-[1.02] z-20'
                  : 'hover:shadow-2xl hover:border-white/40'
              }`}
              style={{
                gridColumnStart: tile.x + 1,
                gridColumnEnd: `span ${tile.w}`,
                gridRowStart: tile.y + 1,
                gridRowEnd: `span ${tile.h}`,
                borderColor: isFocused ? deskAccent.border : undefined,
              }}
            >
              {/* Authentic Apple iOS [-] Delete Badge in Edit Mode */}
              {isEditMode && (
                <button
                  onClick={(e) => {
                    e.stopPropagation();
                    handleRemoveTile(tile.id);
                  }}
                  className="absolute top-2 left-2 z-30 w-7 h-7 rounded-full bg-[#48484a] hover:bg-[#ff3b30] text-white flex items-center justify-center shadow-lg border-2 border-white transition-all active:scale-90 group"
                  title="Удалить со страницы"
                >
                  <Minus className="w-4 h-4 stroke-[3]" />
                </button>
              )}

              {/* In Edit mode: Drag Handle Indicator at top right */}
              {isEditMode && (
                <div className="absolute top-2 right-2 z-30 p-1.5 rounded-full bg-black/60 backdrop-blur-md text-white/80 border border-white/20 shadow pointer-events-none">
                  <Move className="w-3.5 h-3.5" />
                </div>
              )}

              {renderTileContent(tile)}
            </div>
          );
        })}
      </div>

      {/* Direct Floating Drag Clone Following Finger/Pointer (Smartphone / iOS Style) */}
      {activePointerDrag && activePointerDrag.isDragging && (
        <div
          className="fixed z-50 pointer-events-none select-none rounded-[28px] overflow-hidden ring-4 ring-[#007aff] shadow-[0_30px_90px_rgba(0,0,0,0.9)] bg-zinc-900/90 backdrop-blur-3xl"
          style={{
            left: activePointerDrag.currentX - activePointerDrag.offsetX,
            top: activePointerDrag.currentY - activePointerDrag.offsetY,
            width: activePointerDrag.width,
            height: activePointerDrag.height,
            transform: 'scale(1.06) rotate(1.5deg)',
            transition: 'transform 0.05s ease-out',
          }}
        >
          {renderTileContent(activePointerDrag.tile)}
        </div>
      )}

      {/* Floating Context Menu (Styled like iOS Action Sheet) */}
      {contextTile && contextPos && (
        <div
          className="fixed z-50 w-72 rounded-[28px] bg-[#1a1a24]/95 backdrop-blur-3xl border border-white/20 shadow-[0_30px_70px_rgba(0,0,0,0.95)] p-4 text-sm text-zinc-100 select-none animate-in fade-in zoom-in-95 duration-150"
          style={{ top: contextPos.y, left: contextPos.x }}
          onClick={(e) => e.stopPropagation()}
        >
          <div className="px-2 py-2 border-b border-white/10 mb-3">
            <div className="font-extrabold text-base truncate text-white">
              {contextTile.title}
            </div>
            <div className="text-xs text-zinc-400 font-medium">
              Размер: {contextTile.w}×{contextTile.h}
            </div>
          </div>

          {/* Launch Button */}
          <button
            onClick={() => {
              handleLaunch(contextTile);
              closeContextMenu();
            }}
            className="w-full flex items-center justify-center gap-2 px-4 py-3 rounded-2xl bg-[#007aff] hover:bg-[#0062cc] text-sm font-bold text-white transition-all mb-2 shadow-md"
          >
            <Play className="w-4 h-4 fill-current" /> Запустить игру
          </button>

          {/* Software Options (+ / -) */}
          {onOpenSoftwareOptions && (
            <button
              onClick={() => {
                const found = CATALOG_ITEMS.find((c) => c.data === contextTile.data || c.title === contextTile.title);
                if (found) {
                  sound.playSelect();
                  onOpenSoftwareOptions(found);
                }
                closeContextMenu();
              }}
              className="w-full flex items-center justify-center gap-2 px-4 py-2.5 rounded-2xl bg-white/10 hover:bg-white/15 text-xs font-bold text-zinc-100 transition-all mb-2 border border-white/15"
            >
              <span className="font-mono text-xs bg-white/20 px-1 rounded font-bold">＋</span> Параметры программы
            </button>
          )}

          {/* Enter Edit Mode */}
          <button
            onClick={() => {
              enterEditMode();
              closeContextMenu();
            }}
            className="w-full flex items-center justify-center gap-2 px-4 py-2.5 rounded-2xl bg-white/10 hover:bg-white/15 text-xs font-bold text-zinc-200 transition-all mb-3 border border-white/10"
          >
            <Move className="w-4 h-4 text-amber-400" /> Настроить экран (Wiggle)
          </button>

          {/* Size Changer */}
          <div className="px-1 pt-1 pb-1.5 text-xs font-bold text-zinc-300 flex items-center gap-1.5">
            <Maximize2 className="w-4 h-4 text-blue-400" /> Размер виджета:
          </div>
          <div className="grid grid-cols-4 gap-2 mb-3">
            {[
              { w: 1, h: 1, label: '1×1' },
              { w: 2, h: 1, label: '2×1' },
              { w: 2, h: 2, label: '2×2' },
              { w: 2, h: 3, label: '2×3' },
            ].map((size) => (
              <button
                key={`${size.w}x${size.h}`}
                onClick={() => handleChangeTileSize(size.w, size.h)}
                className={`py-2 rounded-xl text-xs font-mono font-extrabold border transition-all ${
                  contextTile.w === size.w && contextTile.h === size.h
                    ? 'bg-[#007aff] text-white border-blue-400 shadow-md'
                    : 'bg-black/40 text-zinc-300 hover:bg-white/10 border-white/10'
                }`}
              >
                {size.label}
              </button>
            ))}
          </div>

          {/* Accent Color Palette */}
          <div className="px-1 pt-1 pb-1.5 text-xs font-bold text-zinc-300 flex items-center gap-1.5">
            <Palette className="w-4 h-4 text-blue-400" /> Цвет рамки:
          </div>
          <div className="flex gap-2 px-1 mb-3">
            {PALETTE.map((pal, idx) => (
              <button
                key={idx}
                onClick={() => handleChangeTileColor(idx)}
                className={`w-7 h-7 rounded-full transition-transform ${
                  contextTile.color === idx ? 'scale-125 ring-2 ring-white shadow-lg' : 'hover:scale-110'
                }`}
                style={{ backgroundColor: pal.hex }}
                title={pal.name}
              />
            ))}
          </div>

          {/* Move to another workspace */}
          {allWorkspaces.length > 1 && (
            <div className="border-t border-white/10 pt-2 mb-2">
              <div className="px-1 py-1 text-xs font-bold text-zinc-400 flex items-center gap-1">
                <Move className="w-3.5 h-3.5 text-blue-400" /> Перенести на страницу:
              </div>
              <div className="max-h-28 overflow-y-auto space-y-1 scrollbar-thin">
                {allWorkspaces.map((desk, idx) => {
                  if (idx === activeDeskIndex) return null;
                  return (
                    <button
                      key={desk.id}
                      onClick={() => {
                        onMoveTileToWorkspace(contextTile.id, idx);
                        closeContextMenu();
                      }}
                      className="w-full text-left px-3 py-2 rounded-xl text-xs font-bold hover:bg-white/10 truncate text-zinc-300 flex items-center justify-between"
                    >
                      <span>{desk.name}</span>
                      <span className="text-[10px] text-zinc-500 font-mono">#{idx + 1}</span>
                    </button>
                  );
                })}
              </div>
            </div>
          )}

          {/* Remove */}
          <div className="border-t border-white/10 pt-2">
            <button
              onClick={() => handleRemoveTile(contextTile.id)}
              className="w-full flex items-center justify-center gap-2 px-3 py-2.5 rounded-2xl hover:bg-red-500/25 text-xs text-[#ff3b30] font-bold transition-all border border-red-500/30"
            >
              <Trash2 className="w-4 h-4" /> Удалить с экрана
            </button>
          </div>
        </div>
      )}
    </div>
  );
};
