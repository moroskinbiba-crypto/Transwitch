/**
 * DeskNX Switch Hardware Service
 *
 * STATUS: simulation only. No bridge daemon ships inside DeskNX.nro.
 *
 * The shipped standalone build (`src-switch/main.cpp`) merely hosts this web UI
 * inside the Horizon OS WebKit applet; that applet has no IPC channel to ns:,
 * pdm:qry, psm: or hid: services, and an offline document cannot open sockets to
 * 127.0.0.1. Every hardware call below therefore resolves to its mock fallback,
 * which keeps the UI usable as a design preview and MUST NOT be mistaken for a
 * working launcher.
 *
 * Real title enumeration, launching and telemetry require the native path:
 * `projects/DeskNXMenu` (HOME library applet, SDL2 + libnx) or the HomeNX-based
 * system backend. The HTTP bridge described below is a future transport, not an
 * existing component — see docs/HOMENX_FRONTEND_PLAN.md.
 */

export interface SwitchSystemInfo {
  isHardware: boolean;
  firmwareVersion: string;
  cfwVersion: string;
  batteryPercent: number;
  isCharging: boolean;
  socTemperatureC: number;
  nandFreeBytes: number;
  sdFreeBytes: number;
}

export interface SwitchInstalledTitle {
  titleId: string;
  name: string;
  author: string;
  version: string;
  iconUrl: string;
  bannerUrl?: string;
  playtimeSeconds: number;
  launchCount: number;
  lastPlayedTimestamp: number;
  isGameCard: boolean;
}

class SwitchHardwareBridge {
  private _isHardware: boolean = false;
  // Reserved transport for a future native bridge. Nothing listens on this port
  // in any shipped build, so calls fail fast and fall back to mock data.
  private _bridgeBaseUrl: string = 'http://127.0.0.1:8080/api';
  private _isInitialized: boolean = false;

  constructor() {
    this.detectHardware();
  }

  public detectHardware(): boolean {
    if (typeof window === 'undefined') return false;
    
    // Check WebKit User-Agent on Nintendo Switch (e.g. "Nintendo Switch / ...")
    const ua = navigator.userAgent || '';
    const hasSwitchUA = ua.includes('Nintendo Switch') || ua.includes('NintendoSwitch');
    const hasNxGlobal = !!(window as any).nx;
    
    this._isHardware = hasSwitchUA || hasNxGlobal;
    return this._isHardware;
  }

  public get isHardware(): boolean {
    return this._isHardware;
  }

  /**
   * Ping local C++ libnx bridge daemon
   */
  public async checkBridgeAlive(): Promise<boolean> {
    if (!this._isHardware) return false;
    try {
      const res = await fetch(`${this._bridgeBaseUrl}/status`, { signal: AbortSignal.timeout(1200) });
      if (res.ok) {
        this._isInitialized = true;
        return true;
      }
    } catch {
      // Running standalone offline web applet without localhost bridge
    }
    return false;
  }

  /**
   * Fetch real installed applications on the Switch via ns:dev / pdm:qry
   */
  public async getInstalledTitles(): Promise<SwitchInstalledTitle[] | null> {
    if (!this._isHardware) return null;

    try {
      const res = await fetch(`${this._bridgeBaseUrl}/titles`, { signal: AbortSignal.timeout(3000) });
      if (res.ok) {
        const data = await res.json();
        return data.titles || [];
      }
    } catch (e) {
      console.warn('[DeskNX Bridge] Unable to reach /api/titles:', e);
    }
    return null;
  }

  /**
   * Launch a game on real hardware via Horizon OS am / ns:dev appletRequestLaunchApplication
   */
  public async launchTitle(titleId: string): Promise<boolean> {
    console.log(`[DeskNX Hardware] Launching TitleID: ${titleId}`);
    
    if (this._isHardware) {
      try {
        const res = await fetch(`${this._bridgeBaseUrl}/launch?tid=${encodeURIComponent(titleId)}`, {
          method: 'POST',
          signal: AbortSignal.timeout(4000),
        });
        return res.ok;
      } catch (e) {
        console.error('[DeskNX Hardware] Failed to launch application on Switch:', e);
      }
    }
    return false;
  }

  /**
   * Read real battery percentage and charging state via libnx psm
   */
  public async getBatteryStatus(): Promise<{ percent: number; charging: boolean } | null> {
    if (!this._isHardware) return null;

    try {
      const res = await fetch(`${this._bridgeBaseUrl}/battery`, { signal: AbortSignal.timeout(1500) });
      if (res.ok) {
        return await res.json();
      }
    } catch {
      // Fallback
    }
    return null;
  }

