import { Kind, Tile, Workspace } from '../types/desknx';

export const COLUMNS = 6;
export const ROWS = 3;
export const MAX_DESKS = 12;

export const PALETTE = [
  { name: 'Switch Red', hex: '#e60012', border: '#ff3344', text: '#ffffff' },
  { name: 'Neon Blue', hex: '#00b4e6', border: '#33ccff', text: '#ffffff' },
  { name: 'Emerald', hex: '#10b981', border: '#34d399', text: '#ffffff' },
  { name: 'Amber Gold', hex: '#f59e0b', border: '#fbbf24', text: '#000000' },
  { name: 'Deep Purple', hex: '#8b5cf6', border: '#a78bfa', text: '#ffffff' },
  { name: 'Onyx Dark', hex: '#27272a', border: '#52525b', text: '#ffffff' },
  { name: 'Cyan Glow', hex: '#06b6d4', border: '#22d3ee', text: '#000000' },
  { name: 'Rose Pink', hex: '#ec4899', border: '#f472b6', text: '#ffffff' },
  { name: 'Electric Violet', hex: '#a855f7', border: '#c084fc', text: '#ffffff' },
  { name: 'Lime', hex: '#84cc16', border: '#a3e635', text: '#10140a' },
  { name: 'Arctic', hex: '#38bdf8', border: '#7dd3fc', text: '#07131d' },
  { name: 'Coral', hex: '#fb7185', border: '#fda4af', text: '#24090e' },
];

/**
 * The single source of truth for tile shapes, mirrored from the native model
 * (`include/desknx/Model.hpp::isSupportedTileSize`). The two implementations
 * must accept exactly the same set: a shape accepted by only one side is
 * silently rewritten on load and corrupts layout round-trips.
 */
export const TILE_SHAPES = [
  { w: 1, h: 1 },
  { w: 2, h: 1 },
  { w: 3, h: 1 },
  { w: 2, h: 2 },
  { w: 3, h: 2 },
] as const;

export const MAX_TILE_W = 3;
export const MAX_TILE_H = 2;

export function isSupportedTileSize(w: number, h: number): boolean {
  return TILE_SHAPES.some((shape) => shape.w === w && shape.h === h);
}

/**
 * Mirrors the native parser's recovery step: clamp to the grid maxima, then
 * snap to the closest supported shape instead of destroying the tile.
 */
export function nearestSupportedTileSize(w: number, h: number): { w: number; h: number } {
  const clampedW = Math.max(1, Math.min(MAX_TILE_W, Number.isFinite(w) ? Math.trunc(w) : 1));
  const clampedH = Math.max(1, Math.min(MAX_TILE_H, Number.isFinite(h) ? Math.trunc(h) : 1));

  if (isSupportedTileSize(clampedW, clampedH)) {
    return { w: clampedW, h: clampedH };
  }

  let best: { w: number; h: number } = TILE_SHAPES[0];
  let bestScore = Number.POSITIVE_INFINITY;

  for (const shape of TILE_SHAPES) {
    const distance = Math.abs(shape.w - clampedW) + Math.abs(shape.h - clampedH);
    const area = shape.w * shape.h;
    const bestArea = best.w * best.h;

    // Prefer the closest shape; break ties towards the larger area so a wide
    // widget is not demoted to a single cell.
    if (distance < bestScore || (distance === bestScore && area > bestArea)) {
      best = shape;
      bestScore = distance;
    }
  }

  return { w: best.w, h: best.h };
}

/** Deep-copies workspaces so callers never share tile arrays with defaults. */
export function cloneWorkspaces(workspaces: Workspace[]): Workspace[] {
  return workspaces.map((desk) => ({ ...desk, tiles: desk.tiles.map((tile) => ({ ...tile })) }));
}

export function kindToCode(kind: Kind): string {
  switch (kind) {
    case 'Game': return 'G';
    case 'Nro': return 'N';
    case 'Clock': return 'C';
    case 'Battery': return 'B';
    case 'System': return 'S';
    case 'Recent': return 'R';
    case 'Storage': return 'V';
    case 'Calendar': return 'A';
    default: return 'U';
  }
}

