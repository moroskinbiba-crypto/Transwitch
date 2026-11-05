import React, { useState, useEffect } from 'react';
import { 
  Clock as ClockIcon, 
  Battery as BatteryIcon, 
  BatteryCharging, 
  HardDrive, 
  Calendar as CalendarIcon, 
  Play,
  Gamepad2,
  Plane,
  Wifi,
  Volume2,
  Sun,
  Power,
  RotateCw,
  Bluetooth,
  Radio,
  Sparkles,
  Zap,
  Cpu,
  Layers
} from 'lucide-react';
import { Candidate, Tile } from '../types/desknx';
import { CATALOG_ITEMS, sound } from '../data/mockCatalog';

interface WidgetProps {
  tile: Tile;
  accentColor?: string;
  onLaunchItem?: (candidate: Candidate) => void;
  onOpenSystemAction?: (action: string) => void;
}

// -------------------------------------------------------------
// Component: Real Battery Glyph (Used for Joy-Cons and Console)
// -------------------------------------------------------------
const BatteryGlyph: React.FC<{
  percent: number;
  width?: number; // width in px
  height?: number; // height in px
  fillColor?: string;
  isCharging?: boolean;
}> = ({ percent, width = 36, height = 18, fillColor = '#34c759', isCharging }) => {
  return (
    <div className="relative flex items-center select-none shrink-0">
      {/* Battery Shell */}
      <div 
        className="rounded-[5px] border-2 border-white/70 p-[2px] flex items-center bg-black/60 overflow-hidden shadow-inner"
        style={{ width: `${width}px`, height: `${height}px` }}
      >
        <div 
          className="h-full rounded-[2.5px] transition-all duration-300 relative overflow-hidden"
          style={{ 
            width: `${percent}%`, 
            backgroundColor: fillColor,
            boxShadow: `0 0 10px ${fillColor}66`
          }} 
        >
          {isCharging && (
            <div className="absolute inset-0 bg-white/30 animate-pulse" />
          )}
        </div>
      </div>
      {/* Battery Positive Terminal Cap */}
      <div 
        className="bg-white/70 rounded-r-[2px] ml-[1.5px]" 
        style={{ width: '3px', height: `${Math.max(6, Math.round(height * 0.5))}px` }}
      />
      {isCharging && (
        <Zap className="w-3 h-3 text-amber-300 fill-amber-300 absolute -top-1.5 -right-1.5 animate-bounce drop-shadow" />
      )}
    </div>
  );
};

