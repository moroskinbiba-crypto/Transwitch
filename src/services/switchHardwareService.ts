/**
 * DeskNX Switch Hardware Service
 * Real Horizon OS (libnx) IPC bridge interface for Nintendo Switch hardware.
 * 
 * Works seamlessly in both environments:
 * 1. On real Nintendo Switch hardware (via local C++ IPC bridge daemon on 127.0.0.1:8080)
 * 2. In browser / dev mode (with realistic hardware simulation & mock fallbacks)
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
