import { Candidate, CacheStats, Workspace } from '../types/desknx';
import { CATALOG_ITEMS, INITIAL_WORKSPACES } from '../data/mockCatalog';

const CACHE_KEY_CATALOG = 'desknx_slauncher_cache_catalog_v2';
const CACHE_KEY_STATS = 'desknx_slauncher_cache_stats_v2';

let inMemoryCatalogCache: Candidate[] | null = null;
let inMemoryWorkspacesCache: Workspace[] | null = null;

let stats: CacheStats = {
  totalItems: 0,
  cacheSizeKb: 0,
  lastSyncTimestamp: Date.now(),
  hitCount: 0,
  missCount: 0,
  averageRetrievalMs: 0.18,
  isWarm: true,
};

export class SLauncherCacheEngine {
  static init() {
    try {
      const savedStats = localStorage.getItem(CACHE_KEY_STATS);
      if (savedStats) {
        stats = { ...stats, ...JSON.parse(savedStats) };
      }
      // Pre-warm the cache immediately
      this.getWarmCatalog();
    } catch (e) {
      console.warn('[sLauncherCache] Failed to load saved cache stats', e);
    }
  }

  static getWarmCatalog(): { items: Candidate[]; fromWarmCache: boolean; loadTimeMs: number } {
    const startTime = performance.now();

    // 1. Check in-memory L1 cache (sub-millisecond)
    if (inMemoryCatalogCache && inMemoryCatalogCache.length > 0) {
      stats.hitCount++;
      const duration = Number((performance.now() - startTime).toFixed(2));
      stats.averageRetrievalMs = Number(((stats.averageRetrievalMs * 4 + duration) / 5).toFixed(2));
      this.persistStats();
      return {
        items: inMemoryCatalogCache,
        fromWarmCache: true,
        loadTimeMs: duration,
      };
    }

    // 2. Check LocalStorage L2 cache
    try {
      const stored = localStorage.getItem(CACHE_KEY_CATALOG);
      if (stored) {
        const parsed = JSON.parse(stored) as Candidate[];
        if (Array.isArray(parsed) && parsed.length > 0) {
          inMemoryCatalogCache = parsed;
          stats.hitCount++;
          stats.totalItems = parsed.length;
          stats.cacheSizeKb = Math.round(new Blob([stored]).size / 1024);
          const duration = Number((performance.now() - startTime).toFixed(2));
          stats.averageRetrievalMs = Number(((stats.averageRetrievalMs * 4 + duration) / 5).toFixed(2));
          this.persistStats();
          return {
            items: parsed,
            fromWarmCache: true,
            loadTimeMs: duration,
          };
        }
      }
    } catch (e) {
      console.warn('[sLauncherCache] L2 cache read error', e);
    }

    // 3. Cache Miss: generate warm cache from CATALOG_ITEMS
    stats.missCount++;
    inMemoryCatalogCache = CATALOG_ITEMS.map((item) => ({
      ...item,
      cachedAt: Date.now(),
    }));
    this.saveToStorage(inMemoryCatalogCache);

    const duration = Number((performance.now() - startTime).toFixed(2));
    return {
      items: inMemoryCatalogCache,
      fromWarmCache: false,
      loadTimeMs: duration,
    };
  }

  static saveToStorage(items: Candidate[]) {
    try {
      const json = JSON.stringify(items);
      localStorage.setItem(CACHE_KEY_CATALOG, json);
      stats.totalItems = items.length;
      stats.cacheSizeKb = Math.round(new Blob([json]).size / 1024);
      stats.lastSyncTimestamp = Date.now();
      stats.isWarm = true;
      this.persistStats();
    } catch (e) {
      console.error('[sLauncherCache] Cache storage failed', e);
    }
  }

  static invalidateAndRescan(): { items: Candidate[]; scanTimeMs: number } {
    const startTime = performance.now();
    inMemoryCatalogCache = CATALOG_ITEMS.map((item) => ({
      ...item,
      cachedAt: Date.now(),
      playtimeSeconds: item.playtimeSeconds + Math.floor(Math.random() * 300),
    }));

    this.saveToStorage(inMemoryCatalogCache);
    stats.missCount = 0;
    stats.hitCount = 1;
    const duration = Number((performance.now() - startTime).toFixed(2));
    this.persistStats();

    return {
      items: inMemoryCatalogCache,
      scanTimeMs: duration,
    };
  }

  static getStats(): CacheStats {
    const hitRate = stats.hitCount + stats.missCount > 0
      ? ((stats.hitCount / (stats.hitCount + stats.missCount)) * 100).toFixed(1)
      : '100.0';

    return {
      ...stats,
      hitCount: stats.hitCount,
      missCount: stats.missCount,
    };
  }

  private static persistStats() {
    try {
      localStorage.setItem(CACHE_KEY_STATS, JSON.stringify(stats));
    } catch (e) {
      // Ignore
    }
  }
}