// -------------------------------------------------------------
// 1. Clock & Date Widget (Scales for 1x1, 2x1, 3x1, 2x2, 3x2)
// -------------------------------------------------------------
export const WidgetClock: React.FC<WidgetProps> = ({ tile }) => {
  const [time, setTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const hours = time.getHours().toString().padStart(2, '0');
  const minutes = time.getMinutes().toString().padStart(2, '0');
  const seconds = time.getSeconds().toString().padStart(2, '0');
  const weekdayShort = time.toLocaleDateString('ru-RU', { weekday: 'short' });
  const weekdayFull = time.toLocaleDateString('ru-RU', { weekday: 'long' });
  const dayMonth = time.toLocaleDateString('ru-RU', { day: 'numeric', month: 'short' });

  const is1x1 = tile.w === 1 && tile.h === 1;
  const isTall = tile.h >= 2;

  // 1x1 Mini Compact Layout
  if (is1x1) {
    return (
      <div className="w-full h-full min-h-0 flex flex-col justify-between p-3 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
        <div className="flex items-center justify-between text-[11px] font-bold text-zinc-300">
          <span className="text-amber-400 capitalize">{weekdayShort}</span>
          <span>{dayMonth}</span>
        </div>
        <div className="my-auto text-center">
          <div className="text-2xl font-black tracking-tight text-white drop-shadow">
            {hours}:{minutes}
          </div>
          <div className="text-[10px] text-zinc-400 font-mono">
            {seconds}с
          </div>
        </div>
        <div className="flex items-center justify-center gap-1 text-[10px] text-zinc-300 font-semibold">
          <Sun className="w-3 h-3 text-amber-400 fill-amber-400" />
          <span>21°C Киото</span>
        </div>
      </div>
    );
  }

  // 2x1, 3x1, 2x2 Standard & Wide Layout
  return (
    <div className={`w-full h-full min-h-0 flex flex-col justify-between select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden ${
      isTall ? 'p-5' : 'p-3.5 md:p-4'
    }`}>
      {/* Top Header */}
      <div className="flex items-center justify-between">
        <span className="flex items-center gap-1.5 text-xs font-semibold text-zinc-300 tracking-tight">
          <span className="w-2 h-2 rounded-full bg-orange-400" />
          <span>Киото • Nintendo HQ</span>
        </span>
        <div className="flex items-center gap-1.5 px-2.5 py-0.5 rounded-full bg-white/10 backdrop-blur-md text-xs font-medium text-white/90">
          <Sun className="w-3.5 h-3.5 text-amber-400 fill-amber-400" />
          <span>21° Ясно</span>
        </div>
      </div>

      {/* Main Clock */}
      <div className="my-auto">
        <div className={`${isTall ? 'text-5xl md:text-6xl' : 'text-3xl md:text-4xl'} font-black tracking-tight text-white drop-shadow-[0_2px_10px_rgba(0,0,0,0.5)]`}>
          {hours}:{minutes}
        </div>
        <div className="text-xs md:text-sm font-semibold text-white/80 capitalize mt-0.5 tracking-tight">
          {weekdayFull}, {time.toLocaleDateString('ru-RU', { day: 'numeric', month: 'long' })}
        </div>
      </div>

      {/* Bottom Smart Bar */}
      {isTall ? (
        <div className="flex items-center justify-between text-xs text-zinc-400 font-medium border-t border-white/10 pt-2">
          <span>Следующее событие</span>
          <span className="text-white font-semibold">18:00 Турнир Smash</span>
        </div>
      ) : (
        <div className="text-[11px] text-zinc-400 flex items-center justify-between border-t border-white/10 pt-1.5">
          <span>Точное время Switch</span>
          <span className="font-mono text-zinc-300 font-bold">{seconds} сек</span>
        </div>
      )}
    </div>
  );
};

// -------------------------------------------------------------
// 2. Battery Widget (Vibrant Green Upper Section + Joy-Cons)
// -------------------------------------------------------------
export const WidgetBattery: React.FC<WidgetProps> = ({ tile }) => {
  const [percent, setPercent] = useState(88);
  const [charging, setCharging] = useState(false);

  const toggleCharging = (e: React.MouseEvent) => {
    e.stopPropagation();
    sound.playClick();
    setCharging((c) => !c);
  };

  const is1x1 = tile.w === 1 && tile.h === 1;
  const isTall = tile.h >= 2;

  // 1x1 Compact Layout: Upper green half for Switch, lower dark half for Joy-Cons
  if (is1x1) {
    return (
      <div 
        className="w-full h-full min-h-0 flex flex-col select-none cursor-pointer overflow-hidden transition-all group"
        onClick={toggleCharging}
        title="Нажмите для переключения зарядки"
      >
        {/* Full Upper Half: Vibrant Battery Green */}
        <div className={`flex-[1.2] flex flex-col justify-between p-2.5 transition-colors duration-300 text-white ${
          charging ? 'bg-gradient-to-br from-amber-500 to-amber-600' : 'bg-gradient-to-br from-emerald-500 via-green-500 to-emerald-600 shadow-inner'
        }`}>
          <div className="flex items-center justify-between">
            <span className="text-[11px] font-black tracking-tight flex items-center gap-1 drop-shadow-sm">
              <BatteryIcon className="w-3.5 h-3.5 fill-white/20 stroke-[2.2]" />
              <span>Switch</span>
            </span>
            {charging ? (
              <span className="flex items-center gap-0.5 text-[9px] font-black bg-black/25 text-white px-1.5 py-0.5 rounded-full">
                <Zap className="w-2.5 h-2.5 fill-current animate-pulse" /> 15W
              </span>
            ) : (
              <span className="text-[9px] font-black bg-black/20 text-white px-1.5 py-0.5 rounded-full font-mono">
                5.3ч
              </span>
            )}
          </div>

          <div className="flex items-center justify-between my-auto">
            <div className="text-3xl font-black tracking-tight drop-shadow-md leading-none">
              {percent}<span className="text-lg opacity-85">%</span>
            </div>
            <div className="w-8 h-4 rounded-[4px] border-2 border-white p-[1.5px] flex items-center bg-black/20 shadow-sm shrink-0">
              <div 
                className="h-full bg-white rounded-[1.5px] transition-all"
                style={{ width: `${percent}%` }}
              />
            </div>
          </div>
        </div>

        {/* Lower Half: Joy-Cons L & R in Sleek Frosted Dark Container */}
        <div className="flex-1 bg-black/60 backdrop-blur-md p-2 flex items-center justify-around border-t border-white/10 text-white">
          <div className="flex items-center gap-1.5">
            <span className="w-4 h-4 rounded-[4px] bg-[#ff3b30] flex items-center justify-center text-[8px] font-black text-white shadow-sm">
              L
            </span>
            <div className="flex flex-col">
              <span className="text-[9px] font-black text-rose-300 leading-none">100%</span>
              <div className="mt-0.5">
                <BatteryGlyph percent={100} width={20} height={8} fillColor="#34c759" />
              </div>
            </div>
          </div>

          <div className="w-[1px] h-5 bg-white/10" />

          <div className="flex items-center gap-1.5">
            <span className="w-4 h-4 rounded-[4px] bg-[#007aff] flex items-center justify-center text-[8px] font-black text-white shadow-sm">
              R
            </span>
            <div className="flex flex-col">
              <span className="text-[9px] font-black text-cyan-300 leading-none">100%</span>
              <div className="mt-0.5">
                <BatteryGlyph percent={100} width={20} height={8} fillColor="#34c759" />
              </div>
            </div>
          </div>
        </div>
      </div>
    );
  }

  // 2x1 & 3x1 Horizontal Layout: Top half green bar + bottom half Joy-Cons
  if (!isTall) {
    return (
      <div 
        className="w-full h-full min-h-0 flex flex-col select-none cursor-pointer overflow-hidden transition-all"
        onClick={toggleCharging}
        title="Нажмите для переключения зарядки"
      >
        {/* Full Upper Section: Vibrant Battery Green */}
        <div className={`flex-[1.2] flex items-center justify-between px-4 py-2.5 transition-colors duration-300 text-white shadow-inner ${
          charging ? 'bg-gradient-to-r from-amber-500 via-amber-600 to-amber-500' : 'bg-gradient-to-r from-emerald-500 via-green-500 to-emerald-600'
        }`}>
          {/* Left: Percentage & Title */}
          <div className="flex items-center gap-3">
            <div className="text-3xl md:text-4xl font-black tracking-tight leading-none drop-shadow-md">
              {percent}%
            </div>
            <div className="flex flex-col">
              <div className="text-xs md:text-sm font-black text-white leading-tight flex items-center gap-1.5">
                <span>Nintendo Switch OLED</span>
                <span className="text-[9px] font-mono bg-black/25 px-1.5 py-0.5 rounded font-bold">
                  4310 мАч
                </span>
              </div>
              <div className="text-[11px] text-white/90 font-medium">
                {charging ? '15.0W Быстрая зарядка USB-PD' : 'Автономная работа (~5 ч. 20 мин.)'}
              </div>
            </div>
          </div>

          {/* Right: Big Stylized Battery Capsule in White/Translucent Shell */}
          <div className="flex items-center gap-2">
            <div className="w-16 h-7 rounded-[6px] border-2 border-white p-[2px] flex items-center bg-black/25 shadow-md">
              <div 
                className="h-full bg-white rounded-[3px] transition-all relative overflow-hidden"
                style={{ width: `${percent}%` }}
              >
                {charging && <div className="absolute inset-0 bg-amber-300 animate-pulse" />}
              </div>
            </div>
            <div className="w-[3px] h-3.5 bg-white rounded-r-[1.5px]" />
          </div>
        </div>

        {/* Lower Section: Joy-Cons (L & R) in Sleek Glass Container */}
        <div className="flex-1 bg-black/60 backdrop-blur-md px-4 py-2 flex items-center justify-between border-t border-white/10 text-white">
          <div className="flex items-center gap-2 text-xs font-bold text-zinc-300">
            <Gamepad2 className="w-4 h-4 text-zinc-400" />
            <span>Контроллеры Joy-Con:</span>
          </div>

          <div className="flex items-center gap-6">
            {/* Joy-Con Left */}
            <div className="flex items-center gap-2 bg-white/5 px-2.5 py-1 rounded-xl border border-white/10">
              <div className="w-4 h-5 rounded-[4px] bg-[#ff3b30] flex items-center justify-center text-[8px] font-black text-white shadow-sm">
                L
              </div>
              <span className="text-xs font-black text-rose-300">100%</span>
              <BatteryGlyph percent={100} width={26} height={10} fillColor="#34c759" />
            </div>

            {/* Joy-Con Right */}
            <div className="flex items-center gap-2 bg-white/5 px-2.5 py-1 rounded-xl border border-white/10">
              <div className="w-4 h-5 rounded-[4px] bg-[#007aff] flex items-center justify-center text-[8px] font-black text-white shadow-sm">
                R
              </div>
              <span className="text-xs font-black text-cyan-300">100%</span>
              <BatteryGlyph percent={100} width={26} height={10} fillColor="#34c759" />
            </div>
          </div>
        </div>
      </div>
    );
  }

  // 2x2, 2x3 & 3x2 Large Layout: Dominant full-bleed upper green hero section + Lower Joy-Con cards
  return (
    <div 
      className="w-full h-full min-h-0 flex flex-col select-none cursor-pointer overflow-hidden transition-all"
      onClick={toggleCharging}
      title="Нажмите для переключения зарядки"
    >
      {/* Full Upper Section: Painted Vibrant Battery Green */}
      <div className={`flex-[1.3] flex flex-col justify-between p-5 md:p-6 transition-colors duration-300 text-white shadow-inner relative overflow-hidden ${
        charging ? 'bg-gradient-to-br from-amber-500 via-amber-600 to-amber-500' : 'bg-gradient-to-br from-emerald-500 via-green-500 to-emerald-600'
      }`}>
        {/* Subtle decorative glow */}
        <div className="absolute -top-12 -right-12 w-48 h-48 rounded-full bg-white/10 blur-2xl pointer-events-none" />

        {/* Header inside green top */}
        <div className="flex items-center justify-between relative z-10">
          <div className="flex items-center gap-2 text-xs md:text-sm font-black tracking-tight text-white drop-shadow">
            <BatteryIcon className="w-5 h-5 fill-white/20 stroke-[2.2]" />
            <span>Nintendo Switch OLED Battery</span>
          </div>
          <span className="text-xs font-black px-3 py-1 rounded-full bg-black/25 text-white backdrop-blur-md border border-white/20 shadow-sm flex items-center gap-1.5">
            {charging ? <Zap className="w-3.5 h-3.5 fill-current animate-bounce text-amber-300" /> : <span className="w-2 h-2 rounded-full bg-white animate-pulse" />}
            {charging ? 'USB-PD 15.0W' : 'Автономный режим'}
          </span>
        </div>

        {/* Center: Enormous % and Specs */}
        <div className="flex items-center justify-between my-auto relative z-10 py-1">
          <div className="flex flex-col">
            <div className="text-5xl md:text-7xl font-black text-white tracking-tight leading-none drop-shadow-[0_4px_16px_rgba(0,0,0,0.3)]">
              {percent}<span className="text-3xl md:text-4xl opacity-80">%</span>
            </div>
            <div className="text-xs md:text-sm font-bold text-white/90 mt-1 flex items-center gap-2">
              <span>Литий-ионный (3.92V)</span>
              <span>•</span>
              <span>Состояние: 100%</span>
            </div>
          </div>

          <div className="flex flex-col items-end">
            {/* Big Console Battery Graphic */}
            <div className="relative flex items-center">
              <div className="w-20 md:w-24 h-9 md:h-11 rounded-[8px] border-[2.5px] border-white p-[3px] flex items-center bg-black/25 shadow-lg">
                <div 
                  className="h-full bg-white rounded-[4px] transition-all relative overflow-hidden"
                  style={{ width: `${percent}%` }}
                >
                  {charging && <div className="absolute inset-0 bg-amber-300/60 animate-pulse" />}
                </div>
              </div>
              <div className="w-[4px] h-5 bg-white rounded-r-[2px] ml-[1px]" />
            </div>
            <span className="text-xs font-mono font-bold text-white/80 mt-1">4310 мАч</span>
          </div>
        </div>

        {/* Bottom edge of green banner */}
        <div className="flex items-center justify-between text-xs font-bold text-white/80 border-t border-white/20 pt-2 relative z-10">
          <span>Осталось времени работы</span>
          <span className="font-mono text-white font-extrabold text-sm">~5 ч. 20 мин.</span>
        </div>
      </div>

      {/* Lower Section: Joy-Con (L) and Joy-Con (R) Controllers in Sleek Bento Card */}
      <div className="flex-1 bg-black/60 backdrop-blur-md p-4 md:p-5 flex flex-col justify-between border-t border-white/15 text-white">
        <div className="flex items-center justify-between text-xs font-black text-zinc-300 uppercase tracking-wider mb-2">
          <span>Подключенные Joy-Con контроллеры</span>
          <span className="text-emerald-400 font-bold">Bluetooth • 1000 Гц</span>
        </div>

        <div className="grid grid-cols-2 gap-3.5">
          {/* Joy-Con Left */}
          <div className="bg-white/5 rounded-2xl p-3 flex items-center justify-between border border-white/10 shadow-sm">
            <div className="flex items-center gap-3">
              <div className="w-6 h-8 rounded-[6px] bg-[#ff3b30] flex flex-col items-center justify-between py-1 text-[9px] font-black text-white shadow-md">
                <span>−</span>
                <span>L</span>
              </div>
              <div className="flex flex-col">
                <span className="text-xs font-black text-rose-300">Joy-Con (L)</span>
                <span className="text-[10px] text-zinc-400 font-medium">Neon Red</span>
              </div>
            </div>
            <div className="flex items-center gap-2">
              <BatteryGlyph percent={100} width={28} height={12} fillColor="#34c759" />
              <span className="text-xs font-black text-white font-mono">100%</span>
            </div>
          </div>

          {/* Joy-Con Right */}
          <div className="bg-white/5 rounded-2xl p-3 flex items-center justify-between border border-white/10 shadow-sm">
            <div className="flex items-center gap-3">
              <div className="w-6 h-8 rounded-[6px] bg-[#007aff] flex flex-col items-center justify-between py-1 text-[9px] font-black text-white shadow-md">
                <span>＋</span>
                <span>R</span>
              </div>
              <div className="flex flex-col">
                <span className="text-xs font-black text-cyan-300">Joy-Con (R)</span>
                <span className="text-[10px] text-zinc-400 font-medium">Neon Blue</span>
              </div>
            </div>
            <div className="flex items-center gap-2">
              <BatteryGlyph percent={100} width={28} height={12} fillColor="#34c759" />
              <span className="text-xs font-black text-white font-mono">100%</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

// -------------------------------------------------------------
// 3. Recent Launches Widget (Reliable flex/grid scaling)
// -------------------------------------------------------------
export const WidgetRecent: React.FC<WidgetProps> = ({ tile, onLaunchItem }) => {
  const is1x1 = tile.w === 1 && tile.h === 1;
  const isWide = tile.w >= 3;
  const isTall = tile.h >= 2;

  // 1x1: Feature the most recent game cleanly
  if (is1x1) {
    const single = CATALOG_ITEMS[0];
    return (
      <div 
        onClick={() => {
          sound.playLaunch();
          if (onLaunchItem && single) onLaunchItem(single);
        }}
        className="relative w-full h-full min-h-0 flex flex-col justify-end p-2.5 overflow-hidden group cursor-pointer select-none bg-black/40"
      >
        {single?.icon && (
          <img 
            src={single.icon} 
            alt={single.title} 
            className="absolute inset-0 w-full h-full object-cover group-hover:scale-105 transition-transform duration-300" 
          />
        )}
        <div className="absolute inset-0 bg-gradient-to-t from-black/90 via-black/40 to-transparent pointer-events-none" />
        <div className="relative z-10">
          <span className="text-[9px] font-bold text-cyan-300 flex items-center gap-1">
            <Play className="w-2.5 h-2.5 fill-current" /> Недавняя
          </span>
          <div className="text-xs font-extrabold text-white truncate drop-shadow mt-0.5">
            {single?.title}
          </div>
        </div>
      </div>
    );
  }

  // 2x1, 3x1 Horizontal Row of Games
  const itemsCount = isWide ? 4 : 3;
  const recents = CATALOG_ITEMS.slice(0, itemsCount);

  return (
    <div className="w-full h-full min-h-0 flex flex-col justify-between p-3 md:p-3.5 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between mb-1">
        <span className="text-xs font-bold text-zinc-300 tracking-tight flex items-center gap-1.5">
          <Gamepad2 className="w-4 h-4 text-cyan-400" />
          <span>Быстрый запуск</span>
        </span>
        <span className="text-[10px] font-semibold text-cyan-300 bg-cyan-950/40 px-2 py-0.5 rounded-full border border-cyan-500/20">
          Недавние
        </span>
      </div>

      {/* Evenly Spaced Flex Row */}
      <div className="flex items-center justify-around gap-2 my-auto w-full">
        {recents.map((item) => (
          <div
            key={item.id}
            onClick={(e) => {
              e.stopPropagation();
              sound.playLaunch();
              if (onLaunchItem) onLaunchItem(item);
            }}
            className="group flex flex-col items-center text-center cursor-pointer flex-1 min-w-0"
          >
            <div className="relative w-12 h-12 md:w-14 md:h-14 rounded-[18px] overflow-hidden bg-black/40 border border-white/20 shadow-md group-hover:scale-105 group-hover:shadow-[0_8px_20px_rgba(0,122,255,0.4)] transition-all">
              <img
                src={item.icon}
                alt={item.title}
                className="w-full h-full object-cover"
              />
              <div className="absolute inset-0 bg-black/35 opacity-0 group-hover:opacity-100 transition-opacity flex items-center justify-center">
                <Play className="w-4 h-4 text-white fill-current" />
              </div>
            </div>
            <span className="text-[10px] font-bold text-white/90 truncate w-full mt-1 leading-tight">
              {item.title}
            </span>
          </div>
        ))}
      </div>

      {/* Footer */}
      <div className="text-[10px] text-zinc-400 flex items-center justify-between border-t border-white/10 pt-1.5 font-medium">
        <span>Нажмите для запуска</span>
        <span className="text-blue-400 font-bold">Играть ↵</span>
      </div>
    </div>
  );
};

// -------------------------------------------------------------
// 4. iOS Classic Calendar Widget (Responsive)
// -------------------------------------------------------------
export const WidgetCalendar: React.FC<WidgetProps> = ({ tile }) => {
  const now = new Date();
  const currentDay = now.getDate();
  const weekday = now.toLocaleDateString('ru-RU', { weekday: 'long' });
  const monthName = now.toLocaleDateString('ru-RU', { month: 'long' });

  const is1x1 = tile.w === 1 && tile.h === 1;

  if (is1x1) {
    return (
      <div className="w-full h-full min-h-0 flex flex-col justify-between overflow-hidden select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white">
        <div className="bg-[#ff3b30] px-3 py-1 flex items-center justify-between">
          <span className="text-[10px] font-black uppercase text-white truncate">{monthName}</span>
          <span className="text-[10px] font-bold text-white capitalize">{weekday.slice(0, 2)}</span>
        </div>
        <div className="my-auto text-center p-2">
          <span className="text-3xl font-black text-white leading-none drop-shadow">
            {currentDay}
          </span>
          <div className="text-[10px] text-zinc-300 font-bold mt-1 truncate capitalize">
            {weekday}
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="w-full h-full min-h-0 flex flex-col justify-between overflow-hidden select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white">
      {/* iOS Red Top Banner */}
      <div className="bg-[#ff3b30] px-4 py-2 flex items-center justify-between shadow-md">
        <span className="text-xs font-black tracking-widest uppercase text-white">
          {monthName}
        </span>
        <span className="text-xs font-bold text-white/90 capitalize">
          {weekday}
        </span>
      </div>

      {/* Date Number & Next Event */}
      <div className="p-3.5 flex-1 flex flex-col justify-between">
        <div className="flex items-baseline gap-3 my-auto">
          <span className="text-4xl md:text-5xl font-black text-white tracking-tight leading-none drop-shadow">
            {currentDay}
          </span>
          <div className="flex flex-col text-left">
            <span className="text-xs font-bold text-white capitalize">{weekday}</span>
            <span className="text-[11px] text-zinc-400 font-medium">Событий нет</span>
          </div>
        </div>

        {/* Event Pill */}
        <div className="bg-white/10 backdrop-blur-md rounded-xl p-2 flex items-center gap-2.5 border border-white/10">
          <div className="w-1 h-6 rounded-full bg-[#ff3b30]" />
          <div className="flex flex-col text-left truncate">
            <span className="text-xs font-bold text-white truncate">The Legend of Zelda: TOTK</span>
            <span className="text-[10px] text-zinc-400 font-medium">19:30 • Продолжить</span>
          </div>
        </div>
      </div>
    </div>
  );
};

// -------------------------------------------------------------
// 5. iOS Control Center Bento Box (Responsive)
// -------------------------------------------------------------
export const WidgetSystemTools: React.FC<WidgetProps> = ({ tile, onOpenSystemAction }) => {
  const [airplane, setAirplane] = useState(false);
  const [wifi, setWifi] = useState(true);
  const [bluetooth, setBluetooth] = useState(true);

  const is1x1 = tile.w === 1 && tile.h === 1;

  if (is1x1) {
    return (
      <div className="w-full h-full min-h-0 grid grid-cols-2 gap-1.5 p-2 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
        {/* Airplane */}
        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setAirplane((a) => !a);
          }}
          className={`rounded-xl flex flex-col items-center justify-center p-1 transition-all ${
            airplane ? 'bg-[#ff9500] text-white' : 'bg-black/40 text-zinc-300'
          }`}
          title="Авиарежим"
        >
          <Plane className="w-4 h-4" />
          <span className="text-[9px] font-bold mt-0.5">{airplane ? 'Вкл' : 'Авиа'}</span>
        </button>

        {/* Wi-Fi */}
        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setWifi((w) => !w);
          }}
          className={`rounded-xl flex flex-col items-center justify-center p-1 transition-all ${
            wifi ? 'bg-[#007aff] text-white' : 'bg-black/40 text-zinc-300'
          }`}
          title="Wi-Fi"
        >
          <Wifi className="w-4 h-4" />
          <span className="text-[9px] font-bold mt-0.5">{wifi ? 'Вкл' : 'Wi-Fi'}</span>
        </button>

        {/* Bluetooth */}
        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setBluetooth((b) => !b);
          }}
          className={`rounded-xl flex flex-col items-center justify-center p-1 transition-all ${
            bluetooth ? 'bg-[#34c759] text-white' : 'bg-black/40 text-zinc-300'
          }`}
          title="Bluetooth Joy-Con"
        >
          <Bluetooth className="w-4 h-4" />
          <span className="text-[9px] font-bold mt-0.5">{bluetooth ? 'Вкл' : 'BT'}</span>
        </button>

        {/* Sleep */}
        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            if (onOpenSystemAction) onOpenSystemAction('sleep');
            else alert('Nintendo Switch переходит в спящий режим');
          }}
          className="rounded-xl flex flex-col items-center justify-center p-1 bg-black/40 hover:bg-purple-600/40 text-purple-300 transition-all"
          title="Спящий режим"
        >
          <Power className="w-4 h-4" />
          <span className="text-[9px] font-bold mt-0.5">Сон</span>
        </button>
      </div>
    );
  }

  return (
    <div className="w-full h-full min-h-0 flex flex-col justify-between p-3.5 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between mb-1.5">
        <span className="text-xs font-bold text-zinc-300 tracking-tight flex items-center gap-1.5">
          <Sparkles className="w-3.5 h-3.5 text-blue-400" />
          <span>Пункт управления</span>
        </span>
        <span className="text-[11px] font-semibold text-zinc-400">Nintendo OS</span>
      </div>

      {/* Bento Module */}
      <div className="grid grid-cols-2 gap-2 my-auto">
        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setAirplane((a) => !a);
          }}
          className={`flex items-center gap-2 p-1.5 rounded-xl transition-all border ${
            airplane 
              ? 'bg-[#ff9500] border-amber-300 text-white shadow-md' 
              : 'bg-black/35 hover:bg-black/50 border-white/10 text-white/80'
          }`}
        >
          <div className={`w-7 h-7 rounded-full flex items-center justify-center ${airplane ? 'bg-white/20' : 'bg-white/10'}`}>
            <Plane className="w-3.5 h-3.5" />
          </div>
          <div className="text-left">
            <div className="text-xs font-bold leading-tight">Авиарежим</div>
            <div className="text-[10px] opacity-80">{airplane ? 'Вкл' : 'Выкл'}</div>
          </div>
        </button>

        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setWifi((w) => !w);
          }}
          className={`flex items-center gap-2 p-1.5 rounded-xl transition-all border ${
            wifi 
              ? 'bg-[#007aff] border-blue-400 text-white shadow-md' 
              : 'bg-black/35 hover:bg-black/50 border-white/10 text-white/80'
          }`}
        >
          <div className={`w-7 h-7 rounded-full flex items-center justify-center ${wifi ? 'bg-white/20' : 'bg-white/10'}`}>
            <Wifi className="w-3.5 h-3.5" />
          </div>
          <div className="text-left">
            <div className="text-xs font-bold leading-tight">Wi-Fi</div>
            <div className="text-[10px] opacity-80">{wifi ? 'Switch_5G' : 'Выкл'}</div>
          </div>
        </button>

        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            setBluetooth((b) => !b);
          }}
          className={`flex items-center gap-2 p-1.5 rounded-xl transition-all border ${
            bluetooth 
              ? 'bg-[#34c759] border-emerald-400 text-white shadow-md' 
              : 'bg-black/35 hover:bg-black/50 border-white/10 text-white/80'
          }`}
        >
          <div className={`w-7 h-7 rounded-full flex items-center justify-center ${bluetooth ? 'bg-white/20' : 'bg-white/10'}`}>
            <Bluetooth className="w-3.5 h-3.5" />
          </div>
          <div className="text-left">
            <div className="text-xs font-bold leading-tight">Bluetooth</div>
            <div className="text-[10px] opacity-80">{bluetooth ? '2 Joy-Con' : 'Выкл'}</div>
          </div>
        </button>

        <button
          onClick={(e) => {
            e.stopPropagation();
            sound.playClick();
            if (onOpenSystemAction) onOpenSystemAction('sleep');
            else alert('Nintendo Switch переходит в спящий режим');
          }}
          className="flex items-center gap-2 p-1.5 rounded-xl bg-black/35 hover:bg-purple-600/30 border border-white/10 transition-all text-white/80"
        >
          <div className="w-7 h-7 rounded-full bg-purple-500/20 text-purple-400 flex items-center justify-center">
            <Power className="w-3.5 h-3.5" />
          </div>
          <div className="text-left">
            <div className="text-xs font-bold leading-tight">Сон</div>
            <div className="text-[10px] text-zinc-400">Режим сна</div>
          </div>
        </button>
      </div>

      {/* Sliders Preview */}
      <div className="grid grid-cols-2 gap-2 pt-1.5 border-t border-white/10">
        <div className="bg-black/35 rounded-xl px-2.5 py-1 flex items-center justify-between border border-white/10">
          <Sun className="w-3.5 h-3.5 text-amber-400" />
          <span className="text-xs font-bold">85%</span>
        </div>
        <div className="bg-black/35 rounded-xl px-2.5 py-1 flex items-center justify-between border border-white/10">
          <Volume2 className="w-3.5 h-3.5 text-cyan-400" />
          <span className="text-xs font-bold">70%</span>
        </div>
      </div>
    </div>
  );
};

