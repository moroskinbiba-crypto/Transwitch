import React, { useState } from 'react';
import { 
  X, 
  Image as ImageIcon, 
  Film, 
  Trash2, 
  Share2, 
  Maximize2, 
  Calendar,
  Gamepad2,
  Check
} from 'lucide-react';
import { sound } from '../data/mockCatalog';

import { notify } from '../ui/toast';
interface ScreenshotItem {
  id: string;
  game: string;
  title: string;
  date: string;
  imageUrl: string;
  isVideo?: boolean;
  duration?: string;
}

const DEFAULT_SCREENSHOTS: ScreenshotItem[] = [
  {
    id: 'shot_1',
    game: 'The Legend of Zelda: Tears of the Kingdom',
    title: 'Парение над Хайрулом на планере',
    date: '30.10.2026, 12:45',
    imageUrl: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=1200&auto=format&fit=crop&q=80',
  },
  {
    id: 'shot_2',
    game: 'Super Mario Odyssey',
    title: 'Лунный город и прыжок с кепкой',
    date: '28.10.2026, 19:20',
    imageUrl: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=1200&auto=format&fit=crop&q=80',
  },
  {
    id: 'shot_3',
    game: 'Metroid Dread',
    title: 'Встреча с роботом E.M.M.I.',
    date: '25.10.2026, 22:15',
    imageUrl: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=1200&auto=format&fit=crop&q=80',
    isVideo: true,
    duration: '0:30',
  },
  {
    id: 'shot_4',
    game: 'Animal Crossing: New Horizons',
    title: 'Праздник урожая на острове',
    date: '20.10.2026, 14:10',
    imageUrl: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?w=1200&auto=format&fit=crop&q=80',
  },
  {
    id: 'shot_5',
    game: 'Super Smash Bros. Ultimate',
    title: 'Финальный удар 4-х игроков',
    date: '18.10.2026, 21:05',
    imageUrl: 'https://images.unsplash.com/photo-1538481199705-c710c4e965fc?w=1200&auto=format&fit=crop&q=80',
    isVideo: true,
    duration: '0:30',
  },
  {
    id: 'shot_6',
    game: 'Mario Kart 8 Deluxe',
    title: 'Победа на Радужной трассе',
    date: '15.10.2026, 16:40',
    imageUrl: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=1200&auto=format&fit=crop&q=80',
  },
];

interface AlbumModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export const AlbumModal: React.FC<AlbumModalProps> = ({ isOpen, onClose }) => {
  const [items, setItems] = useState<ScreenshotItem[]>(DEFAULT_SCREENSHOTS);
  const [selectedItem, setSelectedItem] = useState<ScreenshotItem | null>(null);
  const [filter, setFilter] = useState<'all' | 'screenshots' | 'videos'>('all');

  if (!isOpen) return null;

  const filtered = items.filter((item) => {
    if (filter === 'screenshots') return !item.isVideo;
    if (filter === 'videos') return item.isVideo;
    return true;
  });

  const handleDelete = (id: string, e: React.MouseEvent) => {
    e.stopPropagation();
    sound.playBack();
    const updated = items.filter((i) => i.id !== id);
    setItems(updated);
    if (selectedItem?.id === id) {
      setSelectedItem(null);
    }
  };