export function codeToKind(code: string): Kind {
  switch (code) {
    case 'G': return 'Game';
    case 'N': return 'Nro';
    case 'C': return 'Clock';
    case 'B': return 'Battery';
    case 'S': return 'System';
    case 'R': return 'Recent';
    case 'V': return 'Storage';
    case 'A': return 'Calendar';
    default: return 'Unsupported';
  }
}

export function overlap(a: { x: number; y: number; w: number; h: number }, b: { x: number; y: number; w: number; h: number }): boolean {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

export function placeable(
  desk: Workspace,
  tile: { x: number; y: number; w: number; h: number; id?: string },
  targetX: number,
  targetY: number,
  ignoreTileId?: string
): boolean {
  if (
    targetX < 0 ||
    targetY < 0 ||
    targetX >= COLUMNS ||
    targetY >= ROWS ||
    !isSupportedTileSize(tile.w, tile.h) ||
    targetX + tile.w > COLUMNS ||
    targetY + tile.h > ROWS
  ) {
    return false;
  }

  const candidate = { x: targetX, y: targetY, w: tile.w, h: tile.h };
  for (const existing of desk.tiles) {
    if (ignoreTileId && existing.id === ignoreTileId) continue;
    if (overlap(candidate, existing)) return false;
  }
  return true;
}

export function findFirstFree(
  desk: Workspace,
  w: number,
  h: number,
  ignoreTileId?: string
): { x: number; y: number } | null {
  for (let y = 0; y < ROWS; y++) {
    for (let x = 0; x < COLUMNS; x++) {
      if (placeable(desk, { x, y, w, h }, x, y, ignoreTileId)) {
        return { x, y };
      }
    }
  }
  return null;
}

/**
 * Serializes workspaces into exact DESKNX4 layout.tsv format
 */
export function serializeLayout(workspaces: Workspace[], activeIndex: number): string {
  const lines: string[] = [];
  lines.push('DESKNX4');
  lines.push(`A\t${Math.max(0, Math.min(activeIndex, workspaces.length - 1))}`);

  for (const desk of workspaces) {
    const safeName = desk.name.replace(/[\t\n\r]/g, ' ');
    const safeBg = (desk.backgroundPath || '').replace(/[\t\n\r]/g, ' ');
    // Keep the file parseable by the native reader: no NaN, no out-of-range
    // palette index, no undefined label opacity.
    const labelOpacity = clampInt(desk.labelOpacity, 0, 255, 196);
    const accentColor = clampInt(desk.accentColor, -1, 11, -1);
    lines.push(
      `D\t${safeName}\t${safeBg}\t${clampInt(desk.backgroundFit, 0, 2, 0)}\t${clampInt(desk.artworkMode, 0, 2, 0)}\t${labelOpacity}\t${accentColor}`
    );
  }

  for (let d = 0; d < workspaces.length; d++) {
    const desk = workspaces[d];
    for (const t of desk.tiles) {
      const code = t.unknownTypeCode || kindToCode(t.kind);
      const safeTitle = (t.title ?? '').replace(/[\t\n\r]/g, ' ');
      const safeData = (t.data ?? '').replace(/[\t\n\r]/g, ' ');
      // Serializing an unsupported shape would be rewritten on the next parse.
      const shape = nearestSupportedTileSize(t.w, t.h);
      lines.push(
        `I\t${d}\t${code}\t${safeTitle}\t${safeData}\t${clampInt(t.x, 0, COLUMNS - 1, 0)}\t${clampInt(t.y, 0, ROWS - 1, 0)}\t${shape.w}\t${shape.h}\t${clampInt(t.color, 0, 11, 0)}`
      );
    }
  }

  return lines.join('\n');
}

/**
 * Parses layout.tsv according to DESKNX1-DESKNX4 specification
 */
export function parseLayout(
  tsvContent: string,
  catalogResolver?: (data: string, kind: Kind) => { icon?: string; banner?: string }
): { workspaces: Workspace[]; activeIndex: number } | null {
  const lines = tsvContent.split(/\r?\n/).filter((l) => l.trim().length > 0);
  if (lines.length === 0) return null;

  const header = lines[0].trim();
  if (!['DESKNX1', 'DESKNX2', 'DESKNX3', 'DESKNX4'].includes(header)) {
    return null;
  }

  const hasWorkspaceAppearance = header === 'DESKNX4';
  const hasActiveField = header !== 'DESKNX1';

  let activeIndex = 0;
  const workspaces: Workspace[] = [];

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i];
    const fields = line.split('\t');
    const tag = fields[0];

    if (tag === 'A' && hasActiveField) {
      activeIndex = parseInt(fields[1], 10) || 0;
    } else if (tag === 'D') {
      const name = fields[1] || `Desk ${workspaces.length + 1}`;
      let bgPath = '';
      let bgFit = 0;
      let artMode = 0;
      let labelOp = 196;
      let accent = -1;

      if (hasWorkspaceAppearance && fields.length >= 7) {
        bgPath = fields[2] || '';
        bgFit = clampInt(parseInt(fields[3], 10), 0, 2, 0);
        artMode = clampInt(parseInt(fields[4], 10), 0, 2, 0);
        labelOp = clampInt(parseInt(fields[5], 10), 0, 255, 196);
        // `parseInt(...) ?? -1` let NaN through: NaN is not nullish, and a NaN
        // accent colour poisons the serializer on the next save.
        accent = clampInt(parseInt(fields[6], 10), -1, 11, -1);
      }

      workspaces.push({
        id: `desk_${Date.now()}_${workspaces.length}`,
        name,
        tiles: [],
        backgroundPath: bgPath,
        backgroundFit: bgFit,
        artworkMode: artMode,
        labelOpacity: labelOp,
        accentColor: accent,
      });
    } else if (tag === 'I') {
      const deskIdx = parseInt(fields[1], 10);
      if (isNaN(deskIdx) || deskIdx < 0 || deskIdx >= workspaces.length) continue;

      const code = fields[2];
      const kind = codeToKind(code);
      const title = fields[3] || 'Untitled';
      const data = fields[4] || '';
      let x = parseInt(fields[5], 10) || 0;
      let y = parseInt(fields[6], 10) || 0;
      let w = parseInt(fields[7], 10) || 1;
      let h = parseInt(fields[8], 10) || 1;
      const color = parseInt(fields[9], 10) || 0;

      // Snap to a supported shape exactly like the native reader instead of
      // collapsing unknown sizes into a single cell.
      const shape = nearestSupportedTileSize(w, h);
      w = shape.w;
      h = shape.h;

      x = clampInt(x, 0, COLUMNS - 1, 0);
      y = clampInt(y, 0, ROWS - 1, 0);

      const desk = workspaces[deskIdx];
      // Collision check
      if (!placeable(desk, { x, y, w, h }, x, y)) {
        const free = findFirstFree(desk, w, h);
        if (free) {
          x = free.x;
          y = free.y;
        } else {
          continue; // Cannot fit
        }
      }

      const media = catalogResolver ? catalogResolver(data, kind) : {};

      desk.tiles.push({
        id: `tile_${Date.now()}_${Math.random().toString(36).substring(2, 7)}`,
        kind,
        title,
        data,
        x,
        y,
        w,
        h,
        color,
        unknownTypeCode: kind === 'Unsupported' ? code : undefined,
        icon: media.icon,
        banner: media.banner,
      });
    }
  }

  if (workspaces.length === 0) return null;
  activeIndex = Math.max(0, Math.min(activeIndex, workspaces.length - 1));

  return { workspaces, activeIndex };
}

/** Clamps a possibly-NaN integer into [min, max] with a fallback. */
export function clampInt(value: number, min: number, max: number, fallback: number): number {
  if (!Number.isFinite(value)) return fallback;
  return Math.max(min, Math.min(max, Math.trunc(value)));
}
