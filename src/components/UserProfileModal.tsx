import React, { useState } from 'react';
import { UserProfile, Workspace, DeskNXSettings } from '../types/desknx';
import { sound } from '../data/mockCatalog';
import { 
  X, 
  User, 
  Plus, 
  Check, 
  Edit3, 
  Trash2, 
  Layers, 
  Gamepad2, 
  Shield 
} from 'lucide-react';

interface UserProfileModalProps {
  isOpen: boolean;
  onClose: () => void;
  users: UserProfile[];
  activeUserId: string;
  onSelectUser: (user: UserProfile) => void;
  onCreateUser: (name: string, avatarUrl: string) => void;
  onDeleteUser: (userId: string) => void;
}

const PRESET_AVATARS = [
  { name: 'Red Hero', url: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=200&auto=format&fit=crop&q=80', color: '#e60012' },
  { name: 'Green Champion', url: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=200&auto=format&fit=crop&q=80', color: '#10b981' },
  { name: 'Blue Knight', url: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=200&auto=format&fit=crop&q=80', color: '#00b4e6' },
  { name: 'Golden Spirit', url: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=200&auto=format&fit=crop&q=80', color: '#f59e0b' },
  { name: 'Shadow Amethyst', url: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=200&auto=format&fit=crop&q=80', color: '#8b5cf6' },
];

export const UserProfileModal: React.FC<UserProfileModalProps> = ({
  isOpen,
  onClose,
  users,
  activeUserId,
  onSelectUser,
  onCreateUser,
  onDeleteUser,
}) => {
  const [isCreating, setIsCreating] = useState(false);
  const [newUserName, setNewUserName] = useState('');
  const [selectedAvatar, setSelectedAvatar] = useState(PRESET_AVATARS[0].url);

  if (!isOpen) return null;

  const handleCreateSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!newUserName.trim()) return;
    sound.playSelect();
    onCreateUser(newUserName.trim(), selectedAvatar);
    setNewUserName('');
    setIsCreating(false);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-xl animate-in fade-in select-none text-zinc-100">
      <div className="relative w-full max-w-lg bg-[#111116] border border-white/15 rounded-3xl shadow-[0_25px_70px_rgba(0,0,0,0.9)] overflow-hidden flex flex-col">
        
        {/* Header */}
        <div className="px-6 py-4 bg-zinc-900/80 border-b border-white/10 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-2xl bg-cyan-500/20 border border-cyan-400/40 flex items-center justify-center text-cyan-400 shadow">
              <User className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-base font-bold text-white">Console User Profiles</h2>
              <p className="text-xs text-zinc-400">
                Each user keeps independent desktops, layouts and custom wallpapers
              </p>
            </div>
          </div>
          <button
            onClick={() => {
              sound.playBack();
              onClose();
            }}
            className="p-2 rounded-xl bg-zinc-800 hover:bg-zinc-700 text-zinc-300 hover:text-white transition-all"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* User Profile List */}
        <div className="p-6 space-y-3 max-h-[60vh] overflow-y-auto">
          {users.map((user) => {
            const isActive = user.id === activeUserId;
            return (
              <div
                key={user.id}
                onClick={() => {
                  sound.playSelect();
                  onSelectUser(user);
                  onClose();
                }}
                className={`flex items-center justify-between p-3.5 rounded-2xl cursor-pointer border transition-all ${
                  isActive
                    ? 'bg-gradient-to-r from-cyan-600/30 to-blue-600/20 border-cyan-400/60 shadow-[0_0_20px_rgba(6,182,212,0.25)] ring-2 ring-cyan-400/30'
                    : 'bg-zinc-900/60 hover:bg-zinc-800 border-white/10'
                }`}
              >
                <div className="flex items-center gap-3.5">
                  <div className="relative">
                    <img
                      src={user.avatar}
                      alt={user.name}
                      className="w-12 h-12 rounded-2xl object-cover border-2 shadow-md"
                      style={{ borderColor: user.color || '#00b4e6' }}
                    />
                    {isActive && (
                      <span className="absolute -bottom-1 -right-1 w-4 h-4 bg-emerald-400 border-2 border-black rounded-full" />
                    )}
                  </div>

                  <div>
                    <div className="text-sm font-bold text-white flex items-center gap-2">
                      <span>{user.name}</span>
                      {isActive && (
                        <span className="text-[10px] font-mono bg-cyan-500 text-black px-2 py-0.2 rounded-full font-bold">
                          ACTIVE
                        </span>
                      )}
                    </div>
                    <div className="text-xs text-zinc-400 mt-0.5">
                      {user.workspaces.length} Desktops · {user.workspaces.reduce((acc, w) => acc + w.tiles.length, 0)} Items pinned
                    </div>
                  </div>
                </div>

                <div className="flex items-center gap-2">
                  {users.length > 1 && !isActive && (
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        if (confirm(`Delete profile "${user.name}"?`)) {
                          sound.playBack();
                          onDeleteUser(user.id);
                        }
                      }}
                      className="p-1.5 rounded-lg bg-zinc-800 hover:bg-red-500/30 text-zinc-400 hover:text-red-300 transition-all"
                      title="Delete Profile"
                    >
                      <Trash2 className="w-4 h-4" />
                    </button>
                  )}
                </div>
              </div>
            );
          })}

          {/* New Profile Form / Button */}
          {isCreating ? (
            <form onSubmit={handleCreateSubmit} className="p-4 rounded-2xl bg-black/60 border border-white/15 space-y-3">
              <div className="text-xs font-bold text-white uppercase tracking-wider">
                Create New Console User
              </div>
              <input
                type="text"
                value={newUserName}
                onChange={(e) => setNewUserName(e.target.value)}
                placeholder="User name (e.g. Alex, Player 2)..."
                autoFocus
                className="w-full bg-zinc-900 border border-white/15 rounded-xl px-3.5 py-2 text-sm text-white focus:outline-none focus:border-cyan-400"
              />

              <div>
                <label className="block text-[11px] font-semibold text-zinc-400 mb-1.5">
                  Pick Avatar:
                </label>
                <div className="flex gap-2">
                  {PRESET_AVATARS.map((av) => (
                    <button
                      key={av.name}
                      type="button"
                      onClick={() => setSelectedAvatar(av.url)}
                      className={`w-10 h-10 rounded-xl overflow-hidden border-2 transition-transform ${
                        selectedAvatar === av.url ? 'scale-115 ring-2 ring-cyan-400' : 'opacity-70 hover:opacity-100'
                      }`}
                      style={{ borderColor: av.color }}
                    >
                      <img src={av.url} alt={av.name} className="w-full h-full object-cover" />
                    </button>
                  ))}
                </div>
              </div>

              <div className="flex justify-end gap-2 pt-1">
                <button
                  type="button"
                  onClick={() => setIsCreating(false)}
                  className="px-3 py-1.5 rounded-xl text-xs text-zinc-400 hover:text-white"
                >
                  Cancel
                </button>
                <button
                  type="submit"
                  className="px-4 py-1.5 rounded-xl bg-cyan-500 hover:bg-cyan-400 text-black font-bold text-xs"
                >
                  Create Profile
                </button>
              </div>
            </form>
          ) : (
            <button
              onClick={() => {
                sound.playClick();
                setIsCreating(true);
              }}
              className="w-full py-3 rounded-2xl bg-white/5 hover:bg-white/10 border border-dashed border-white/20 text-xs font-bold text-cyan-300 flex items-center justify-center gap-2 transition-all"
            >
              <Plus className="w-4 h-4" /> Add New User Profile
            </button>
          )}
        </div>

        {/* Footer */}
        <div className="px-6 py-3 bg-zinc-950 border-t border-white/10 flex items-center justify-between text-xs text-zinc-400">
          <span>Nintendo Switch Multi-Account Support</span>
          <button
            onClick={onClose}
            className="px-4 py-1.5 rounded-xl bg-zinc-800 text-zinc-200 hover:text-white font-medium"
          >
            Done
          </button>
        </div>
      </div>
    </div>
  );
};