  return (
    <div 
      className="fixed inset-0 z-50 flex items-center justify-center p-4 md:p-8 bg-black/85 backdrop-blur-2xl animate-in fade-in duration-200 select-none text-white font-sans"
      onClick={onClose}
    >
      <div 
        className="w-full max-w-5xl h-[85vh] bg-[#14141c]/95 backdrop-blur-3xl border border-white/15 rounded-[32px] shadow-[0_30px_90px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col animate-in zoom-in-95 duration-150"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="px-6 py-4 border-b border-white/10 flex items-center justify-between bg-white/[0.03]">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-2xl bg-gradient-to-tr from-blue-600 to-cyan-400 flex items-center justify-center text-white shadow-md">
              <ImageIcon className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-lg md:text-xl font-extrabold text-white tracking-tight">
                Альбом Nintendo Switch
              </h2>
              <p className="text-xs text-zinc-400">
                Скриншоты и видеозаписи с карты microSD
              </p>
            </div>
          </div>

          {/* Filter pills */}
          <div className="flex items-center gap-2">
            <div className="flex items-center gap-1 bg-black/40 p-1 rounded-full border border-white/10">
              {[
                { id: 'all', label: `Все (${items.length})` },
                { id: 'screenshots', label: 'Фото' },
                { id: 'videos', label: 'Видео 30с' },
              ].map((tab) => (
                <button
                  key={tab.id}
                  onClick={() => {
                    sound.playClick();
                    setFilter(tab.id as any);
                  }}
                  className={`px-3 py-1 rounded-full text-xs font-bold transition-all ${
                    filter === tab.id
                      ? 'bg-white text-black shadow'
                      : 'text-zinc-400 hover:text-white'
                  }`}
                >
                  {tab.label}
                </button>
              ))}
            </div>

            <button
              onClick={() => {
                sound.playBack();
                onClose();
              }}
              className="p-2 rounded-full hover:bg-white/10 text-zinc-400 hover:text-white transition-all ml-2"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Grid Content */}
        <div className="flex-1 overflow-y-auto p-6 scrollbar-thin">
          {filtered.length === 0 ? (
            <div className="h-full flex flex-col items-center justify-center text-zinc-500">
              <ImageIcon className="w-16 h-16 mb-2 opacity-40" />
              <p className="text-sm font-semibold">Нет сохраненных материалов</p>
            </div>
          ) : (
            <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-3 gap-4">
              {filtered.map((item) => (
                <div
                  key={item.id}
                  onClick={() => {
                    sound.playSelect();
                    setSelectedItem(item);
                  }}
                  className="group relative aspect-video rounded-2xl overflow-hidden bg-black/60 border border-white/15 cursor-pointer hover:border-cyan-400 hover:scale-[1.02] transition-all shadow-lg"
                >
                  <img
                    src={item.imageUrl}
                    alt={item.title}
                    className="w-full h-full object-cover group-hover:brightness-110 transition-all"
                  />
                  {/* Video Badge */}
                  {item.isVideo && (
                    <div className="absolute top-2 left-2 bg-black/70 backdrop-blur-md px-2 py-0.5 rounded-full text-[10px] font-bold text-amber-300 flex items-center gap-1 border border-white/20">
                      <Film className="w-3 h-3" />
                      <span>{item.duration}</span>
                    </div>
                  )}

                  {/* Gradient Overlay */}
                  <div className="absolute inset-0 bg-gradient-to-t from-black/90 via-black/30 to-transparent flex flex-col justify-end p-3 opacity-90 group-hover:opacity-100 transition-opacity">
                    <div className="text-xs font-bold text-white truncate drop-shadow">
                      {item.title}
                    </div>
                    <div className="text-[10px] text-zinc-400 flex items-center justify-between mt-0.5">
                      <span className="truncate">{item.game}</span>
                      <span className="shrink-0">{item.date.split(',')[0]}</span>
                    </div>
                  </div>

                  {/* Delete button on hover */}
                  <button
                    onClick={(e) => handleDelete(item.id, e)}
                    className="absolute top-2 right-2 p-1.5 rounded-full bg-black/70 hover:bg-red-600 text-white opacity-0 group-hover:opacity-100 transition-opacity border border-white/20"
                    title="Удалить снимок"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-3 border-t border-white/10 bg-black/50 flex items-center justify-between text-xs text-zinc-400">
          <div className="flex items-center gap-4">
            <span>Снимков на microSD: <strong className="text-white">{items.length}</strong></span>
            <span>Свободно: <strong className="text-cyan-400">189.4 ГБ</strong></span>
          </div>
          <div className="text-zinc-500">
            Кнопка снимка экрана: ⯀ на левом Joy-Con
          </div>
        </div>
      </div>

      {/* Fullscreen Photo Lightbox */}
      {selectedItem && (
        <div 
          className="fixed inset-0 z-60 bg-black/95 flex flex-col animate-in fade-in duration-150"
          onClick={() => setSelectedItem(null)}
        >
          {/* Lightbox Header */}
          <div className="px-6 py-4 flex items-center justify-between border-b border-white/10 bg-black/60">
            <div className="flex flex-col">
              <span className="text-sm font-bold text-white">{selectedItem.title}</span>
              <span className="text-xs text-zinc-400">{selectedItem.game} · {selectedItem.date}</span>
            </div>
            <button
              onClick={() => setSelectedItem(null)}
              className="p-2 rounded-full bg-white/10 hover:bg-white/20 text-white"
            >
              <X className="w-5 h-5" />
            </button>
          </div>

          {/* Lightbox Image */}
          <div className="flex-1 flex items-center justify-center p-4">
            <img
              src={selectedItem.imageUrl}
              alt={selectedItem.title}
              className="max-w-full max-h-[75vh] object-contain rounded-2xl shadow-2xl border border-white/15"
            />
          </div>

          {/* Lightbox Controls */}
          <div className="px-6 py-4 border-t border-white/10 flex items-center justify-center gap-4 bg-black/60">
            <button 
              onClick={(e) => {
                e.stopPropagation();
                sound.playClick();
                notify('Снимок скопирован в буфер обмена для отправки на смартфон!');
              }}
              className="flex items-center gap-2 px-4 py-2 rounded-full bg-white/10 hover:bg-white/20 text-xs font-bold text-white transition-all"
            >
              <Share2 className="w-4 h-4" /> Отправить на смартфон (QR)
            </button>
            <button
              onClick={(e) => {
                e.stopPropagation();
                handleDelete(selectedItem.id, e);
              }}
              className="flex items-center gap-2 px-4 py-2 rounded-full bg-red-600/30 hover:bg-red-600 text-xs font-bold text-red-300 hover:text-white transition-all border border-red-500/40"
            >
              <Trash2 className="w-4 h-4" /> Удалить
            </button>
          </div>
        </div>
      )}
    </div>
  );
};

{"content":"﻿import React, { useState } from 'react';\r\nimport { \r\n  X, \r\n  Image as ImageIcon, \r\n  Film, \r\n  Trash2, \r\n  Share2, \r\n  Maximize2, \r\n  Calendar,\r\n  Gamepad2,\r\n  Check\r\n} from 'lucide-react';\r\nimport { sound } from '../data/mockCatalog';\r\n\r\nimport { notify } from '../ui/toast';\r\ninterface ScreenshotItem {\r\n  id: string;\r\n  game: string;\r\n  title: string;\r\n  date: string;\r\n  imageUrl: string;\r\n  isVideo?: boolean;\r\n  duration?: string;\r\n}\r\n\r\nconst DEFAULT_SCREENSHOTS: ScreenshotItem[] = [\r\n  {\r\n    id: 'shot_1',\r\n    game: 'The Legend of Zelda: Tears of the Kingdom',\r\n    title: 'Парение над Хайрулом на планере',\r\n    date: '30.10.2026, 12:45',\r\n    imageUrl: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=1200&auto=format&fit=crop&q=80',\r\n  },\r\n  {\r\n    id: 'shot_2',\r\n    game: 'Super Mario Odyssey',\r\n    title: 'Лунный город и прыжок с кепкой',\r\n    date: '28.10.2026, 19:20',\r\n    imageUrl: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=1200&auto=format&fit=crop&q=80',\r\n  },\r\n  {\r\n    id: 'shot_3',\r\n    game: 'Metroid Dread',\r\n    title: 'Встреча с роботом E.M.M.I.',\r\n    date: '25.10.2026, 22:15',\r\n    imageUrl: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=1200&auto=format&fit=crop&q=80',\r\n    isVideo: true,\r\n    duration: '0:30',\r\n  },\r\n  {\r\n    id: 'shot_4',\r\n    game: 'Animal Crossing: New Horizons',\r\n    title: 'Праздник урожая на острове',\r\n    date: '20.10.2026, 14:10',\r\n    imageUrl: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?w=1200&auto=format&fit=crop&q=80',\r\n  },\r\n  {\r\n    id: 'shot_5',\r\n    game: 'Super Smash Bros. Ultimate',\r\n    title: 'Финальный удар 4-х игроков',\r\n    date: '18.10.2026, 21:05',\r\n    imageUrl: 'https://images.unsplash.com/photo-1538481199705-c710c4e965fc?w=1200&auto=format&fit=crop&q=80',\r\n    isVideo: true,\r\n    duration: '0:30',\r\n  },\r\n  {\r\n    id: 'shot_6',\r\n    game: 'Mario Kart 8 Deluxe',\r\n    title: 'Победа на Радужной трассе',\r\n    date: '15.10.2026, 16:40',\r\n    imageUrl: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=1200&auto=format&fit=crop&q=80',\r\n  },\r\n];\r\n\r\ninterface AlbumModalProps {\r\n  isOpen: boolean;\r\n  onClose: () => void;\r\n}\r\n\r\nexport const AlbumModal: React.FC<AlbumModalProps> = ({ isOpen, onClose }) => {\r\n  const [items, setItems] = useState<ScreenshotItem[]>(DEFAULT_SCREENSHOTS);\r\n  const [selectedItem, setSelectedItem] = useState<ScreenshotItem | null>(null);\r\n  const [filter, setFilter] = useState<'all' | 'screenshots' | 'videos'>('all');\r\n\r\n  if (!isOpen) return null;\r\n\r\n  const filtered = items.filter((item) => {\r\n    if (filter === 'screenshots') return !item.isVideo;\r\n    if (filter === 'videos') return item.isVideo;\r\n    return true;\r\n  });\r\n\r\n  const handleDelete = (id: string, e: React.MouseEvent) => {\r\n    e.stopPropagation();\r\n    sound.playBack();\r\n    const updated = items.filter((i) => i.id !== id);\r\n    setItems(updated);\r\n    if (selectedItem?.id === id) {\r\n      setSelectedItem(null);\r\n    }\r\n  };\r\n\r\n  return (\r\n    <div \r\n      className=\"fixed inset-0 z-50 flex items-center justify-center p-4 md:p-8 bg-black/85 backdrop-blur-2xl animate-in fade-in duration-200 select-none text-white font-sans\"\r\n      onClick={onClose}\r\n    >\r\n      <div \r\n        className=\"w-full max-w-5xl h-[85vh] bg-[#14141c]/95 backdrop-blur-3xl border border-white/15 rounded-[32px] shadow-[0_30px_90px_rgba(0,0,0,0.95)] overflow-hidden flex flex-col animate-in zoom-in-95 duration-150\"\r\n        onClick={(e) => e.stopPropagation()}\r\n      >\r\n        {/* Header */}\r\n        <div className=\"px-6 py-4 border-b border-white/10 flex items-center justify-between bg-white/[0.03]\">\r\n          <div className=\"flex items-center gap-3\">\r\n            <div className=\"w-10 h-10 rounded-2xl bg-gradient-to-tr from-blue-600 to-cyan-400 flex items-center justify-center text-white shadow-md\">\r\n              <ImageIcon className=\"w-5 h-5\" />\r\n            </div>\r\n            <div>\r\n              <h2 className=\"text-lg md:text-xl font-extrabold text-white tracking-tight\">\r\n                Альбом Nintendo Switch\r\n              </h2>\r\n              <p className=\"text-xs text-zinc-400\">\r\n                Скриншоты и видеозаписи с карты microSD\r\n              </p>\r\n            </div>\r\n          </div>\r\n\r\n          {/* Filter pills */}\r\n          <div className=\"flex items-center gap-2\">\r\n            <div className=\"flex items-center gap-1 bg-black/40 p-1 rounded-full border border-white/10\">\r\n              {[\r\n                { id: 'all', label: `Все (${items.length})` },\r\n                { id: 'screenshots', label: 'Фото' },\r\n                { id: 'videos', label: 'Видео 30с' },\r\n              ].map((tab) => (\r\n                <button\r\n                  key={tab.id}\r\n                  onClick={() => {\r\n                    sound.playClick();\r\n                    setFilter(tab.id as any);\r\n                  }}\r\n                  className={`px-3 py-1 rounded-full text-xs font-bold transition-all ${\r\n                    filter === tab.id\r\n                      ? 'bg-white text-black shadow'\r\n                      : 'text-zinc-400 hover:text-white'\r\n                  }`}\r\n                >\r\n                  {tab.label}\r\n                </button>\r\n              ))}\r\n            </div>\r\n\r\n            <button\r\n              onClick={() => {\r\n                sound.playBack();\r\n                onClose();\r\n              }}\r\n              className=\"p-2 rounded-full hover:bg-white/10 text-zinc-400 hover:text-white transition-all ml-2\"\r\n            >\r\n              <X className=\"w-5 h-5\" />\r\n            </button>\r\n          </div>\r\n        </div>\r\n\r\n        {/* Grid Content */}\r\n        <div className=\"flex-1 overflow-y-auto p-6 scrollbar-thin\">\r\n          {filtered.length === 0 ? (\r\n            <div className=\"h-full flex flex-col items-center justify-center text-zinc-500\">\r\n              <ImageIcon className=\"w-16 h-16 mb-2 opacity-40\" />\r\n              <p className=\"text-sm font-semibold\">Нет сохраненных материалов</p>\r\n            </div>\r\n          ) : (\r\n            <div className=\"grid grid-cols-2 sm:grid-cols-3 md:grid-cols-3 gap-4\">\r\n              {filtered.map((item) => (\r\n                <div\r\n                  key={item.id}\r\n                  onClick={() => {\r\n                    sound.playSelect();\r\n                    setSelectedItem(item);\r\n                  }}\r\n                  className=\"group relative aspect-video rounded-2xl overflow-hidden bg-black/60 border border-white/15 cursor-pointer hover:border-cyan-400 hover:scale-[1.02] transition-all shadow-lg\"\r\n                >\r\n                  <img\r\n                    src={item.imageUrl}\r\n                    alt={item.title}\r\n                    className=\"w-full h-full object-cover group-hover:brightness-110 transition-all\"\r\n                  />\r\n                  {/* Video Badge */}\r\n                  {item.isVideo && (\r\n                    <div className=\"absolute top-2 left-2 bg-black/70 backdrop-blur-md px-2 py-0.5 rounded-full text-[10px] font-bold text-amber-300 flex items-center gap-1 border border-white/20\">\r\n                      <Film className=\"w-3 h-3\" />\r\n                      <span>{item.duration}</span>\r\n                    </div>\r\n                  )}\r\n\r\n                  {/* Gradient Overlay */}\r\n                  <div className=\"absolute inset-0 bg-gradient-to-t from-black/90 via-black/30 to-transparent flex flex-col justify-end p-3 opacity-90 group-hover:opacity-100 transition-opacity\">\r\n                    <div className=\"text-xs font-bold text-white truncate drop-shadow\">\r\n                      {item.title}\r\n                    </div>\r\n                    <div className=\"text-[10px] text-zinc-400 flex items-center justify-between mt-0.5\">\r\n                      <span className=\"truncate\">{item.game}</span>\r\n                      <span className=\"shrink-0\">{item.date.split(',')[0]}</span>\r\n                    </div>\r\n                  </div>\r\n\r\n                  {/* Delete button on hover */}\r\n                  <button\r\n                    onClick={(e) => handleDelete(item.id, e)}\r\n                    className=\"absolute top-2 right-2 p-1.5 rounded-full bg-black/70 hover:bg-red-600 text-white opacity-0 group-hover:opacity-100 transition-opacity border border-white/20\"\r\n                    title=\"Удалить снимок\"\r\n                  >\r\n                    <Trash2 className=\"w-3.5 h-3.5\" />\r\n                  </button>\r\n                </div>\r\n              ))}\r\n            </div>\r\n          )}\r\n        </div>\r\n\r\n        {/* Footer */}\r\n        <div className=\"px-6 py-3 border-t border-white/10 bg-black/50 flex items-center justify-between text-xs text-zinc-400\">\r\n          <div className=\"flex items-center gap-4\">\r\n            <span>Снимков на microSD: <strong className=\"text-white\">{items.length}</strong></span>\r\n            <span>Свободно: <strong className=\"text-cyan-400\">189.4 ГБ</strong></span>\r\n          </div>\r\n          <div className=\"text-zinc-500\">\r\n            Кнопка снимка экрана: ⯀ на левом Joy-Con\r\n          </div>\r\n        </div>\r\n      </div>\r\n\r\n      {/* Fullscreen Photo Lightbox */}\r\n      {selectedItem && (\r\n        <div \r\n          className=\"fixed inset-0 z-60 bg-black/95 flex flex-col animate-in fade-in duration-150\"\r\n          onClick={() => setSelectedItem(null)}\r\n        >\r\n          {/* Lightbox Header */}\r\n          <div className=\"px-6 py-4 flex items-center justify-between border-b border-white/10 bg-black/60\">\r\n            <div className=\"flex flex-col\">\r\n              <span className=\"text-sm font-bold text-white\">{selectedItem.title}</span>\r\n              <span className=\"text-xs text-zinc-400\">{selectedItem.game} · {selectedItem.date}</span>\r\n            </div>\r\n            <button\r\n              onClick={() => setSelectedItem(null)}\r\n              className=\"p-2 rounded-full bg-white/10 hover:bg-white/20 text-white\"\r\n            >\r\n              <X className=\"w-5 h-5\" />\r\n            </button>\r\n          </div>\r\n\r\n          {/* Lightbox Image */}\r\n          <div className=\"flex-1 flex items-center justify-center p-4\">\r\n            <img\r\n              src={selectedItem.imageUrl}\r\n              alt={selectedItem.title}\r\n              className=\"max-w-full max-h-[75vh] object-contain rounded-2xl shadow-2xl border border-white/15\"\r\n            />\r\n          </div>\r\n\r\n          {/* Lightbox Controls */}\r\n          <div className=\"px-6 py-4 border-t border-white/10 flex items-center justify-center gap-4 bg-black/60\">\r\n            <button \r\n              onClick={(e) => {\r\n                e.stopPropagation();\r\n                sound.playClick();\r\n                notify('Снимок скопирован в буфер обмена для отправки на смартфон!');\r\n              }}\r\n              className=\"flex items-center gap-2 px-4 py-2 rounded-full bg-white/10 hover:bg-white/20 text-xs font-bold text-white transition-all\"\r\n            >\r\n              <Share2 className=\"w-4 h-4\" /> Отправить на смартфон (QR)\r\n            </button>\r\n            <button\r\n              onClick={(e) => {\r\n                e.stopPropagation();\r\n                handleDelete(selectedItem.id, e);\r\n              }}\r\n              className=\"flex items-center gap-2 px-4 py-2 rounded-full bg-red-600/30 hover:bg-red-600 text-xs font-bold text-red-300 hover:text-white transition-all border border-red-500/40\"\r\n            >\r\n              <Trash2 className=\"w-4 h-4\" /> Удалить\r\n            </button>\r\n          </div>\r\n        </div>\r\n      )}\r\n    </div>\r\n  );\r\n};\r\n"}