// -------------------------------------------------------------
// 6. iOS Storage Widget (Responsive)
// -------------------------------------------------------------
export const WidgetStorage: React.FC<WidgetProps> = ({ tile }) => {
  const is1x1 = tile.w === 1 && tile.h === 1;

  if (is1x1) {
    return (
      <div className="w-full h-full min-h-0 flex flex-col justify-between p-3 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
        <div className="flex items-center justify-between text-[11px] font-bold text-zinc-300">
          <HardDrive className="w-3.5 h-3.5 text-purple-400" />
          <span>microSD</span>
        </div>
        <div className="my-auto text-center">
          <div className="text-2xl font-black tracking-tight text-white">
            189.4
          </div>
          <div className="text-[10px] text-zinc-400 font-semibold">
            ГБ свободно
          </div>
        </div>
        <div className="w-full h-2 rounded-full bg-black/50 overflow-hidden flex border border-white/10">
          <div className="bg-[#007aff] h-full" style={{ width: '60%' }} />
          <div className="bg-[#af52de] h-full" style={{ width: '20%' }} />
        </div>
      </div>
    );
  }

  return (
    <div className="w-full h-full min-h-0 flex flex-col justify-between p-3.5 md:p-4 select-none bg-gradient-to-b from-white/[0.08] to-transparent text-white overflow-hidden">
      {/* Header */}
      <div className="flex items-center justify-between">
        <span className="text-xs font-bold text-zinc-300 tracking-tight flex items-center gap-1.5">
          <HardDrive className="w-4 h-4 text-purple-400" />
          <span>Хранилище Switch</span>
        </span>
        <span className="text-xs font-bold text-white bg-white/10 px-2.5 py-0.5 rounded-full">
          512 ГБ
        </span>
      </div>

      {/* Main Stats */}
      <div className="my-auto space-y-2">
        <div>
          <div className="flex justify-between items-baseline mb-1">
            <span className="text-2xl md:text-3xl font-black tracking-tight">189.4 ГБ</span>
            <span className="text-xs text-zinc-400 font-medium">Свободно на microSD</span>
          </div>
          <div className="w-full h-3 rounded-full bg-black/50 overflow-hidden flex p-0.5 border border-white/15">
            <div className="bg-[#007aff] h-full rounded-l-full" style={{ width: '45%' }} title="Игры (230 ГБ)" />
            <div className="bg-[#af52de] h-full" style={{ width: '15%' }} title="Медиа (76 ГБ)" />
            <div className="bg-[#ff9500] h-full" style={{ width: '8%' }} title="Сохранения (40 ГБ)" />
            <div className="bg-transparent h-full flex-1" />
          </div>
        </div>

        {/* Legend */}
        <div className="grid grid-cols-3 gap-1 text-[11px] font-bold text-zinc-300 pt-0.5">
          <span className="flex items-center gap-1">
            <span className="w-2 h-2 rounded-full bg-[#007aff]" /> Игры (45%)
          </span>
          <span className="flex items-center gap-1">
            <span className="w-2 h-2 rounded-full bg-[#af52de]" /> Медиа (15%)
          </span>
          <span className="flex items-center gap-1">
            <span className="w-2 h-2 rounded-full bg-[#ff9500]" /> Сейвы (8%)
          </span>
        </div>
      </div>

      {/* Footer */}
      <div className="text-xs text-zinc-400 flex items-center justify-between border-t border-white/10 pt-1.5 font-medium">
        <span>Память NAND: 14.2 ГБ своб.</span>
        <span className="text-emerald-400 font-bold">Оптимизировано</span>
      </div>
    </div>
  );
};