  /**
   * Send HD Rumble vibration to Joy-Con (L / R) or Pro Controller
   */
  public async sendHapticRumble(pattern: number[] = [100, 50, 150]): Promise<void> {
    // 1. Web Vibration API fallback
    if (typeof navigator !== 'undefined' && navigator.vibrate) {
      try {
        navigator.vibrate(pattern);
      } catch {
        // Ignore
      }
    }

    // 2. Real Switch hardware hidSendVibrationValues IPC
    if (this._isHardware) {
      try {
        await fetch(`${this._bridgeBaseUrl}/rumble`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ pattern }),
          signal: AbortSignal.timeout(800),
        });
      } catch {
        // Ignore
      }
    }
  }

  /**
   * Request console to enter Sleep mode (spsmRequestSleep)
   */
  public async requestSleep(): Promise<boolean> {
    if (this._isHardware) {
      try {
        const res = await fetch(`${this._bridgeBaseUrl}/power/sleep`, { method: 'POST' });
        return res.ok;
      } catch {
        // Ignore
      }
    }
    return false;
  }

  /**
   * Request console reboot (spsmRequestReboot)
   */
  public async requestReboot(): Promise<boolean> {
    if (this._isHardware) {
      try {
        const res = await fetch(`${this._bridgeBaseUrl}/power/reboot`, { method: 'POST' });
        return res.ok;
      } catch {
        // Ignore
      }
    }
    return false;
  }
}

export const SwitchHardware = new SwitchHardwareBridge();

