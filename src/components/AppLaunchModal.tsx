import React, { useState, useEffect } from 'react';
import { Candidate } from '../types/desknx';
import { sound } from '../data/mockCatalog';
import { 
  Home, 
  Gamepad2, 
  Terminal, 
  Sparkles
} from 'lucide-react';

interface AppLaunchModalProps {
  candidate: Candidate | null;
  onClose: () => void;
  systemMode: boolean;
}

export const AppLaunchModal: React.FC<AppLaunchModalProps> = ({
  candidate,
  onClose,
  systemMode,
}) => {
  const [bootStage, setBootStage] = useState<'animating' | 'running'>('animating');
  const [sessionSeconds, setSessionSeconds] = useState(0);

  useEffect(() => {
    if (!candidate) {
      setBootStage('animating');
      setSessionSeconds(0);
      return;
    }

    sound.playLaunch();
    const timer = setTimeout(() => {
      setBootStage('running');
    }, 1100);

    return () => clearTimeout(timer);
  }, [candidate]);

  useEffect(() => {
    if (bootStage !== 'running') return;
    const interval = setInterval(() => {
      setSessionSeconds((s) => s + 1);
    }, 1000);
    return () => clearInterval(interval);
  }, [bootStage]);

  if (!candidate) return null;

  const handleReturnHome = () => {
    sound.playSelect();
    onClose();
  };

  const formatTimer = (sec: number) => {
    const m = Math.floor(sec / 60).toString().padStart(2, '0');
    const s = (sec % 60).toString().padStart(2, '0');
    return `${m}:${s}`;
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black select-none text-white overflow-hidden animate-in fade-in duration-200">
      
      {/* NINTENDO SWITCH JOY-CON SNAP ANIMATION */}
      {bootStage === 'animating' && (
        <div className="flex flex-col items-center justify-center animate-pulse">
          <div className="flex items-center gap-3">
            <div className="w-12 h-24 rounded-l-2xl bg-[#e60012] border-4 border-black flex items-center justify-center shadow-[0_0_30px_rgba(230,0,18,0.5)]">
              <div className="w-4 h-4 rounded-full bg-white mb-6" />
            </div>
            <div className="w-12 h-24 rounded-r-2xl bg-[#00b4e6] border-4 border-black flex items-center justify-center shadow-[0_0_30px_rgba(0,180,230,0.5)]">
              <div className="w-4 h-4 rounded-full bg-white mt-6" />
            </div>
          </div>
          <div className="mt-6 text-sm font-bold tracking-widest uppercase font-mono text-zinc-300">
            Nintendo Switch
          </div>
          <div className="mt-2 text-xs text-zinc-400">
            {systemMode ? 'Starting Title...' : 'Launching in Homebrew Mode...'}
          </div>
        </div>
      )}

      {/* RUNNING GAME ENVIRONMENT */}
      {bootStage === 'running' && (
        <div className="relative w-full h-full flex flex-col justify-between bg-zinc-950">
          
          {/* Top Clean Bar */}
          <div className="px-6 py-3 bg-black/80 backdrop-blur-md border-b border-white/10 flex items-center justify-between z-20">
            <div className="flex items-center gap-3">
              <span className={`px-2.5 py-0.5 rounded-full text-xs font-bold ${
                candidate.kind === 'Game' ? 'bg-red-600 text-white' : 'bg-cyan-600 text-white'
              }`}>
                {candidate.kind === 'Game' ? 'Game' : 'Homebrew'}
              </span>
              <span className="font-bold text-sm text-white truncate max-w-md">
                {candidate.title}
              </span>
              <span className="text-xs text-zinc-400">
                {candidate.author || 'Nintendo'}
              </span>
            </div>

            <div className="flex items-center gap-5 text-xs text-zinc-300">
              <div>Session Time: <span className="text-cyan-400 font-mono font-bold">{formatTimer(sessionSeconds)}</span></div>
            </div>
          </div>

          {/* Main Visual Display */}
          <div className="flex-1 relative overflow-hidden flex items-center justify-center p-8">
            {candidate.banner || candidate.icon ? (
              <img
                src={candidate.banner || candidate.icon}
                alt={candidate.title}
                className="absolute inset-0 w-full h-full object-cover opacity-60 filter blur-xs scale-105"
              />
            ) : null}

            <div className="relative z-10 max-w-xl w-full bg-black/80 backdrop-blur-2xl border border-white/20 rounded-3xl p-8 shadow-2xl text-center space-y-6">
              <div className="w-24 h-24 mx-auto rounded-3xl overflow-hidden shadow-2xl border-2 border-white/30">
                <img
                  src={candidate.icon || candidate.banner}
                  alt={candidate.title}
                  className="w-full h-full object-cover"
                />
              </div>

              <div>
                <h2 className="text-2xl font-bold text-white drop-shadow">
                  {candidate.title}
                </h2>
                <p className="text-sm text-cyan-400 mt-1 font-medium">
                  {candidate.author || 'Nintendo Switch'}
                </p>
              </div>

              <div className="p-4 rounded-2xl bg-white/5 border border-white/10 text-xs text-zinc-300 leading-relaxed">
                {candidate.description || 'Application running. All game controls are active.'}
              </div>

              <div className="flex items-center justify-center gap-4 pt-2">
                <button
                  onClick={handleReturnHome}
                  className="flex items-center gap-2 px-6 py-3.5 rounded-2xl bg-cyan-500 hover:bg-cyan-400 text-black font-bold text-sm shadow-xl transition-all active:scale-95"
                >
                  <Home className="w-5 h-5" /> Press HOME to Return to Launcher
                </button>
              </div>
            </div>
          </div>

          {/* Bottom Bar Hints */}
          <div className="px-6 py-3 bg-black/90 border-t border-white/10 flex items-center justify-between text-xs text-zinc-400 z-20">
            <div>
              {systemMode ? 'Full HOME Menu Override' : 'Standalone Applet Mode'}
            </div>
            <div className="flex items-center gap-4 font-mono">
              <span>[HOME] Exit to Desktop</span>
              <span>[+] Menu</span>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
