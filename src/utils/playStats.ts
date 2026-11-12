import { CATALOG_ITEMS } from '../data/mockCatalog';
import type { Kind } from '../types/desknx';

/**
 * Honest play statistics for a launched tile.
 *
 * The web UI has no channel to `pdm:qry` (see switchHardwareService), so any
 * number invented at launch time is a lie rendered as "12 ч. сыграно". This
 * helper resolves stats from the catalogue record when one exists and reports
 * `available: false` otherwise, so the UI can say "no data" instead of
 * fabricating progress.
 */
export interface PlayStats {
  playtimeSeconds: number;
  lastPlayed: number;
  launchCount: number;
  /** False when no play record exists; the UI must not invent values. */
  available: boolean;
}

const NO_PLAY_RECORD: PlayStats = {
  playtimeSeconds: 0,
  lastPlayed: 0,
  launchCount: 0,
  available: false,
};

export function resolvePlayStats(tile: { kind: Kind; data: string }): PlayStats {
  const match = CATALOG_ITEMS.find(
    (item) => item.kind === tile.kind && item.data === tile.data
  );

  if (!match) {
    return NO_PLAY_RECORD;
  }

  return {
    playtimeSeconds: match.playtimeSeconds,
    lastPlayed: match.lastPlayed,
    launchCount: match.launchCount,
    available: match.playtimeSeconds > 0 || match.launchCount > 0,
  };
}

{"content":"import { CATALOG_ITEMS } from '../data/mockCatalog';\nimport type { Kind } from '../types/desknx';\n\n/**\n * Honest play statistics for a launched tile.\n *\n * The web UI has no channel to `pdm:qry` (see switchHardwareService), so any\n * number invented at launch time is a lie rendered as \"12 ч. сыграно\". This\n * helper resolves stats from the catalogue record when one exists and reports\n * `available: false` otherwise, so the UI can say \"no data\" instead of\n * fabricating progress.\n */\nexport interface PlayStats {\n  playtimeSeconds: number;\n  lastPlayed: number;\n  launchCount: number;\n  /** False when no play record exists; the UI must not invent values. */\n  available: boolean;\n}\n\nconst NO_PLAY_RECORD: PlayStats = {\n  playtimeSeconds: 0,\n  lastPlayed: 0,\n  launchCount: 0,\n  available: false,\n};\n\nexport function resolvePlayStats(tile: { kind: Kind; data: string }): PlayStats {\n  const match = CATALOG_ITEMS.find(\n    (item) => item.kind === tile.kind && item.data === tile.data\n  );\n\n  if (!match) {\n    return NO_PLAY_RECORD;\n  }\n\n  return {\n    playtimeSeconds: match.playtimeSeconds,\n    lastPlayed: match.lastPlayed,\n    launchCount: match.launchCount,\n    available: match.playtimeSeconds > 0 || match.launchCount > 0,\n  };\n}\n"}