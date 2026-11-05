import React, { useState, useEffect } from 'react';
import { 
  X, 
  Terminal, 
  Activity, 
  ShieldAlert, 
  Cpu, 
  Radio, 
  RotateCcw, 
  CheckCircle, 
  AlertTriangle 
} from 'lucide-react';
import { sound } from '../data/mockCatalog';
import { IPCMessage, LogEntry } from '../types/desknx';

interface DiagnosticsModalProps {
  isOpen: boolean;
  onClose: () => void;
  systemMode: boolean;
}

export const DiagnosticsModal: React.FC<DiagnosticsModalProps> = ({
  isOpen,
  onClose,
  systemMode,
}) => {
  const [activeTab, setActiveTab] = useState<'logs' | 'ipc' | 'recovery' | 'audit'>('logs');
  const [recoveryHoldTimer, setRecoveryHoldTimer] = useState<number>(0);
  const [recoverySuccess, setRecoverySuccess] = useState<boolean>(false);
  const [isHolding, setIsHolding] = useState<boolean>(false);

  const [logs, setLogs] = useState<LogEntry[]>([
    {
      id: '1',
      timestamp: '12:44:02.108',
      level: 'INFO',
      source: 'diagnostics',
      message: '[Init] DeskNX SDL2 UI initialized. Renderer: accelerated VSync 60fps.',
    },
    {
      id: '2',
      timestamp: '12:44:02.140',
      level: 'INFO',
      source: 'catalogue',
      message: '[NS:Service] Initialized account & ns services successfully. Title count: 12.',
    },
    {
      id: '3',
      timestamp: '12:44:02.195',
      level: 'INFO',
      source: 'catalogue',
      message: '[NRO:Scanner] Found 7 homebrew packages in sdmc:/switch/. Cache warm.',
    },
    {
      id: '4',
      timestamp: '12:44:02.240',
      level: 'DEBUG',
      source: 'ipc',
      message: '[IPC] Handshake sent: Magic=0x584E4B44 (DKNX), Version=2, Cmd=MenuReady.',
    },
    {
      id: '5',
      timestamp: '12:44:02.260',
      level: 'INFO',
      source: 'backend',
      message: '[sSystem] qlaunch replacement backend acknowledged MenuReady. State: IDLE.',
    },
  ]);

  const [ipcHistory, setIpcHistory] = useState<IPCMessage[]>([
    {
      timestamp: '12:44:02.240',
      magic: 'DKNX',
      version: 2,
      command: 'MenuReady (1)',
      payload: 'header_size=16, payload=0',
      direction: 'OUT',
      status: 'OK',
    },
    {
      timestamp: '12:44:02.260',
      magic: 'DKNX',
      version: 2,
      command: 'Heartbeat (4)',
      payload: 'sequence=1, uptime=2s',
      direction: 'IN',
      status: 'OK',
    },
    {
      timestamp: '12:44:12.260',
      magic: 'DKNX',
      version: 2,
      command: 'Heartbeat (4)',
      payload: 'sequence=2, uptime=12s',
      direction: 'IN',
      status: 'OK',
    },
  ]);

  // Simulation of periodic heartbeat
  useEffect(() => {
    if (!isOpen) return;
    const interval = setInterval(() => {
      const timeStr = new Date().toLocaleTimeString() + '.' + Math.floor(Math.random() * 900 + 100);
      setIpcHistory((prev) => [
        {
          timestamp: timeStr,
          magic: 'DKNX',
          version: 2,
          command: 'Heartbeat (4)',
          payload: `backend_alive=true, free_mem=2684MB`,
          direction: 'IN',
          status: 'OK',
        },
        ...prev.slice(0, 19),
      ]);
    }, 5000);
    return () => clearInterval(interval);
  }, [isOpen]);

  // Recovery Combo simulation: hold 3 seconds
  useEffect(() => {
    let timer: ReturnType<typeof setTimeout>;
    if (isHolding && recoveryHoldTimer < 3) {
      timer = setTimeout(() => {
        setRecoveryHoldTimer((t) => t + 1);
      }, 1000);
    } else if (isHolding && recoveryHoldTimer >= 3 && !recoverySuccess) {
      sound.playLaunch();
      setRecoverySuccess(true);
    }
    return () => clearTimeout(timer);
  }, [isHolding, recoveryHoldTimer, recoverySuccess]);

  const handleStartHold = () => {
    setIsHolding(true);
    setRecoveryHoldTimer(0);
    setRecoverySuccess(false);
  };

  const handleStopHold = () => {
    setIsHolding(false);
    if (!recoverySuccess) {
      setRecoveryHoldTimer(0);
    }
  };

  if (!isOpen) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 md:p-8 bg-black/85 backdrop-blur-md animate-in fade-in duration-200">
      <div className="relative w-full max-w-5xl h-[84vh] bg-zinc-900 border border-white/20 rounded-2xl shadow-2xl overflow-hidden flex flex-col select-none text-zinc-100">
        
        {/* Header */}
        <div className="px-6 py-4 bg-zinc-800/90 border-b border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <Activity className="w-6 h-6 text-emerald-400" />
            <div>
              <h2 className="text-lg font-bold tracking-wide">DeskNX Diagnostics & System Lab</h2>
              <p className="text-xs text-zinc-400">
                Atmosphère qlaunch backend · Protocol v2 IPC · SDMC Logs · Recovery
              </p>
            </div>
          </div>
          <button
            onClick={() => {
              sound.playBack();
              onClose();
            }}
            className="p-2 rounded-lg bg-zinc-700/60 hover:bg-red-500/80 text-zinc-300 hover:text-white transition-all"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Tab Selector */}
        <div className="px-6 py-2.5 bg-zinc-950/70 border-b border-white/10 flex items-center gap-2">
          {[
            { id: 'logs', label: 'diagnostics.log & catalogue.log', icon: Terminal },
            { id: 'ipc', label: 'Protocol v2 IPC Monitor', icon: Radio },
            { id: 'recovery', label: 'Emergency Recovery Combo', icon: ShieldAlert },
            { id: 'audit', label: 'System Audit 2026', icon: Cpu },
          ].map((tab) => {
            const Icon = tab.icon;
            return (
              <button
                key={tab.id}
                onClick={() => {
                  sound.playClick();
                  setActiveTab(tab.id as any);
                }}
                className={`flex items-center gap-2 px-3 py-1.5 rounded-lg text-xs font-semibold whitespace-nowrap transition-all ${
                  activeTab === tab.id
                    ? 'bg-cyan-500 text-black shadow'
                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
                }`}
              >
                <Icon className="w-4 h-4" />
                {tab.label}
              </button>
            );
          })}
        </div>

        {/* Tab Body */}
        <div className="flex-1 p-6 overflow-y-auto">
          {/* TAB 1: LOGS */}
          {activeTab === 'logs' && (
            <div className="space-y-4">
              <div className="flex items-center justify-between">
                <span className="text-xs font-semibold text-zinc-300">
                  sdmc:/switch/DeskNX/diagnostics.log (Live stream):
                </span>
                <span className="text-[10px] font-mono text-emerald-400 flex items-center gap-1.5">
                  <span className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse" /> Live Rotation
                </span>
              </div>

              <div className="bg-black/80 rounded-xl p-4 border border-white/10 font-mono text-xs text-zinc-300 space-y-2 max-h-[460px] overflow-y-auto">
                {logs.map((log) => (
                  <div key={log.id} className="flex items-start gap-2.5 border-b border-white/5 pb-1.5">
                    <span className="text-zinc-500 text-[11px] whitespace-nowrap">{log.timestamp}</span>
                    <span
                      className={`text-[10px] font-bold px-1.5 py-0.5 rounded uppercase ${
                        log.level === 'INFO'
                          ? 'bg-blue-900/60 text-blue-300'
                          : log.level === 'WARN'
                          ? 'bg-amber-900/60 text-amber-300'
                          : log.level === 'ERROR'
                          ? 'bg-red-900/60 text-red-300'
                          : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      {log.level}
                    </span>
                    <span className="text-cyan-400 text-[11px]">[{log.source}]</span>
                    <span className="flex-1 text-zinc-200">{log.message}</span>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* TAB 2: IPC PROTOCOL */}
          {activeTab === 'ipc' && (
            <div className="space-y-4">
              <div className="p-3.5 rounded-xl bg-cyan-950/40 border border-cyan-500/30 text-xs text-cyan-200 flex items-center justify-between">
                <div>
                  <strong>Protocol Header:</strong> Magic = <code className="text-white">0x584E4B44 (DKNX)</code> · Version = <code className="text-white">2</code> (V2 LaunchTitle with AccountUid)
                </div>
                <div className="text-emerald-400 font-semibold font-mono">
                  Channel: /dev/desknx_ipc
                </div>
              </div>

              <div className="bg-black/80 rounded-xl p-4 border border-white/10 font-mono text-xs overflow-x-auto">
                <table className="w-full text-left">
                  <thead>
                    <tr className="text-zinc-500 border-b border-white/10 pb-2">
                      <th className="py-2">Time</th>
                      <th>Dir</th>
                      <th>Magic</th>
                      <th>Ver</th>
                      <th>Command</th>
                      <th>Payload Details</th>
                      <th>Status</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-white/5">
                    {ipcHistory.map((msg, i) => (
                      <tr key={i} className="hover:bg-white/5">
                        <td className="py-2 text-zinc-400 text-[11px]">{msg.timestamp}</td>
                        <td className="py-2">
                          <span
                            className={`px-1.5 py-0.5 rounded text-[10px] font-bold ${
                              msg.direction === 'OUT' ? 'bg-purple-900/60 text-purple-300' : 'bg-emerald-900/60 text-emerald-300'
                            }`}
                          >
                            {msg.direction}
                          </span>
                        </td>
                        <td className="text-cyan-400 font-bold">{msg.magic}</td>
                        <td className="text-zinc-300">v{msg.version}</td>
                        <td className="text-white font-medium">{msg.command}</td>
                        <td className="text-zinc-400 text-[11px] truncate max-w-xs">{msg.payload}</td>
                        <td>
                          <span className="text-[10px] bg-emerald-500/20 text-emerald-400 px-1.5 py-0.5 rounded font-bold">
                            {msg.status}
                          </span>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          )}

          {/* TAB 3: EMERGENCY RECOVERY COMBO */}
          {activeTab === 'recovery' && (
            <div className="space-y-6 max-w-xl mx-auto text-center">
              <div className="p-4 rounded-xl bg-amber-950/40 border border-amber-500/40 text-amber-200 text-xs text-left flex items-start gap-3">
                <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" />
                <div>
                  <strong className="block text-amber-300 font-semibold mb-1">
                    Atmosphère Emergency Recovery Combo (README §4):
                  </strong>
                  If DeskNX fails to launch any HOME applet slots and no title is active, holding 
                  <code className="text-white bg-black/40 px-1 py-0.5 rounded mx-1">
                    L + R + ZL + ZR + Plus + Minus + D-Pad Down
                  </code> 
                  for 3 continuous seconds triggers an emergency qlaunch rollback and safety reboot.
                </div>
              </div>

              <div className="p-8 rounded-2xl bg-black/50 border border-white/10 flex flex-col items-center">
                <div className="text-sm font-semibold text-zinc-300 mb-4">
                  Hold the button below for 3 seconds to test emergency recovery signal:
                </div>

                <button
                  onMouseDown={handleStartHold}
                  onMouseUp={handleStopHold}
                  onTouchStart={handleStartHold}
                  onTouchEnd={handleStopHold}
                  className={`px-8 py-5 rounded-2xl font-bold text-sm tracking-wide transition-all shadow-xl active:scale-95 ${
                    recoverySuccess
                      ? 'bg-emerald-500 text-black'
                      : isHolding
                      ? 'bg-amber-500 text-black ring-4 ring-amber-400/50 scale-105'
                      : 'bg-zinc-800 hover:bg-zinc-700 text-white border border-white/20'
                  }`}
                >
                  {recoverySuccess ? (
                    <span className="flex items-center gap-2">
                      <CheckCircle className="w-5 h-5" /> RECOVERY TRIGGERED — Rollback active
                    </span>
                  ) : isHolding ? (
                    `Holding... ${recoveryHoldTimer}/3 seconds`
                  ) : (
                    'Hold L+R+ZL+ZR+Plus+Minus+Down (Press & Hold)'
                  )}
                </button>

                <div className="w-48 bg-black/60 h-2 rounded-full overflow-hidden mt-4">
                  <div
                    className="bg-amber-400 h-full transition-all duration-300"
                    style={{ width: `${(recoveryHoldTimer / 3) * 100}%` }}
                  />
                </div>
              </div>
            </div>
          )}

          {/* TAB 4: AUDIT */}
          {activeTab === 'audit' && (
            <div className="space-y-4 max-w-2xl font-mono text-xs text-zinc-300">
              <div className="p-4 rounded-xl bg-black/40 border border-white/10 space-y-2">
                <h4 className="font-bold text-cyan-400">DeskNX 2026 Technical Audit Report</h4>
                <div className="text-zinc-400">Atmosphère Target: 1.12.1 · libnx Ref: Fixed release</div>
                <ul className="list-disc pl-5 space-y-1 text-zinc-300">
                  <li><strong>Memory & VSync:</strong> VSync renderer runs uninterrupted; software delay active only when fallback without hardware sync.</li>
                  <li><strong>Catalogue LRU Cache:</strong> Icons cached in memory with normalized 16-hex key. Lazy PDM query thread.</li>
                  <li><strong>Atomic Storage:</strong> DESKNX4 layout published via temporary write + rename to prevent SD corruption.</li>
                  <li><strong>Protocol Rules:</strong> Strict size checking (v1 8 bytes, v2 32 bytes). Unknown commands rejected safely.</li>
                </ul>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
