import React, { useState } from 'react';
import { Candidate, Workspace } from '../types/desknx';
import { sound } from '../data/mockCatalog';
import { 
  X, 
  Play, 
  Trash2, 
  Archive, 
  HardDrive, 
  RefreshCw, 
  Cloud, 
  ShieldCheck, 
  Info, 
  CheckCircle2, 
  Wifi, 
  FolderSync, 
  ArrowRightLeft, 
  Sliders, 
  Sparkles,
  Gamepad2,
  AlertTriangle
} from 'lucide-react';

interface SoftwareOptionsModalProps {
  isOpen: boolean;
  onClose: () => void;
  candidate: Candidate | null;
  onLaunch?: (candidate: Candidate) => void;
  onLaunchCandidate?: (candidate: Candidate) => void;
  workspaces?: Workspace[];
  activeWorkspaceIndex?: number;
  onPinToWorkspace?: (candidate: Candidate, deskIndex: number) => void;
}

export const SoftwareOptionsModal: React.FC<SoftwareOptionsModalProps> = ({
  isOpen,
  onClose,
  candidate,
  onLaunch,
  onLaunchCandidate,
  workspaces = [],
  activeWorkspaceIndex = 0,
  onPinToWorkspace,
}) => {
  const [activeTab, setActiveTab] = useState<'info' | 'data' | 'update' | 'cloud' | 'custom'>('info');
  const [isScanning, setIsScanning] = useState(false);
  const [scanProgress, setScanProgress] = useState(0);
  const [scanResult, setScanResult] = useState<string | null>(null);
  const [isUpdating, setIsUpdating] = useState(false);
  const [updateResult, setUpdateResult] = useState<string | null>(null);
  const [cloudSyncStatus, setCloudSyncStatus] = useState<'synced' | 'backing_up'>('synced');
  const [pinNotice, setPinNotice] = useState<string | null>(null);

  if (!isOpen || !candidate) return null;

  const hours = Math.floor(candidate.playtimeSeconds / 3600);
  const playtimeFormatted = hours > 0 ? `${hours} ч. сыграно` : 'Менее часа';

  // Simulate scanning for corrupt data (Nintendo Switch official verification)
  const handleStartDataScan = () => {
    sound.playSelect();
    setIsScanning(true);
    setScanProgress(0);
    setScanResult(null);

    let current = 0;
    const interval = setInterval(() => {
      current += 15;
      if (current >= 100) {
        clearInterval(interval);
        setScanProgress(100);
        setIsScanning(false);
        sound.playSelect();
        setScanResult('Ошибок в данных программы не обнаружено. Хэши NCA / ROMFS верифицированы.');
      } else {
        setScanProgress(current);
      }
    }, 180);
  };

  // Simulate version check via internet
  const handleCheckUpdate = () => {
    sound.playSelect();
    setIsUpdating(true);
    setUpdateResult(null);

    setTimeout(() => {
      setIsUpdating(false);
      sound.playSelect();
      setUpdateResult('Установлена последняя доступная версия программы (v1.6.0). Обновления не требуются.');
    }, 1200);
  };

  const handleBackupCloud = () => {
    sound.playSelect();
    setCloudSyncStatus('backing_up');
    setTimeout(() => {
      sound.playSelect();
      setCloudSyncStatus('synced');
      alert(`Резервная копия сохранений "${candidate.title}" успешно сохранена в /switch/JKSV/backups/`);
    }, 1000);
  };

  const handlePinDesk = (deskIdx: number) => {
    if (!onPinToWorkspace) return;
    sound.playSelect();
    onPinToWorkspace(candidate, deskIdx);
    setPinNotice(`Закреплено на странице: ${workspaces[deskIdx]?.name || deskIdx + 1}`);
    setTimeout(() => setPinNotice(null), 2500);
  };

  return (
    <div 
      className="fixed inset-0 z-50 flex items-center justify-center p-3 md:p-6 bg-black/85 backdrop-blur-2xl animate-in fade-in duration-200 select-none text-zinc-100"
      onClick={onClose}
    >
      <div 
        className="w-full max-w-4xl bg-[#141419]/95 border border-white/15 rounded-[32px] shadow-[0_30px_90px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col max-h-[90vh] animate-in zoom-in-95 duration-150"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Top Header: App Hero Info */}
        <div className="relative px-6 py-5 border-b border-white/10 bg-white/[0.03] flex items-center justify-between">
          <div className="flex items-center gap-4 min-w-0">
            {candidate.icon ? (
              <div className="w-16 h-16 rounded-[20px] overflow-hidden bg-black/50 border border-white/20 shrink-0 shadow-lg">
                <img src={candidate.icon} alt={candidate.title} className="w-full h-full object-cover" />
              </div>
            ) : (
              <div className="w-16 h-16 rounded-[20px] bg-blue-900/50 border border-blue-400/30 flex items-center justify-center text-blue-400 shrink-0">
                <Gamepad2 className="w-8 h-8" />
              </div>
            )}

            <div className="min-w-0">
              <div className="flex items-center gap-2">
                <h2 className="text-xl md:text-2xl font-black text-white tracking-tight truncate">
                  {candidate.title}
                </h2>
                <span className="px-2.5 py-0.5 rounded-full text-[10px] font-bold bg-white/10 text-zinc-300 border border-white/15 shrink-0">
                  v1.6.0
                </span>
              </div>
              <div className="flex items-center gap-3 mt-1 text-xs text-zinc-400">
                <span className="text-white/80 font-medium">{candidate.author || 'Nintendo'}</span>
                <span>•</span>
                <span className="text-white/70 font-medium">{playtimeFormatted}</span>
                <span>•</span>
                <span className="text-zinc-500 font-mono text-[11px] truncate">{candidate.data || candidate.id}</span>
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={() => {
                sound.playLaunch();
                if (onLaunchCandidate) onLaunchCandidate(candidate);
                else if (onLaunch) onLaunch(candidate);
                onClose();
              }}
              className="hidden sm:flex items-center gap-1.5 px-5 py-2.5 rounded-full bg-[#007aff] hover:bg-[#0062cc] text-white font-extrabold text-sm shadow-[0_8px_20px_rgba(0,122,255,0.4)] active:scale-95 transition-all"
            >
              <Play className="w-4 h-4 fill-current" />
              <span>Запустить</span>
            </button>
            <button
              onClick={() => {
                sound.playBack();
                onClose();
              }}
              className="p-2 rounded-full hover:bg-white/10 text-zinc-400 hover:text-white transition-colors"
            >
              <X className="w-6 h-6" />
            </button>
          </div>
        </div>

        {/* Pin Notification Banner */}
        {pinNotice && (
          <div className="px-6 py-2.5 bg-emerald-950/80 border-b border-emerald-500/30 text-emerald-300 text-xs font-bold flex items-center gap-2">
            <CheckCircle2 className="w-4 h-4" />
            <span>{pinNotice}</span>
          </div>
        )}

        {/* Master Body: Sidebar Tabs + Content Area */}
        <div className="flex-1 flex flex-col md:flex-row min-h-0 overflow-hidden">
          {/* Left Navigation Sidebar */}
          <div className="w-full md:w-64 bg-black/35 border-b md:border-b-0 md:border-r border-white/10 p-3 space-y-1 shrink-0 overflow-y-auto scrollbar-thin">
            {[
              { id: 'info', label: 'Информация о программе', icon: Info },
              { id: 'data', label: 'Управление данными', icon: HardDrive },
              { id: 'update', label: 'Обновление программы', icon: RefreshCw },
              { id: 'cloud', label: 'Сохранения и бэкап', icon: Cloud },
              { id: 'custom', label: 'Размещение в DeskNX', icon: Sliders },
            ].map((tab) => {
              const Icon = tab.icon;
              const isActive = activeTab === tab.id;
              return (
                <button
                  key={tab.id}
                  onClick={() => {
                    sound.playNav();
                    setActiveTab(tab.id as any);
                  }}
                  className={`w-full flex items-center gap-3 px-3.5 py-3 rounded-2xl text-xs md:text-sm font-bold transition-all text-left ${
                    isActive
                      ? 'bg-white text-black shadow-lg font-black'
                      : 'text-zinc-400 hover:text-white hover:bg-white/10'
                  }`}
                >
                  <Icon className={`w-4 h-4 shrink-0 ${isActive ? 'text-black' : 'text-zinc-400'}`} />
                  <span className="truncate">{tab.label}</span>
                </button>
              );
            })}
          </div>

          {/* Right Tab Content View */}
          <div className="flex-1 p-5 md:p-7 overflow-y-auto scrollbar-thin text-zinc-200 space-y-6">
            {/* 1. Tab: Информация о программе */}
            {activeTab === 'info' && (
              <div className="space-y-5 animate-in fade-in duration-150">
                <h3 className="text-base font-black text-white flex items-center gap-2">
                  <Info className="w-5 h-5 text-blue-400" /> Основные сведения о программе
                </h3>

                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3.5">
                  <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10">
                    <span className="text-[11px] font-bold text-zinc-400 uppercase tracking-wider">Формат релиза</span>
                    <div className="text-sm font-extrabold text-white mt-1">
                      {candidate.kind === 'Game' ? 'NSP / Cartridge (eShop)' : 'Homebrew Executable (NRO)'}
                    </div>
                  </div>

                  <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10">
                    <span className="text-[11px] font-bold text-zinc-400 uppercase tracking-wider">Место хранения</span>
                    <div className="text-sm font-extrabold text-white mt-1 flex items-center gap-2">
                      <span className="w-2 h-2 rounded-full bg-emerald-400" />
                      Карта памяти microSD (14.2 ГБ)
                    </div>
                  </div>

                  <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10">
                    <span className="text-[11px] font-bold text-zinc-400 uppercase tracking-wider">Title ID</span>
                    <div className="text-xs font-mono font-bold text-zinc-300 mt-1 select-all">
                      {candidate.data || candidate.id}
                    </div>
                  </div>

                  <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10">
                    <span className="text-[11px] font-bold text-zinc-400 uppercase tracking-wider">Время в игре</span>
                    <div className="text-sm font-extrabold text-white/80 mt-1">
                      {playtimeFormatted}
                    </div>
                  </div>
                </div>

                <div className="p-4 rounded-2xl bg-white/[0.03] border border-white/10 text-xs text-zinc-400 space-y-1.5">
                  <div className="font-bold text-zinc-300">Совместимость с контроллерами:</div>
                  <div>• Joy-Con (L/R) раздельно и в держателе</div>
                  <div>• Контроллер Nintendo Switch Pro</div>
                  <div>• Портативный режим консоли (сенсорный ввод 720p 60 FPS)</div>
                </div>
              </div>
            )}

            {/* 2. Tab: Управление данными (Data Management) */}
            {activeTab === 'data' && (
              <div className="space-y-4 animate-in fade-in duration-150">
                <h3 className="text-base font-black text-white flex items-center gap-2">
                  <HardDrive className="w-5 h-5 text-emerald-400" /> Управление данными программы
                </h3>

                {/* Corrupt Data Scan Action */}
                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 space-y-3">
                  <div className="flex items-center justify-between">
                    <div>
                      <div className="text-sm font-bold text-white">Проверить данные на наличие ошибок</div>
                      <div className="text-xs text-zinc-400 mt-0.5">
                        Проверка целостности ROMFS / NCAs и проверка контрольных сумм файлов
                      </div>
                    </div>
                    <button
                      onClick={handleStartDataScan}
                      disabled={isScanning}
                      className="px-4 py-2 rounded-xl bg-white/10 hover:bg-white/20 text-xs font-bold text-white border border-white/15 transition-all shrink-0 ml-3"
                    >
                      {isScanning ? 'Проверка...' : 'Проверить'}
                    </button>
                  </div>

                  {isScanning && (
                    <div className="space-y-1.5 pt-2">
                      <div className="flex justify-between text-xs font-mono text-zinc-300">
                        <span>Сканирование пакетов...</span>
                        <span>{scanProgress}%</span>
                      </div>
                      <div className="w-full h-2 rounded-full bg-black/60 overflow-hidden border border-white/10">
                        <div 
                          className="h-full bg-[#007aff] transition-all duration-150"
                          style={{ width: `${scanProgress}%` }}
                        />
                      </div>
                    </div>
                  )}

                  {scanResult && (
                    <div className="p-3 rounded-xl bg-emerald-500/20 border border-emerald-400/30 text-emerald-300 text-xs font-bold flex items-center gap-2">
                      <ShieldCheck className="w-4 h-4 shrink-0" />
                      <span>{scanResult}</span>
                    </div>
                  )}
                </div>

                {/* Move Data Storage */}
                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 flex items-center justify-between">
                  <div>
                    <div className="text-sm font-bold text-white">Перенести данные программы</div>
                    <div className="text-xs text-zinc-400 mt-0.5">
                      Перенос между картой памяти microSD и системной памятью eMMC
                    </div>
                  </div>
                  <button
                    onClick={() => {
                      sound.playSelect();
                      alert('Данные программы уже находятся на карте памяти microSD.');
                    }}
                    className="flex items-center gap-1.5 px-4 py-2 rounded-xl bg-white/10 hover:bg-white/20 text-xs font-bold text-white border border-white/15 transition-all shrink-0 ml-3"
                  >
                    <ArrowRightLeft className="w-3.5 h-3.5 text-blue-400" />
                    <span>Перенести</span>
                  </button>
                </div>

                {/* Quick Archive Software */}
                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 flex items-center justify-between">
                  <div>
                    <div className="text-sm font-bold text-white">Быстрая архивация</div>
                    <div className="text-xs text-zinc-400 mt-0.5">
                      Освобождает место на диске. Значок программы и данные сохранений остаются.
                    </div>
                  </div>
                  <button
                    onClick={() => {
                      sound.playClick();
                      if (confirm(`Архивировать программу "${candidate.title}"? Данные сохранений будут сохранены.`)) {
                        sound.playSelect();
                        alert(`Программа "${candidate.title}" архивирована.`);
                      }
                    }}
                    className="flex items-center gap-1.5 px-4 py-2 rounded-xl bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 border border-amber-500/30 text-xs font-bold transition-all shrink-0 ml-3"
                  >
                    <Archive className="w-3.5 h-3.5" />
                    <span>Архивировать</span>
                  </button>
                </div>

                {/* Delete Software */}
                <div className="p-4 rounded-2xl bg-rose-500/10 border border-rose-500/25 flex items-center justify-between">
                  <div>
                    <div className="text-sm font-bold text-rose-300">Удалить программу</div>
                    <div className="text-xs text-rose-200/70 mt-0.5">
                      Программа и её значок будут удалены с консоли. Данные сохранений не будут затронуты.
                    </div>
                  </div>
                  <button
                    onClick={() => {
                      sound.playBack();
                      if (confirm(`Вы действительно хотите полностью удалить "${candidate.title}"?`)) {
                        alert(`Программа "${candidate.title}" удалена.`);
                      }
                    }}
                    className="flex items-center gap-1.5 px-4 py-2 rounded-xl bg-rose-600 hover:bg-rose-700 text-white text-xs font-bold transition-all shadow-md shrink-0 ml-3"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                    <span>Удалить</span>
                  </button>
                </div>
              </div>
            )}

            {/* 3. Tab: Обновление программы */}
            {activeTab === 'update' && (
              <div className="space-y-4 animate-in fade-in duration-150">
                <h3 className="text-base font-black text-white flex items-center gap-2">
                  <RefreshCw className="w-5 h-5 text-cyan-400" /> Обновление версии программы
                </h3>

                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 space-y-3">
                  <div className="flex items-center justify-between">
                    <div>
                      <div className="text-sm font-bold text-white">Через Интернет</div>
                      <div className="text-xs text-zinc-400 mt-0.5">
                        Поиск последних патчей и обновлений контента на серверах
                      </div>
                    </div>
                    <button
                      onClick={handleCheckUpdate}
                      disabled={isUpdating}
                      className="flex items-center gap-1.5 px-4 py-2 rounded-xl bg-[#007aff] hover:bg-[#0062cc] text-xs font-bold text-white transition-all shrink-0 ml-3 shadow-md"
                    >
                      <Wifi className="w-3.5 h-3.5" />
                      <span>{isUpdating ? 'Поиск...' : 'Проверить'}</span>
                    </button>
                  </div>

                  {updateResult && (
                    <div className="p-3 rounded-xl bg-emerald-500/20 border border-emerald-400/30 text-emerald-300 text-xs font-bold flex items-center gap-2">
                      <CheckCircle2 className="w-4 h-4 shrink-0" />
                      <span>{updateResult}</span>
                    </div>
                  )}
                </div>

                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 flex items-center justify-between">
                  <div>
                    <div className="text-sm font-bold text-white">Согласовать версию с пользователями поблизости</div>
                    <div className="text-xs text-zinc-400 mt-0.5">
                      Прямая передача данных обновления между консолями Nintendo Switch по локальной беспроводной сети (LDN)
                    </div>
                  </div>
                  <button
                    onClick={() => {
                      sound.playSelect();
                      alert('Создана локальная группа согласования версий поблизости...');
                    }}
                    className="flex items-center gap-1.5 px-4 py-2 rounded-xl bg-white/10 hover:bg-white/20 text-xs font-bold text-white border border-white/15 transition-all shrink-0 ml-3"
                  >
                    <FolderSync className="w-3.5 h-3.5 text-cyan-400" />
                    <span>Создать группу</span>
                  </button>
                </div>
              </div>
            )}

            {/* 4. Tab: Сохранения и бэкап */}
            {activeTab === 'cloud' && (
              <div className="space-y-4 animate-in fade-in duration-150">
                <h3 className="text-base font-black text-white flex items-center gap-2">
                  <Cloud className="w-5 h-5 text-blue-400" /> Резервное копирование и сохраненные данные
                </h3>

                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 space-y-3">
                  <div className="flex items-center justify-between">
                    <div>
                      <div className="text-sm font-bold text-white">Локальный бэкап (JKSV / Checkpoint)</div>
                      <div className="text-xs text-zinc-400 mt-0.5">
                        Создать полную копию сохранений на карте памяти microSD
                      </div>
                    </div>
                    <button
                      onClick={handleBackupCloud}
                      className="px-4 py-2 rounded-xl bg-[#007aff] hover:bg-[#0062cc] text-xs font-bold text-white transition-all shrink-0 ml-3 shadow-md"
                    >
                      {cloudSyncStatus === 'backing_up' ? 'Копирование...' : 'Создать бэкап'}
                    </button>
                  </div>
                  <div className="text-[11px] text-zinc-400 font-mono bg-black/40 p-2.5 rounded-xl border border-white/10">
                    Путь: /switch/JKSV/{candidate.title.replace(/[^a-zA-Z0-9]/g, '_')}/
                  </div>
                </div>

                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 flex items-center justify-between">
                  <div>
                    <div className="text-sm font-bold text-white">Статус целостности сохранений</div>
                    <div className="text-xs text-emerald-400 font-bold mt-0.5 flex items-center gap-1.5">
                      <CheckCircle2 className="w-3.5 h-3.5" />
                      Файлы сохранений в норме (последнее сохранение сегодня)
                    </div>
                  </div>
                </div>
              </div>
            )}

            {/* 5. Tab: Размещение в DeskNX */}
            {activeTab === 'custom' && (
              <div className="space-y-4 animate-in fade-in duration-150">
                <h3 className="text-base font-black text-white flex items-center gap-2">
                  <Sparkles className="w-5 h-5 text-amber-400" /> Размещение на рабочих столах DeskNX
                </h3>

                <div className="p-4 rounded-2xl bg-white/[0.04] border border-white/10 space-y-3">
                  <div className="text-sm font-bold text-white">Закрепить на рабочем столе:</div>
                  <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
                    {workspaces.map((desk, idx) => (
                      <button
                        key={desk.id}
                        onClick={() => handlePinDesk(idx)}
                        className={`p-3 rounded-xl border text-left flex items-center justify-between transition-all ${
                          idx === activeWorkspaceIndex
                            ? 'bg-blue-600/20 border-blue-400/40 text-white'
                            : 'bg-black/30 border-white/10 text-zinc-300 hover:bg-white/10'
                        }`}
                      >
                        <span className="font-bold text-xs truncate">{desk.name}</span>
                        <span className="text-[10px] font-mono text-zinc-400">
                          {idx === activeWorkspaceIndex ? 'Текущий стол' : `Стол #${idx + 1}`}
                        </span>
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Nintendo Switch Standard Controller Footer Bar */}
        <div className="px-6 py-3.5 bg-black/60 border-t border-white/10 flex items-center justify-between text-xs text-zinc-400">
          <div className="flex items-center gap-4">
            <span><strong className="text-white font-bold bg-white/20 px-1.5 py-0.5 rounded-full mr-1">A</strong> Выбрать</span>
            <span><strong className="text-white font-bold bg-white/20 px-1.5 py-0.5 rounded-full mr-1">B</strong> Назад</span>
            <span><strong className="text-white font-bold bg-white/20 px-1.5 py-0.5 rounded-full mr-1">X</strong> Запуск</span>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={() => {
                sound.playLaunch();
                if (onLaunchCandidate) onLaunchCandidate(candidate);
                else if (onLaunch) onLaunch(candidate);
                onClose();
              }}
              className="px-4 py-1.5 rounded-full bg-[#007aff] hover:bg-[#0062cc] text-white font-bold text-xs shadow-md transition-all active:scale-95"
            >
              Запустить
            </button>
            <button
              onClick={() => {
                sound.playBack();
                onClose();
              }}
              className="px-4 py-1.5 rounded-full bg-white/10 hover:bg-white/20 text-white font-bold text-xs transition-all"
            >
              Закрыть
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