{"content":"/**\r\n * DeskNX Switch Hardware Service\r\n *\r\n * STATUS: simulation only. No bridge daemon ships inside DeskNX.nro.\r\n *\r\n * The shipped standalone build (`src-switch/main.cpp`) merely hosts this web UI\r\n * inside the Horizon OS WebKit applet; that applet has no IPC channel to ns:,\r\n * pdm:qry, psm: or hid: services, and an offline document cannot open sockets to\r\n * 127.0.0.1. Every hardware call below therefore resolves to its mock fallback,\r\n * which keeps the UI usable as a design preview and MUST NOT be mistaken for a\r\n * working launcher.\r\n *\r\n * Real title enumeration, launching and telemetry require the native path:\r\n * `projects/DeskNXMenu` (HOME library applet, SDL2 + libnx) or the HomeNX-based\r\n * system backend. The HTTP bridge described below is a future transport, not an\r\n * existing component — see docs/HOMENX_FRONTEND_PLAN.md.\r\n */\r\n\r\nexport interface SwitchSystemInfo {\r\n  isHardware: boolean;\r\n  firmwareVersion: string;\r\n  cfwVersion: string;\r\n  batteryPercent: number;\r\n  isCharging: boolean;\r\n  socTemperatureC: number;\r\n  nandFreeBytes: number;\r\n  sdFreeBytes: number;\r\n}\r\n\r\nexport interface SwitchInstalledTitle {\r\n  titleId: string;\r\n  name: string;\r\n  author: string;\r\n  version: string;\r\n  iconUrl: string;\r\n  bannerUrl?: string;\r\n  playtimeSeconds: number;\r\n  launchCount: number;\r\n  lastPlayedTimestamp: number;\r\n  isGameCard: boolean;\r\n}\r\n\r\nclass SwitchHardwareBridge {\r\n  private _isHardware: boolean = false;\r\n  // Reserved transport for a future native bridge. Nothing listens on this port\r\n  // in any shipped build, so calls fail fast and fall back to mock data.\r\n  private _bridgeBaseUrl: string = 'http://127.0.0.1:8080/api';\r\n  private _isInitialized: boolean = false;\r\n\r\n  constructor() {\r\n    this.detectHardware();\r\n  }\r\n\r\n  public detectHardware(): boolean {\r\n    if (typeof window === 'undefined') return false;\r\n    \r\n    // Check WebKit User-Agent on Nintendo Switch (e.g. \"Nintendo Switch / ...\")\r\n    const ua = navigator.userAgent || '';\r\n    const hasSwitchUA = ua.includes('Nintendo Switch') || ua.includes('NintendoSwitch');\r\n    const hasNxGlobal = !!(window as any).nx;\r\n    \r\n    this._isHardware = hasSwitchUA || hasNxGlobal;\r\n    return this._isHardware;\r\n  }\r\n\r\n  public get isHardware(): boolean {\r\n    return this._isHardware;\r\n  }\r\n\r\n  /**\r\n   * Ping local C++ libnx bridge daemon\r\n   */\r\n  public async checkBridgeAlive(): Promise<boolean> {\r\n    if (!this._isHardware) return false;\r\n    try {\r\n      const res = await fetch(`${this._bridgeBaseUrl}/status`, { signal: AbortSignal.timeout(1200) });\r\n      if (res.ok) {\r\n        this._isInitialized = true;\r\n        return true;\r\n      }\r\n    } catch {\r\n      // Running standalone offline web applet without localhost bridge\r\n    }\r\n    return false;\r\n  }\r\n\r\n  /**\r\n   * Fetch real installed applications on the Switch via ns:dev / pdm:qry\r\n   */\r\n  public async getInstalledTitles(): Promise<SwitchInstalledTitle[] | null> {\r\n    if (!this._isHardware) return null;\r\n\r\n    try {\r\n      const res = await fetch(`${this._bridgeBaseUrl}/titles`, { signal: AbortSignal.timeout(3000) });\r\n      if (res.ok) {\r\n        const data = await res.json();\r\n        return data.titles || [];\r\n      }\r\n    } catch (e) {\r\n      console.warn('[DeskNX Bridge] Unable to reach /api/titles:', e);\r\n    }\r\n    return null;\r\n  }\r\n\r\n  /**\r\n   * Launch a game on real hardware via Horizon OS am / ns:dev appletRequestLaunchApplication\r\n   */\r\n  public async launchTitle(titleId: string): Promise<boolean> {\r\n    console.log(`[DeskNX Hardware] Launching TitleID: ${titleId}`);\r\n    \r\n    if (this._isHardware) {\r\n      try {\r\n        const res = await fetch(`${this._bridgeBaseUrl}/launch?tid=${encodeURIComponent(titleId)}`, {\r\n          method: 'POST',\r\n          signal: AbortSignal.timeout(4000),\r\n        });\r\n        return res.ok;\r\n      } catch (e) {\r\n        console.error('[DeskNX Hardware] Failed to launch application on Switch:', e);\r\n      }\r\n    }\r\n    return false;\r\n  }\r\n\r\n  /**\r\n   * Read real battery percentage and charging state via libnx psm\r\n   */\r\n  public async getBatteryStatus(): Promise<{ percent: number; charging: boolean } | null> {\r\n    if (!this._isHardware) return null;\r\n\r\n    try {\r\n      const res = await fetch(`${this._bridgeBaseUrl}/battery`, { signal: AbortSignal.timeout(1500) });\r\n      if (res.ok) {\r\n        return await res.json();\r\n      }\r\n    } catch {\r\n      // Fallback\r\n    }\r\n    return null;\r\n  }\r\n\r\n  /**\r\n   * Send HD Rumble vibration to Joy-Con (L / R) or Pro Controller\r\n   */\r\n  public async sendHapticRumble(pattern: number[] = [100, 50, 150]): Promise<void> {\r\n    // 1. Web Vibration API fallback\r\n    if (typeof navigator !== 'undefined' && navigator.vibrate) {\r\n      try {\r\n        navigator.vibrate(pattern);\r\n      } catch {\r\n        // Ignore\r\n      }\r\n    }\r\n\r\n    // 2. Real Switch hardware hidSendVibrationValues IPC\r\n    if (this._isHardware) {\r\n      try {\r\n        await fetch(`${this._bridgeBaseUrl}/rumble`, {\r\n          method: 'POST',\r\n          headers: { 'Content-Type': 'application/json' },\r\n          body: JSON.stringify({ pattern }),\r\n          signal: AbortSignal.timeout(800),\r\n        });\r\n      } catch {\r\n        // Ignore\r\n      }\r\n    }\r\n  }\r\n\r\n  /**\r\n   * Request console to enter Sleep mode (spsmRequestSleep)\r\n   */\r\n  public async requestSleep(): Promise<boolean> {\r\n    if (this._isHardware) {\r\n      try {\r\n        const res = await fetch(`${this._bridgeBaseUrl}/power/sleep`, { method: 'POST' });\r\n        return res.ok;\r\n      } catch {\r\n        // Ignore\r\n      }\r\n    }\r\n    return false;\r\n  }\r\n\r\n  /**\r\n   * Request console reboot (spsmRequestReboot)\r\n   */\r\n  public async requestReboot(): Promise<boolean> {\r\n    if (this._isHardware) {\r\n      try {\r\n        const res = await fetch(`${this._bridgeBaseUrl}/power/reboot`, { method: 'POST' });\r\n        return res.ok;\r\n      } catch {\r\n        // Ignore\r\n      }\r\n    }\r\n    return false;\r\n  }\r\n}\r\n\r\nexport const SwitchHardware = new SwitchHardwareBridge();\r\n"}