import React, { useState } from 'react';
import { 
  X, 
  Gamepad2, 
  RotateCcw, 
  Search, 
  Plus, 
  Volume2, 
  Battery, 
  BatteryCharging, 
  Check, 
  ChevronRight,
  Sparkles,
  Zap,
  Radio,
  SlidersHorizontal,
  Info
} from 'lucide-react';
import { sound } from '../data/mockCatalog';

interface ControllersModalProps {
  isOpen: boolean;
  onClose: () => void;
}

type TabType = 'main' | 'change_grip' | 'find' | 'pair';

interface ConnectedController {
  id: string;
  name: string;
  type: 'joycon_pair' | 'joycon_l' | 'joycon_r' | 'pro_controller';
  battery: number;
  isCharging: boolean;
  playerIndex: number; // 1-4
  colorL?: string;
  colorR?: string;
}

const INITIAL_CONTROLLERS: ConnectedController[] = [
  {
    id: 'joycon_handheld',
    name: 'Joy-Con (L) / Joy-Con (R)',
    type: 'joycon_pair',
    battery: 88,
    isCharging: false,
    playerIndex: 1,
    colorL: '#ff3b30',
    colorR: '#007aff',
  },
  {
    id: 'pro_controller_p2',
    name: 'Nintendo Switch Pro Controller',
    type: 'pro_controller',
    battery: 100,
    isCharging: true,
    playerIndex: 2,
  },
];

