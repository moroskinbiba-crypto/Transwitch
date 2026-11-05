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

export function isSupportedTileSize(w: number, h: number): boolean {
  return (
    (w === 1 && h === 1) ||
    (w === 2 && h === 1) ||
    (w === 2 && h === 2) ||
    (w === 2 && h === 3)
  );
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
    lines.push(
      `D\t${safeName}\t${safeBg}\t${desk.backgroundFit}\t${desk.artworkMode}\t${desk.labelOpacity}\t${desk.accentColor}`
    );
  }

  for (let d = 0; d < workspaces.length; d++) {
    const desk = workspaces[d];
    for (const t of desk.tiles) {
      const code = t.unknownTypeCode || kindToCode(t.kind);
      const safeTitle = t.title.replace(/[\t\n\r]/g, ' ');
      const safeData = t.data.replace(/[\t\n\r]/g, ' ');
      lines.push(
        `I\t${d}\t${code}\t${safeTitle}\t${safeData}\t${t.x}\t${t.y}\t${t.w}\t${t.h}\t${t.color}`
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
        bgFit = parseInt(fields[3], 10) || 0;
        artMode = parseInt(fields[4], 10) || 0;
        labelOp = parseInt(fields[5], 10) || 196;
        accent = parseInt(fields[6], 10) ?? -1;
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

      // Ensure dimensions are supported
      if (!isSupportedTileSize(w, h)) {
        w = 1;
        h = 1;
      }

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
