import React from 'react';
import { 
  ArrowUp, 
  ArrowDown, 
  ArrowLeft, 
  ArrowRight, 
  Home, 
  Minus, 
  Plus, 
  Circle, 
  Square 
} from 'lucide-react';
import { sound } from '../data/mockCatalog';

interface JoyconOverlayProps {
  visible: boolean;
  onNavigate: (direction: 'up' | 'down' | 'left' | 'right') => void;
  onButtonA: () => void;
  onButtonB: () => void;
  onButtonX: () => void;
  onButtonY: () => void;
  onShoulderL: () => void;
  onShoulderR: () => void;
  onButtonPlus: () => void;
  onButtonMinus: () => void;
  onButtonHome: () => void;
}

export const JoyconOverlay: React.FC<JoyconOverlayProps> = ({
  visible,
  onNavigate,
  onButtonA,
  onButtonB,
  onButtonX,
  onButtonY,
  onShoulderL,
  onShoulderR,
  onButtonPlus,
  onButtonMinus,
  onButtonHome,
}) => {
  if (!visible) return null;

  return (
    <div className="fixed bottom-12 inset-x-0 pointer-events-none flex justify-between px-4 md:px-8 z-30 select-none">
      
      {/* LEFT JOY-CON (Neon Blue) */}
      <div className="pointer-events-auto bg-[#00b4e6]/90 backdrop-blur-md p-3 rounded-3xl border-2 border-[#33ccff] shadow-[0_0_25px_rgba(0,180,230,0.35)] flex flex-col items-center gap-2 text-white">
        {/* L / ZL bumpers */}
        <div className="flex items-center gap-1.5 w-full justify-between">
          <button
            onClick={() => {
              sound.playNav();
              onShoulderL();
            }}
            className="px-2.5 py-1 rounded-md bg-black/40 hover:bg-black/60 text-[10px] font-bold font-mono active:scale-95 transition-transform"
            title="Switch Workspace Left (Q)"
          >
            L
          </button>
          <button
            onClick={() => {
              sound.playClick();
              onButtonMinus();
            }}
            className="p-1 rounded-full bg-black/40 hover:bg-black/60 active:scale-95"
            title="Minus (-)"
          >
            <Minus className="w-3.5 h-3.5" />
          </button>
        </div>

        {/* D-Pad Buttons */}
        <div className="grid grid-cols-3 gap-1 p-1">
          <div />
          <button
            onClick={() => {
              sound.playNav();
              onNavigate('up');
            }}
            className="w-7 h-7 rounded-md bg-black/70 hover:bg-black flex items-center justify-center active:scale-90"
            title="Up Arrow"
          >
            <ArrowUp className="w-4 h-4" />
          </button>
          <div />

          <button
            onClick={() => {
              sound.playNav();
              onNavigate('left');
            }}
            className="w-7 h-7 rounded-md bg-black/70 hover:bg-black flex items-center justify-center active:scale-90"
            title="Left Arrow"
          >
            <ArrowLeft className="w-4 h-4" />
          </button>
          <div className="w-7 h-7 rounded-md bg-black/30 flex items-center justify-center">
            <Circle className="w-2 h-2 text-white/40" />
          </div>
          <button
            onClick={() => {
              sound.playNav();
              onNavigate('right');
            }}
            className="w-7 h-7 rounded-md bg-black/70 hover:bg-black flex items-center justify-center active:scale-90"
            title="Right Arrow"
          >
            <ArrowRight className="w-4 h-4" />
          </button>

          <div />
          <button
            onClick={() => {
              sound.playNav();
              onNavigate('down');
            }}
            className="w-7 h-7 rounded-md bg-black/70 hover:bg-black flex items-center justify-center active:scale-90"
            title="Down Arrow"
          >
            <ArrowDown className="w-4 h-4" />
          </button>
          <div />
        </div>
      </div>

      {/* RIGHT JOY-CON (Neon Red) */}
      <div className="pointer-events-auto bg-[#e60012]/90 backdrop-blur-md p-3 rounded-3xl border-2 border-[#ff4d5a] shadow-[0_0_25px_rgba(230,0,18,0.35)] flex flex-col items-center gap-2 text-white">
        {/* R / Plus bumpers */}
        <div className="flex items-center gap-1.5 w-full justify-between">
          <button
            onClick={() => {
              sound.playClick();
              onButtonPlus();
            }}
            className="p-1 rounded-full bg-black/40 hover:bg-black/60 active:scale-95"
            title="Plus (+)"
          >
            <Plus className="w-3.5 h-3.5" />
          </button>
          <button
            onClick={() => {
              sound.playNav();
              onShoulderR();
            }}
            className="px-2.5 py-1 rounded-md bg-black/40 hover:bg-black/60 text-[10px] font-bold font-mono active:scale-95 transition-transform"
            title="Switch Workspace Right (E)"
          >
            R
          </button>
        </div>

        {/* ABXY Action Buttons */}
        <div className="grid grid-cols-3 gap-1 p-1">
          <div />
          <button
            onClick={() => {
              sound.playClick();
              onButtonX();
            }}
            className="w-7 h-7 rounded-full bg-black/70 hover:bg-black flex items-center justify-center text-xs font-bold font-mono active:scale-90"
            title="X (Options)"
          >
            X
          </button>
          <div />

          <button
            onClick={() => {
              sound.playNav();
              onButtonY();
            }}
            className="w-7 h-7 rounded-full bg-black/70 hover:bg-black flex items-center justify-center text-xs font-bold font-mono active:scale-90"
            title="Y (Catalog)"
          >
            Y
          </button>
          <button
            onClick={() => {
              sound.playClick();
              onButtonHome();
            }}
            className="w-7 h-7 rounded-full bg-black/30 hover:bg-black flex items-center justify-center active:scale-90"
            title="HOME"
          >
            <Home className="w-3.5 h-3.5" />
          </button>
          <button
            onClick={() => {
              sound.playSelect();
              onButtonA();
            }}
            className="w-7 h-7 rounded-full bg-black/70 hover:bg-black flex items-center justify-center text-xs font-bold font-mono active:scale-90"
            title="A (Select)"
          >
            A
          </button>

          <div />
          <button
            onClick={() => {
              sound.playBack();
              onButtonB();
            }}
            className="w-7 h-7 rounded-full bg-black/70 hover:bg-black flex items-center justify-center text-xs font-bold font-mono active:scale-90"
            title="B (Back)"
          >
            B
          </button>
          <div />
        </div>
      </div>
    </div>
  );
};