export const ControllersModal: React.FC<ControllersModalProps> = ({ isOpen, onClose }) => {
  const [currentTab, setCurrentTab] = useState<TabType>('main');
  const [controllers, setControllers] = useState<ConnectedController[]>(INITIAL_CONTROLLERS);
  const [vibratingId, setVibratingId] = useState<string | null>(null);
  const [gripPlayerAssignments, setGripPlayerAssignments] = useState<Record<number, string | null>>({
    1: 'joycon_handheld',
    2: 'pro_controller_p2',
    3: null,
    4: null,
  });

  if (!isOpen) return null;

  const handleClose = () => {
    sound.playBack();
    setCurrentTab('main');
    onClose();
  };

  // Trigger simulated HD Rumble vibration search
  const handleVibrateController = (id: string) => {
    sound.playClick();
    setVibratingId(id);
    if (navigator.vibrate) {
      navigator.vibrate([100, 50, 100, 50, 200]);
    }
    setTimeout(() => {
      setVibratingId(null);
    }, 1800);
  };

  // Change Grip Slot assignment
  const handleAssignSlot = (slot: number) => {
    sound.playSelect();
    setGripPlayerAssignments((prev) => ({
      ...prev,
      [slot]: prev[slot] ? null : 'joycon_handheld',
    }));
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-3 sm:p-6 bg-black/80 backdrop-blur-2xl animate-fade-in select-none">
      <div className="relative w-full max-w-4xl max-h-[92vh] flex flex-col bg-[#1e1e24] border border-white/20 rounded-[32px] shadow-[0_25px_60px_rgba(0,0,0,0.8)] overflow-hidden text-white">
        
        {/* Top Header Bar */}
        <div className="flex items-center justify-between px-6 py-4 bg-[#282830] border-b border-white/10 shrink-0">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-2xl bg-gradient-to-tr from-[#e60012] to-[#00b4e6] flex items-center justify-center text-white shadow-md">
              <Gamepad2 className="w-6 h-6" />
            </div>
            <div>
              <div className="text-xl font-black tracking-tight text-white flex items-center gap-2">
                <span>Контроллеры</span>
                <span className="text-xs font-mono font-normal text-zinc-400 bg-white/10 px-2 py-0.5 rounded-full">
                  Horizon OS
                </span>
              </div>
              <div className="text-xs text-zinc-400">
                {currentTab === 'main' && 'Управление подключенными Joy-Con и Pro Controller'}
                {currentTab === 'change_grip' && 'Изменить способ удерживания / порядок'}
                {currentTab === 'find' && 'Поиск контроллеров (HD Rumble)'}
                {currentTab === 'pair' && 'Подключение новых контроллеров'}
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2">
            {currentTab !== 'main' && (
              <button
                onClick={() => {
                  sound.playBack();
                  setCurrentTab('main');
                }}
                className="px-4 py-2 rounded-full bg-white/10 hover:bg-white/20 text-xs font-extrabold text-white transition-all"
              >
                ← Назад
              </button>
            )}
            <button
              onClick={handleClose}
              className="w-10 h-10 rounded-full bg-white/10 hover:bg-white/20 flex items-center justify-center text-zinc-300 hover:text-white transition-all active:scale-95"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Modal Body */}
        <div className="flex-1 overflow-y-auto p-6 space-y-6">

          {/* TAB 1: MAIN MENU (Authentic Switch Controllers Menu) */}
          {currentTab === 'main' && (
            <div className="space-y-6">
              
              {/* Primary Actions Grid */}
              <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                
                {/* 1. Change Grip/Order */}
                <button
                  onClick={() => {
                    sound.playSelect();
                    setCurrentTab('change_grip');
                  }}
                  className="group flex flex-col justify-between p-5 rounded-2xl bg-gradient-to-b from-white/10 to-white/5 hover:from-white/15 hover:to-white/10 border border-white/15 hover:border-cyan-400/50 shadow-md text-left transition-all active:scale-[0.98]"
                >
                  <div className="flex items-center justify-between w-full mb-3">
                    <div className="w-11 h-11 rounded-xl bg-cyan-500/20 text-cyan-400 flex items-center justify-center border border-cyan-400/30 group-hover:scale-105 transition-transform">
                      <RotateCcw className="w-6 h-6" />
                    </div>
                    <ChevronRight className="w-5 h-5 text-zinc-500 group-hover:text-white transition-colors" />
                  </div>
                  <div>
                    <h3 className="text-base font-extrabold text-white group-hover:text-cyan-300 transition-colors">
                      Изменить способ удерживания / порядок
                    </h3>
                    <p className="text-xs text-zinc-400 mt-1 leading-relaxed">
                      Нажмите L + R для назначения игроков (P1–P4) и выбора режима Joy-Con.
                    </p>
                  </div>
                </button>

                {/* 2. Find Controllers */}
                <button
                  onClick={() => {
                    sound.playSelect();
                    setCurrentTab('find');
                  }}
                  className="group flex flex-col justify-between p-5 rounded-2xl bg-gradient-to-b from-white/10 to-white/5 hover:from-white/15 hover:to-white/10 border border-white/15 hover:border-emerald-400/50 shadow-md text-left transition-all active:scale-[0.98]"
                >
                  <div className="flex items-center justify-between w-full mb-3">
                    <div className="w-11 h-11 rounded-xl bg-emerald-500/20 text-emerald-400 flex items-center justify-center border border-emerald-400/30 group-hover:scale-105 transition-transform">
                      <Search className="w-6 h-6" />
                    </div>
                    <ChevronRight className="w-5 h-5 text-zinc-500 group-hover:text-white transition-colors" />
                  </div>
                  <div>
                    <h3 className="text-base font-extrabold text-white group-hover:text-emerald-300 transition-colors">
                      Найти контроллеры
                    </h3>
                    <p className="text-xs text-zinc-400 mt-1 leading-relaxed">
                      Отправить вибрацию HD Rumble на потерянный Joy-Con или Pro Controller.
                    </p>
                  </div>
                </button>

                {/* 3. Pair New Controllers */}
                <button
                  onClick={() => {
                    sound.playSelect();
                    setCurrentTab('pair');
                  }}
                  className="group flex flex-col justify-between p-5 rounded-2xl bg-gradient-to-b from-white/10 to-white/5 hover:from-white/15 hover:to-white/10 border border-white/15 hover:border-rose-400/50 shadow-md text-left transition-all active:scale-[0.98]"
                >
                  <div className="flex items-center justify-between w-full mb-3">
                    <div className="w-11 h-11 rounded-xl bg-rose-500/20 text-rose-400 flex items-center justify-center border border-rose-400/30 group-hover:scale-105 transition-transform">
                      <Plus className="w-6 h-6" />
                    </div>
                    <ChevronRight className="w-5 h-5 text-zinc-500 group-hover:text-white transition-colors" />
                  </div>
                  <div>
                    <h3 className="text-base font-extrabold text-white group-hover:text-rose-300 transition-colors">
                      Подключить новые контроллеры
                    </h3>
                    <p className="text-xs text-zinc-400 mt-1 leading-relaxed">
                      Прикрепить по направляющим или зажать кнопку SYNC по Bluetooth.
                    </p>
                  </div>
                </button>

              </div>

              {/* Connected Controllers Status Bar (As shown at bottom of real Switch screen) */}
              <div className="bg-black/40 rounded-3xl p-5 border border-white/10">
                <div className="flex items-center justify-between mb-4 pb-2 border-b border-white/10">
                  <span className="text-xs font-black uppercase tracking-wider text-zinc-300 flex items-center gap-2">
                    <Radio className="w-4 h-4 text-emerald-400" />
                    Подключенные устройства
                  </span>
                  <span className="text-xs text-zinc-400 font-mono">
                    Всего: {controllers.length}
                  </span>
                </div>

                <div className="space-y-3">
                  {controllers.map((ctrl) => (
                    <div 
                      key={ctrl.id}
                      className="flex items-center justify-between p-3.5 rounded-2xl bg-white/5 border border-white/10 hover:bg-white/10 transition-colors"
                    >
                      {/* Left: Silhouette & Name */}
                      <div className="flex items-center gap-3.5">
                        {ctrl.type === 'joycon_pair' ? (
                          <div className="flex items-center gap-1 bg-black/60 px-2.5 py-1.5 rounded-xl border border-white/10">
                            <div className="w-3.5 h-6 rounded-l-[4px] bg-[#ff3b30] flex items-center justify-center text-[8px] font-black shadow-sm">
                              −
                            </div>
                            <div className="w-3.5 h-6 rounded-r-[4px] bg-[#007aff] flex items-center justify-center text-[8px] font-black shadow-sm">
                              +
                            </div>
                          </div>
                        ) : (
                          <div className="w-10 h-7 rounded-xl bg-zinc-800 flex items-center justify-center border border-white/10 text-zinc-300">
                            <Gamepad2 className="w-5 h-5 text-zinc-300" />
                          </div>
                        )}

                        <div>
                          <div className="text-sm font-extrabold text-white flex items-center gap-2">
                            <span>{ctrl.name}</span>
                            <span className="text-[10px] font-black px-2 py-0.5 rounded-full bg-blue-500/20 text-blue-300 border border-blue-400/20">
                              Игрок {ctrl.playerIndex}
                            </span>
                          </div>
                          <div className="text-xs text-zinc-400 flex items-center gap-2">
                            <span>Bluetooth Wireless</span>
                            <span>•</span>
                            <span className="text-emerald-400 font-medium">Подключен</span>
                          </div>
                        </div>
                      </div>

                      {/* Right: Battery Status */}
                      <div className="flex items-center gap-3">
                        <div className="flex items-center gap-2 bg-black/50 px-3 py-1.5 rounded-xl border border-white/10">
                          {ctrl.isCharging ? (
                            <Zap className="w-4 h-4 text-amber-300 fill-amber-300 animate-pulse" />
                          ) : (
                            <Battery className="w-4 h-4 text-emerald-400" />
                          )}
                          <span className="text-xs font-black font-mono">
                            {ctrl.battery}%
                          </span>
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              </div>

            </div>
          )}

          {/* TAB 2: CHANGE GRIP / ORDER (Authentic Switch UI) */}
          {currentTab === 'change_grip' && (
            <div className="space-y-6">
              <div className="text-center py-2 bg-gradient-to-r from-blue-950/40 via-cyan-900/30 to-blue-950/40 rounded-2xl border border-cyan-500/30 p-4">
                <p className="text-base font-extrabold text-white">
                  Нажмите <span className="px-2 py-0.5 rounded bg-white text-black font-black">L</span> + <span className="px-2 py-0.5 rounded bg-white text-black font-black">R</span> на контроллере, который хотите использовать.
                </p>
                <p className="text-xs text-cyan-200 mt-1">
                  Для одного Joy-Con в горизонтальном положении нажмите <span className="font-bold">SL + SR</span>.
                </p>
              </div>

              {/* 4 Player Slots */}
              <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                {[1, 2, 3, 4].map((slot) => {
                  const assigned = gripPlayerAssignments[slot];
                  return (
                    <div 
                      key={slot}
                      onClick={() => handleAssignSlot(slot)}
                      className={`h-48 rounded-2xl border flex flex-col justify-between p-4 cursor-pointer transition-all select-none ${
                        assigned 
                          ? 'bg-gradient-to-b from-cyan-950/50 to-black/60 border-cyan-400 shadow-[0_0_20px_rgba(6,182,212,0.25)]' 
                          : 'bg-black/30 border-white/10 hover:border-white/30'
                      }`}
                    >
                      <div className="flex items-center justify-between">
                        <span className="text-lg font-black text-zinc-400">P{slot}</span>
                        {assigned && (
                          <span className="w-2.5 h-2.5 rounded-full bg-cyan-400 animate-ping" />
                        )}
                      </div>

                      <div className="flex flex-col items-center justify-center my-auto">
                        {assigned ? (
                          <div className="flex flex-col items-center gap-2">
                            <div className="flex items-center gap-1 bg-black/60 px-3 py-2 rounded-xl border border-cyan-400/40">
                              <div className="w-4 h-7 rounded-l-[4px] bg-[#ff3b30] flex items-center justify-center text-[9px] font-black shadow-sm">
                                −
                              </div>
                              <div className="w-4 h-7 rounded-r-[4px] bg-[#007aff] flex items-center justify-center text-[9px] font-black shadow-sm">
                                +
                              </div>
                            </div>
                            <span className="text-xs font-black text-cyan-300">Подключен</span>
                          </div>
                        ) : (
                          <div className="flex flex-col items-center text-zinc-500">
                            <Gamepad2 className="w-8 h-8 opacity-40 mb-1" />
                            <span className="text-xs font-bold">Нажмите L + R</span>
                          </div>
                        )}
                      </div>

                      <div className="text-center text-[11px] font-mono text-zinc-400">
                        {assigned ? 'Игрок готов' : 'Ожидание...'}
                      </div>
                    </div>
                  );
                })}
              </div>

              {/* Confirm Done */}
              <div className="flex items-center justify-center pt-2">
                <button
                  onClick={() => {
                    sound.playSelect();
                    setCurrentTab('main');
                  }}
                  className="px-8 py-3 rounded-full bg-gradient-to-r from-cyan-500 to-blue-600 hover:from-cyan-400 hover:to-blue-500 text-white font-extrabold text-sm shadow-lg shadow-cyan-500/25 active:scale-95 transition-all flex items-center gap-2"
                >
                  <Check className="w-4 h-4" /> Готово (Нажмите A)
                </button>
              </div>
            </div>
          )}

          {/* TAB 3: FIND CONTROLLERS (Simulate Rumble) */}
          {currentTab === 'find' && (
            <div className="space-y-6">
              <div className="bg-emerald-950/30 border border-emerald-500/30 rounded-2xl p-4 text-center">
                <p className="text-sm font-extrabold text-emerald-300">
                  Нажмите на контроллер, чтобы послать вибросигнал высокой четкости HD Rumble.
                </p>
                <p className="text-xs text-zinc-300 mt-1">
                  Контроллер начнет пульсировать и издавать звук моторчика.
                </p>
              </div>

              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                {controllers.map((ctrl) => {
                  const isVibrating = vibratingId === ctrl.id;
                  return (
                    <div
                      key={ctrl.id}
                      onClick={() => handleVibrateController(ctrl.id)}
                      className={`p-5 rounded-2xl border cursor-pointer transition-all select-none flex items-center justify-between ${
                        isVibrating
                          ? 'bg-emerald-600/30 border-emerald-400 animate-pulse scale-[1.02] shadow-[0_0_30px_rgba(52,199,89,0.5)]'
                          : 'bg-white/5 hover:bg-white/10 border-white/15'
                      }`}
                    >
                      <div className="flex items-center gap-4">
                        <div className={`w-12 h-12 rounded-2xl flex items-center justify-center transition-all ${
                          isVibrating ? 'bg-emerald-400 text-black' : 'bg-black/50 text-zinc-300'
                        }`}>
                          <Volume2 className={`w-6 h-6 ${isVibrating ? 'animate-bounce' : ''}`} />
                        </div>
                        <div>
                          <div className="text-base font-extrabold text-white">
                            {ctrl.name}
                          </div>
                          <div className="text-xs text-zinc-400">
                            {isVibrating ? 'Вибрация отправлена...' : 'Нажмите для поиска'}
                          </div>
                        </div>
                      </div>

                      <div className="text-right">
                        <span className={`text-xs font-black px-3 py-1.5 rounded-full ${
                          isVibrating ? 'bg-emerald-400 text-black' : 'bg-white/10 text-white'
                        }`}>
                          {isVibrating ? 'ВИБРАЦИЯ' : 'Поиск'}
                        </span>
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          )}

          {/* TAB 4: PAIR NEW CONTROLLERS */}
          {currentTab === 'pair' && (
            <div className="space-y-6">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-5">
                
                {/* Method 1: Joy-Con Rails */}
                <div className="bg-white/5 rounded-2xl p-5 border border-white/15 flex flex-col justify-between">
                  <div>
                    <span className="text-xs font-black uppercase text-rose-400 tracking-wider">
                      Способ 1: Направляющие консоли
                    </span>
                    <h4 className="text-base font-extrabold text-white mt-1">
                      Прикрепить Joy-Con к корпусу
                    </h4>
                    <p className="text-xs text-zinc-300 mt-2 leading-relaxed">
                      Вставьте Joy-Con (L) и Joy-Con (R) в направляющие консоли Nintendo Switch до характерного щелчка. Сопряжение произойдет мгновенно и автоматически.
                    </p>
                  </div>
                  <div className="mt-4 pt-3 border-t border-white/10 flex items-center gap-2 text-xs font-bold text-zinc-400">
                    <Check className="w-4 h-4 text-emerald-400" /> Автоматическая синхронизация
                  </div>
                </div>

                {/* Method 2: SYNC button (Bluetooth) */}
                <div className="bg-white/5 rounded-2xl p-5 border border-white/15 flex flex-col justify-between">
                  <div>
                    <span className="text-xs font-black uppercase text-blue-400 tracking-wider">
                      Способ 2: Кнопка SYNC (Bluetooth)
                    </span>
                    <h4 className="text-base font-extrabold text-white mt-1">
                      Беспроводное сопряжение
                    </h4>
                    <p className="text-xs text-zinc-300 mt-2 leading-relaxed">
                      Нажмите и удерживайте круглую кнопку SYNC на внутренней стороне Joy-Con или на верхней грани Pro Controller в течение 3 секунд, пока индикаторы игроков не начнут бегать.
                    </p>
                  </div>
                  <div className="mt-4 pt-3 border-t border-white/10 flex items-center gap-2 text-xs font-bold text-zinc-400">
                    <Radio className="w-4 h-4 text-cyan-400 animate-pulse" /> Поиск устройств Bluetooth...
                  </div>
                </div>

              </div>

              {/* Instructions Callout */}
              <div className="bg-black/40 rounded-2xl p-4 border border-white/10 flex items-center gap-3">
                <Info className="w-5 h-5 text-zinc-400 shrink-0" />
                <p className="text-xs text-zinc-300 leading-normal">
                  К консоли Nintendo Switch можно одновременно подключить до 8 беспроводных контроллеров (или до 4 пар Joy-Con).
                </p>
              </div>
            </div>
          )}

        </div>

        {/* Footer Bar */}
        <div className="px-6 py-3.5 bg-[#282830] border-t border-white/10 flex items-center justify-between text-xs text-zinc-400 shrink-0">
          <div className="flex items-center gap-3">
            <span>Режим питания: <span className="text-emerald-400 font-bold">Оптимальный</span></span>
            <span>•</span>
            <span>Опрос датчиков: <span className="text-white font-bold">1000 Гц (USB/BT)</span></span>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-zinc-500">Закрыть:</span>
            <button 
              onClick={handleClose}
              className="px-3 py-1 rounded bg-white/15 hover:bg-white/25 text-white font-black text-xs transition-colors"
            >
              B Назад
            </button>
          </div>
        </div>

      </div>
    </div>
  );
};
