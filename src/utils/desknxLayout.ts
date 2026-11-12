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
    const accentColor = clampInt(desk.accentColor, -1, 7, -1);
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
        `I\t${d}\t${code}\t${safeTitle}\t${safeData}\t${clampInt(t.x, 0, COLUMNS - 1, 0)}\t${clampInt(t.y, 0, ROWS - 1, 0)}\t${shape.w}\t${shape.h}\t${clampInt(t.color, 0, 7, 0)}`
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
        accent = clampInt(parseInt(fields[6], 10), -1, 7, -1);
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

{"content":"import { Kind, Tile, Workspace } from '../types/desknx';\r\n\r\nexport const COLUMNS = 6;\r\nexport const ROWS = 3;\r\nexport const MAX_DESKS = 12;\r\n\r\nexport const PALETTE = [\r\n  { name: 'Switch Red', hex: '#e60012', border: '#ff3344', text: '#ffffff' },\r\n  { name: 'Neon Blue', hex: '#00b4e6', border: '#33ccff', text: '#ffffff' },\r\n  { name: 'Emerald', hex: '#10b981', border: '#34d399', text: '#ffffff' },\r\n  { name: 'Amber Gold', hex: '#f59e0b', border: '#fbbf24', text: '#000000' },\r\n  { name: 'Deep Purple', hex: '#8b5cf6', border: '#a78bfa', text: '#ffffff' },\r\n  { name: 'Onyx Dark', hex: '#27272a', border: '#52525b', text: '#ffffff' },\r\n  { name: 'Cyan Glow', hex: '#06b6d4', border: '#22d3ee', text: '#000000' },\r\n  { name: 'Rose Pink', hex: '#ec4899', border: '#f472b6', text: '#ffffff' },\r\n];\r\n\r\n/**\r\n * The single source of truth for tile shapes, mirrored from the native model\r\n * (`include/desknx/Model.hpp::isSupportedTileSize`). The two implementations\r\n * must accept exactly the same set: a shape accepted by only one side is\r\n * silently rewritten on load and corrupts layout round-trips.\r\n */\r\nexport const TILE_SHAPES = [\r\n  { w: 1, h: 1 },\r\n  { w: 2, h: 1 },\r\n  { w: 3, h: 1 },\r\n  { w: 2, h: 2 },\r\n  { w: 3, h: 2 },\r\n] as const;\r\n\r\nexport const MAX_TILE_W = 3;\r\nexport const MAX_TILE_H = 2;\r\n\r\nexport function isSupportedTileSize(w: number, h: number): boolean {\r\n  return TILE_SHAPES.some((shape) => shape.w === w && shape.h === h);\r\n}\r\n\r\n/**\r\n * Mirrors the native parser's recovery step: clamp to the grid maxima, then\r\n * snap to the closest supported shape instead of destroying the tile.\r\n */\r\nexport function nearestSupportedTileSize(w: number, h: number): { w: number; h: number } {\r\n  const clampedW = Math.max(1, Math.min(MAX_TILE_W, Number.isFinite(w) ? Math.trunc(w) : 1));\r\n  const clampedH = Math.max(1, Math.min(MAX_TILE_H, Number.isFinite(h) ? Math.trunc(h) : 1));\r\n\r\n  if (isSupportedTileSize(clampedW, clampedH)) {\r\n    return { w: clampedW, h: clampedH };\r\n  }\r\n\r\n  let best: { w: number; h: number } = TILE_SHAPES[0];\r\n  let bestScore = Number.POSITIVE_INFINITY;\r\n\r\n  for (const shape of TILE_SHAPES) {\r\n    const distance = Math.abs(shape.w - clampedW) + Math.abs(shape.h - clampedH);\r\n    const area = shape.w * shape.h;\r\n    const bestArea = best.w * best.h;\r\n\r\n    // Prefer the closest shape; break ties towards the larger area so a wide\r\n    // widget is not demoted to a single cell.\r\n    if (distance < bestScore || (distance === bestScore && area > bestArea)) {\r\n      best = shape;\r\n      bestScore = distance;\r\n    }\r\n  }\r\n\r\n  return { w: best.w, h: best.h };\r\n}\r\n\r\n/** Deep-copies workspaces so callers never share tile arrays with defaults. */\r\nexport function cloneWorkspaces(workspaces: Workspace[]): Workspace[] {\r\n  return workspaces.map((desk) => ({ ...desk, tiles: desk.tiles.map((tile) => ({ ...tile })) }));\r\n}\r\n\r\nexport function kindToCode(kind: Kind): string {\r\n  switch (kind) {\r\n    case 'Game': return 'G';\r\n    case 'Nro': return 'N';\r\n    case 'Clock': return 'C';\r\n    case 'Battery': return 'B';\r\n    case 'System': return 'S';\r\n    case 'Recent': return 'R';\r\n    case 'Storage': return 'V';\r\n    case 'Calendar': return 'A';\r\n    default: return 'U';\r\n  }\r\n}\r\n\r\nexport function codeToKind(code: string): Kind {\r\n  switch (code) {\r\n    case 'G': return 'Game';\r\n    case 'N': return 'Nro';\r\n    case 'C': return 'Clock';\r\n    case 'B': return 'Battery';\r\n    case 'S': return 'System';\r\n    case 'R': return 'Recent';\r\n    case 'V': return 'Storage';\r\n    case 'A': return 'Calendar';\r\n    default: return 'Unsupported';\r\n  }\r\n}\r\n\r\nexport function overlap(a: { x: number; y: number; w: number; h: number }, b: { x: number; y: number; w: number; h: number }): boolean {\r\n  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;\r\n}\r\n\r\nexport function placeable(\r\n  desk: Workspace,\r\n  tile: { x: number; y: number; w: number; h: number; id?: string },\r\n  targetX: number,\r\n  targetY: number,\r\n  ignoreTileId?: string\r\n): boolean {\r\n  if (\r\n    targetX < 0 ||\r\n    targetY < 0 ||\r\n    targetX >= COLUMNS ||\r\n    targetY >= ROWS ||\r\n    !isSupportedTileSize(tile.w, tile.h) ||\r\n    targetX + tile.w > COLUMNS ||\r\n    targetY + tile.h > ROWS\r\n  ) {\r\n    return false;\r\n  }\r\n\r\n  const candidate = { x: targetX, y: targetY, w: tile.w, h: tile.h };\r\n  for (const existing of desk.tiles) {\r\n    if (ignoreTileId && existing.id === ignoreTileId) continue;\r\n    if (overlap(candidate, existing)) return false;\r\n  }\r\n  return true;\r\n}\r\n\r\nexport function findFirstFree(\r\n  desk: Workspace,\r\n  w: number,\r\n  h: number,\r\n  ignoreTileId?: string\r\n): { x: number; y: number } | null {\r\n  for (let y = 0; y < ROWS; y++) {\r\n    for (let x = 0; x < COLUMNS; x++) {\r\n      if (placeable(desk, { x, y, w, h }, x, y, ignoreTileId)) {\r\n        return { x, y };\r\n      }\r\n    }\r\n  }\r\n  return null;\r\n}\r\n\r\n/**\r\n * Serializes workspaces into exact DESKNX4 layout.tsv format\r\n */\r\nexport function serializeLayout(workspaces: Workspace[], activeIndex: number): string {\r\n  const lines: string[] = [];\r\n  lines.push('DESKNX4');\r\n  lines.push(`A\\t${Math.max(0, Math.min(activeIndex, workspaces.length - 1))}`);\r\n\r\n  for (const desk of workspaces) {\r\n    const safeName = desk.name.replace(/[\\t\\n\\r]/g, ' ');\r\n    const safeBg = (desk.backgroundPath || '').replace(/[\\t\\n\\r]/g, ' ');\r\n    // Keep the file parseable by the native reader: no NaN, no out-of-range\r\n    // palette index, no undefined label opacity.\r\n    const labelOpacity = clampInt(desk.labelOpacity, 0, 255, 196);\r\n    const accentColor = clampInt(desk.accentColor, -1, 7, -1);\r\n    lines.push(\r\n      `D\\t${safeName}\\t${safeBg}\\t${clampInt(desk.backgroundFit, 0, 2, 0)}\\t${clampInt(desk.artworkMode, 0, 2, 0)}\\t${labelOpacity}\\t${accentColor}`\r\n    );\r\n  }\r\n\r\n  for (let d = 0; d < workspaces.length; d++) {\r\n    const desk = workspaces[d];\r\n    for (const t of desk.tiles) {\r\n      const code = t.unknownTypeCode || kindToCode(t.kind);\r\n      const safeTitle = (t.title ?? '').replace(/[\\t\\n\\r]/g, ' ');\r\n      const safeData = (t.data ?? '').replace(/[\\t\\n\\r]/g, ' ');\r\n      // Serializing an unsupported shape would be rewritten on the next parse.\r\n      const shape = nearestSupportedTileSize(t.w, t.h);\r\n      lines.push(\r\n        `I\\t${d}\\t${code}\\t${safeTitle}\\t${safeData}\\t${clampInt(t.x, 0, COLUMNS - 1, 0)}\\t${clampInt(t.y, 0, ROWS - 1, 0)}\\t${shape.w}\\t${shape.h}\\t${clampInt(t.color, 0, 7, 0)}`\r\n      );\r\n    }\r\n  }\r\n\r\n  return lines.join('\\n');\r\n}\r\n\r\n/**\r\n * Parses layout.tsv according to DESKNX1-DESKNX4 specification\r\n */\r\nexport function parseLayout(\r\n  tsvContent: string,\r\n  catalogResolver?: (data: string, kind: Kind) => { icon?: string; banner?: string }\r\n): { workspaces: Workspace[]; activeIndex: number } | null {\r\n  const lines = tsvContent.split(/\\r?\\n/).filter((l) => l.trim().length > 0);\r\n  if (lines.length === 0) return null;\r\n\r\n  const header = lines[0].trim();\r\n  if (!['DESKNX1', 'DESKNX2', 'DESKNX3', 'DESKNX4'].includes(header)) {\r\n    return null;\r\n  }\r\n\r\n  const hasWorkspaceAppearance = header === 'DESKNX4';\r\n  const hasActiveField = header !== 'DESKNX1';\r\n\r\n  let activeIndex = 0;\r\n  const workspaces: Workspace[] = [];\r\n\r\n  for (let i = 1; i < lines.length; i++) {\r\n    const line = lines[i];\r\n    const fields = line.split('\\t');\r\n    const tag = fields[0];\r\n\r\n    if (tag === 'A' && hasActiveField) {\r\n      activeIndex = parseInt(fields[1], 10) || 0;\r\n    } else if (tag === 'D') {\r\n      const name = fields[1] || `Desk ${workspaces.length + 1}`;\r\n      let bgPath = '';\r\n      let bgFit = 0;\r\n      let artMode = 0;\r\n      let labelOp = 196;\r\n      let accent = -1;\r\n\r\n      if (hasWorkspaceAppearance && fields.length >= 7) {\r\n        bgPath = fields[2] || '';\r\n        bgFit = clampInt(parseInt(fields[3], 10), 0, 2, 0);\r\n        artMode = clampInt(parseInt(fields[4], 10), 0, 2, 0);\r\n        labelOp = clampInt(parseInt(fields[5], 10), 0, 255, 196);\r\n        // `parseInt(...) ?? -1` let NaN through: NaN is not nullish, and a NaN\r\n        // accent colour poisons the serializer on the next save.\r\n        accent = clampInt(parseInt(fields[6], 10), -1, 7, -1);\r\n      }\r\n\r\n      workspaces.push({\r\n        id: `desk_${Date.now()}_${workspaces.length}`,\r\n        name,\r\n        tiles: [],\r\n        backgroundPath: bgPath,\r\n        backgroundFit: bgFit,\r\n        artworkMode: artMode,\r\n        labelOpacity: labelOp,\r\n        accentColor: accent,\r\n      });\r\n    } else if (tag === 'I') {\r\n      const deskIdx = parseInt(fields[1], 10);\r\n      if (isNaN(deskIdx) || deskIdx < 0 || deskIdx >= workspaces.length) continue;\r\n\r\n      const code = fields[2];\r\n      const kind = codeToKind(code);\r\n      const title = fields[3] || 'Untitled';\r\n      const data = fields[4] || '';\r\n      let x = parseInt(fields[5], 10) || 0;\r\n      let y = parseInt(fields[6], 10) || 0;\r\n      let w = parseInt(fields[7], 10) || 1;\r\n      let h = parseInt(fields[8], 10) || 1;\r\n      const color = parseInt(fields[9], 10) || 0;\r\n\r\n      // Snap to a supported shape exactly like the native reader instead of\r\n      // collapsing unknown sizes into a single cell.\r\n      const shape = nearestSupportedTileSize(w, h);\r\n      w = shape.w;\r\n      h = shape.h;\r\n\r\n      x = clampInt(x, 0, COLUMNS - 1, 0);\r\n      y = clampInt(y, 0, ROWS - 1, 0);\r\n\r\n      const desk = workspaces[deskIdx];\r\n      // Collision check\r\n      if (!placeable(desk, { x, y, w, h }, x, y)) {\r\n        const free = findFirstFree(desk, w, h);\r\n        if (free) {\r\n          x = free.x;\r\n          y = free.y;\r\n        } else {\r\n          continue; // Cannot fit\r\n        }\r\n      }\r\n\r\n      const media = catalogResolver ? catalogResolver(data, kind) : {};\r\n\r\n      desk.tiles.push({\r\n        id: `tile_${Date.now()}_${Math.random().toString(36).substring(2, 7)}`,\r\n        kind,\r\n        title,\r\n        data,\r\n        x,\r\n        y,\r\n        w,\r\n        h,\r\n        color,\r\n        unknownTypeCode: kind === 'Unsupported' ? code : undefined,\r\n        icon: media.icon,\r\n        banner: media.banner,\r\n      });\r\n    }\r\n  }\r\n\r\n  if (workspaces.length === 0) return null;\r\n  activeIndex = Math.max(0, Math.min(activeIndex, workspaces.length - 1));\r\n\r\n  return { workspaces, activeIndex };\r\n}\r\n\r\n/** Clamps a possibly-NaN integer into [min, max] with a fallback. */\r\nexport function clampInt(value: number, min: number, max: number, fallback: number): number {\r\n  if (!Number.isFinite(value)) return fallback;\r\n  return Math.max(min, Math.min(max, Math.trunc(value)));\r\n}\r\n"}