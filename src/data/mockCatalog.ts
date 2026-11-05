import { Candidate, Kind, Workspace } from '../types/desknx';

export const CATALOG_ITEMS: Candidate[] = [
  // --- INSTALLED GAMES ---
  {
    id: 'totk',
    kind: 'Game',
    title: 'The Legend of Zelda: Tears of the Kingdom',
    data: '0100F2C0115B6000',
    author: 'Nintendo',
    version: 'v1.2.1',
    size: '16.3 GB',
    w: 2,
    h: 2,
    color: 0,
    playtimeSeconds: 324000, // 90h
    lastPlayed: Date.now() - 3600000 * 2,
    launchCount: 142,
    icon: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=300&auto=format&fit=crop&q=80',
    banner: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=800&auto=format&fit=crop&q=80',
    description: 'An epic adventure across the land and skies of Hyrule.',
    category: 'Game',
  },
  {
    id: 'smo',
    kind: 'Game',
    title: 'Super Mario Odyssey',
    data: '0100000000010000',
    author: 'Nintendo',
    version: 'v1.3.0',
    size: '5.6 GB',
    w: 2,
    h: 1,
    color: 0,
    playtimeSeconds: 216000, // 60h
    lastPlayed: Date.now() - 3600000 * 12,
    launchCount: 88,
    icon: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=300&auto=format&fit=crop&q=80',
    banner: 'https://images.unsplash.com/photo-1563089145-599997674d42?w=800&auto=format&fit=crop&q=80',
    description: 'Explore incredible places far from the Mushroom Kingdom with Mario and Cappy.',
    category: 'Game',
  },
  {
    id: 'smash',
    kind: 'Game',
    title: 'Super Smash Bros. Ultimate',
    data: '01006A800016E000',
    author: 'Nintendo / Bandai Namco',
    version: 'v13.0.1',
    size: '17.2 GB',
    w: 1,
    h: 1,
    color: 1,
    playtimeSeconds: 468000, // 130h
    lastPlayed: Date.now() - 3600000 * 24,
    launchCount: 310,
    icon: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=300&auto=format&fit=crop&q=80',
    description: 'Gaming icons clash in the ultimate showdown you can play anytime, anywhere.',
    category: 'Game',
  },
  {
    id: 'acnh',
    kind: 'Game',
    title: 'Animal Crossing: New Horizons',
    data: '01006F8002326000',
    author: 'Nintendo',
    version: 'v2.0.6',
    size: '10.8 GB',
    w: 1,
    h: 1,
    color: 2,
    playtimeSeconds: 540000, // 150h
    lastPlayed: Date.now() - 3600000 * 48,
    launchCount: 220,
    icon: 'https://images.unsplash.com/photo-1518791841217-8f162f1e1131?w=300&auto=format&fit=crop&q=80',
    description: 'Escape to a deserted island and create your own paradise as you explore, create, and customize.',
    category: 'Game',
  },
  {
    id: 'metroid',
    kind: 'Game',
    title: 'Metroid Dread',
    data: '010093801237C000',
    author: 'MercurySteam / Nintendo',
    version: 'v2.1.0',
    size: '4.4 GB',
    w: 2,
    h: 1,
    color: 4,
    playtimeSeconds: 90000, // 25h
    lastPlayed: Date.now() - 3600000 * 72,
    launchCount: 45,
    icon: 'https://images.unsplash.com/photo-1509198397868-475647b2a1e5?w=300&auto=format&fit=crop&q=80',
    description: 'Join intergalactic bounty hunter Samus Aran in her first new 2D Metroid story in 19 years.',
    category: 'Game',
  },
  {
    id: 'mk8d',
    kind: 'Game',
    title: 'Mario Kart 8 Deluxe',
    data: '0100152000022000',
    author: 'Nintendo',
    version: 'v3.0.1',
    size: '8.2 GB',
    w: 1,
    h: 1,
    color: 0,
    playtimeSeconds: 380000,
    lastPlayed: Date.now() - 3600000 * 18,
    launchCount: 195,
    icon: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?w=300&auto=format&fit=crop&q=80',
    description: 'Hit the road with the definitive version of Mario Kart 8 and play anytime, anywhere.',
    category: 'Game',
  },
  {
    id: 'hades',
    kind: 'Game',
    title: 'Hades',
    data: '0100535012974000',
    author: 'Supergiant Games',
    version: 'v1.0.38',
    size: '5.8 GB',
    w: 2,
    h: 1,
    color: 7,
    playtimeSeconds: 180000,
    lastPlayed: Date.now() - 3600000 * 6,
    launchCount: 112,
    icon: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=300&auto=format&fit=crop&q=80',
    description: 'Defy the god of the dead as you hack and slash out of the Underworld.',
    category: 'Game',
  },
  {
    id: 'hollowknight',
    kind: 'Game',
    title: 'Hollow Knight',
    data: '0100633007D48000',
    author: 'Team Cherry',
    version: 'v1.4.3.2',
    size: '5.3 GB',
    w: 1,
    h: 1,
    color: 5,
    playtimeSeconds: 270000,
    lastPlayed: Date.now() - 3600000 * 30,
    launchCount: 89,
    icon: 'https://images.unsplash.com/photo-1534447677768-be436bb09401?w=300&auto=format&fit=crop&q=80',
    description: 'Forge your own path in Hollow Knight! An epic action adventure through a vast ruined kingdom.',
    category: 'Game',
  },
  {
    id: 'splatoon3',
    kind: 'Game',
    title: 'Splatoon 3',
    data: '0100C2500FC20000',
    author: 'Nintendo',
    version: 'v6.0.0',
    size: '7.9 GB',
    w: 1,
    h: 1,
    color: 3,
    playtimeSeconds: 154000,
    lastPlayed: Date.now() - 3600000 * 40,
    launchCount: 76,
    icon: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=300&auto=format&fit=crop&q=80',
    description: 'Welcome to the Splatlands, a sun-scorched desert inhabited by battle-hardened Inklings and Octolings.',
    category: 'Game',
  },

  // --- HOMEBREW NROs ---
  {
    id: 'hbmenu',
    kind: 'Nro',
    title: 'Homebrew Menu',
    data: 'sdmc:/switch/hbmenu.nro',
    author: 'devkitPro & switchbrew',
    version: 'v3.5.1',
    size: '4.8 MB',
    w: 2,
    h: 1,
    color: 1,
    playtimeSeconds: 120000,
    lastPlayed: Date.now() - 3600000 * 1,
    launchCount: 520,
    icon: 'https://images.unsplash.com/photo-1618005182384-a83a8bd57fbe?w=300&auto=format&fit=crop&q=80',
    description: 'Standard Nintendo Switch homebrew launcher environment.',
    category: 'Homebrew',
  },
  {
    id: 'nxshell',
    kind: 'Nro',
    title: 'NX-Shell',
    data: 'sdmc:/switch/nx-shell.nro',
    author: 'joel16',
    version: 'v4.0.1',
    size: '6.2 MB',
    w: 1,
    h: 1,
    color: 5,
    playtimeSeconds: 34000,
    lastPlayed: Date.now() - 3600000 * 5,
    launchCount: 160,
    icon: 'https://images.unsplash.com/photo-1558494949-ef010cbdcc31?w=300&auto=format&fit=crop&q=80',
    description: 'Multi-purpose file manager and explorer for Nintendo Switch.',
    category: 'Homebrew',
  },
  {
    id: 'daybreak',
    kind: 'Nro',
    title: 'Daybreak',
    data: 'sdmc:/switch/daybreak.nro',
    author: 'Atmosphère team (SciresM)',
    version: 'v1.0.0',
    size: '1.2 MB',
    w: 1,
    h: 1,
    color: 0,
    playtimeSeconds: 1800,
    lastPlayed: Date.now() - 3600000 * 300,
    launchCount: 14,
    icon: 'https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=300&auto=format&fit=crop&q=80',
    description: 'Safe and seamless firmware updater for Atmosphère CFW.',
    category: 'Homebrew',
  },
  {
    id: 'edizon',
    kind: 'Nro',
    title: 'EdiZon SE',
    data: 'sdmc:/switch/edizon.nro',
    author: 'WerWolv & TomSwitch',
    version: 'v3.8.37',
    size: '8.4 MB',
    w: 2,
    h: 1,
    color: 4,
    playtimeSeconds: 45000,
    lastPlayed: Date.now() - 3600000 * 8,
    launchCount: 94,
    icon: 'https://images.unsplash.com/photo-1526374965328-7f61d4dc18c5?w=300&auto=format&fit=crop&q=80',
    description: 'Cheat code manager, memory debugger and RAM bookmark editor.',
    category: 'Homebrew',
  },
  {
    id: 'jksv',
    kind: 'Nro',
    title: 'JKSV Save Manager',
    data: 'sdmc:/switch/jksv.nro',
    author: 'J-D-K',
    version: '09.01.2023',
    size: '3.1 MB',
    w: 1,
    h: 1,
    color: 2,
    playtimeSeconds: 67000,
    lastPlayed: Date.now() - 3600000 * 20,
    launchCount: 130,
    icon: 'https://images.unsplash.com/photo-1544716278-ca5e3f4abd8c?w=300&auto=format&fit=crop&q=80',
    description: 'Save data backup, export, and restore utility for all games.',
    category: 'Homebrew',
  },
  {
    id: 'retroarch',
    kind: 'Nro',
    title: 'RetroArch',
    data: 'sdmc:/switch/retroarch.nro',
    author: 'Libretro Team',
    version: 'v1.17.0',
    size: '142 MB',
    w: 2,
    h: 2,
    color: 5,
    playtimeSeconds: 210000,
    lastPlayed: Date.now() - 3600000 * 4,
    launchCount: 180,
    icon: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=300&auto=format&fit=crop&q=80',
    description: 'Frontend for multi-console emulators, game engines, and media players.',
    category: 'Homebrew',
  },
  {
    id: 'nxmp',
    kind: 'Nro',
    title: 'NXMP Media Player',
    data: 'sdmc:/switch/nxmp.nro',
    author: 'proconsule',
    version: 'v0.8.3',
    size: '18.9 MB',
    w: 2,
    h: 1,
    color: 6,
    playtimeSeconds: 52000,
    lastPlayed: Date.now() - 3600000 * 60,
    launchCount: 41,
    icon: 'https://images.unsplash.com/photo-1489599849927-2ee91cede3ba?w=300&auto=format&fit=crop&q=80',
    description: 'Hardware accelerated video player supporting MKV, MP4, FLV with subtitles.',
    category: 'Homebrew',
  },

  // --- DYNAMIC SYSTEM WIDGETS ---
  {
    id: 'widget-clock',
    kind: 'Clock',
    title: 'Clock & Date',
    data: 'widget://clock',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 2,
    h: 1,
    color: 1,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Real-time digital clock, weekday, date, and local timezone.',
    category: 'Widget',
  },
  {
    id: 'widget-battery',
    kind: 'Battery',
    title: 'Battery & Power',
    data: 'widget://battery',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 1,
    h: 1,
    color: 2,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Battery percentage, charging state, voltage, and health status.',
    category: 'Widget',
  },
  {
    id: 'widget-storage',
    kind: 'Storage',
    title: 'SD Card Storage',
    data: 'widget://storage',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 2,
    h: 1,
    color: 5,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Detailed disk usage for sdmc:/ and internal NAND eMMC.',
    category: 'Widget',
  },
  {
    id: 'widget-system',
    kind: 'System',
    title: 'System Tools',
    data: 'widget://system',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 2,
    h: 1,
    color: 0,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Quick toggles: Sleep, Reboot, Payloads, Album, and Settings.',
    category: 'Widget',
  },
  {
    id: 'widget-recent',
    kind: 'Recent',
    title: 'Recent Activity',
    data: 'widget://recent',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 3,
    h: 1,
    color: 4,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Quick resume bar showing the 4 most recently launched titles.',
    category: 'Widget',
  },
  {
    id: 'widget-calendar',
    kind: 'Calendar',
    title: 'Calendar',
    data: 'widget://calendar',
    author: 'DeskNX System',
    version: 'v2.0',
    size: 'Widget',
    w: 2,
    h: 2,
    color: 3,
    playtimeSeconds: 0,
    lastPlayed: 0,
    launchCount: 0,
    icon: '',
    description: 'Monthly calendar view with current day highlight.',
    category: 'Widget',
  },
];

export const INITIAL_WORKSPACES: Workspace[] = [
  {
    id: 'desk_main',
    name: 'Main',
    backgroundFit: 0,
    artworkMode: 0,
    labelOpacity: 196,
    accentColor: 0,
    tiles: [
      {
        id: 'tile_clock',
        kind: 'Clock',
        title: 'Clock & Date',
        data: 'widget://clock',
        x: 0,
        y: 0,
        w: 2,
        h: 1,
        color: 1,
      },
      {
        id: 'tile_battery',
        kind: 'Battery',
        title: 'Battery',
        data: 'widget://battery',
        x: 2,
        y: 0,
        w: 1,
        h: 1,
        color: 2,
      },
      {
        id: 'tile_recent',
        kind: 'Recent',
        title: 'Recent Launches',
        data: 'widget://recent',
        x: 3,
        y: 0,
        w: 3,
        h: 1,
        color: 4,
      },
      {
        id: 'tile_totk',
        kind: 'Game',
        title: 'Zelda: Tears of the Kingdom',
        data: '0100F2C0115B6000',
        x: 0,
        y: 1,
        w: 2,
        h: 2,
        color: 0,
        icon: 'https://images.unsplash.com/photo-1579783900882-c0d3dad7b119?w=300&auto=format&fit=crop&q=80',
        banner: 'https://images.unsplash.com/photo-1518709268805-4e9042af9f23?w=800&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_smo',
        kind: 'Game',
        title: 'Super Mario Odyssey',
        data: '0100000000010000',
        x: 2,
        y: 1,
        w: 2,
        h: 1,
        color: 0,
        icon: 'https://images.unsplash.com/photo-1566576912321-d58ddd7a6088?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_smash',
        kind: 'Game',
        title: 'Super Smash Bros. Ultimate',
        data: '01006A800016E000',
        x: 4,
        y: 1,
        w: 1,
        h: 1,
        color: 1,
        icon: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_acnh',
        kind: 'Game',
        title: 'Animal Crossing: New Horizons',
        data: '01006F8002326000',
        x: 5,
        y: 1,
        w: 1,
        h: 1,
        color: 2,
        icon: 'https://images.unsplash.com/photo-1518791841217-8f162f1e1131?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_metroid',
        kind: 'Game',
        title: 'Metroid Dread',
        data: '010093801237C000',
        x: 2,
        y: 2,
        w: 2,
        h: 1,
        color: 4,
        icon: 'https://images.unsplash.com/photo-1509198397868-475647b2a1e5?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_hades',
        kind: 'Game',
        title: 'Hades',
        data: '0100535012974000',
        x: 4,
        y: 2,
        w: 2,
        h: 1,
        color: 7,
        icon: 'https://images.unsplash.com/photo-1542751371-adc38448a05e?w=300&auto=format&fit=crop&q=80',
      },
    ],
  },
  {
    id: 'desk_homebrew',
    name: 'Homebrew',
    backgroundFit: 0,
    artworkMode: 0,
    labelOpacity: 210,
    accentColor: 1,
    tiles: [
      {
        id: 'tile_system',
        kind: 'System',
        title: 'DeskNX Tools',
        data: 'widget://system',
        x: 0,
        y: 0,
        w: 2,
        h: 1,
        color: 0,
      },
      {
        id: 'tile_storage',
        kind: 'Storage',
        title: 'SD Storage',
        data: 'widget://storage',
        x: 2,
        y: 0,
        w: 2,
        h: 1,
        color: 5,
      },
      {
        id: 'tile_hbmenu',
        kind: 'Nro',
        title: 'Homebrew Menu',
        data: 'sdmc:/switch/hbmenu.nro',
        x: 4,
        y: 0,
        w: 2,
        h: 1,
        color: 1,
        icon: 'https://images.unsplash.com/photo-1618005182384-a83a8bd57fbe?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_nxshell',
        kind: 'Nro',
        title: 'NX-Shell',
        data: 'sdmc:/switch/nx-shell.nro',
        x: 0,
        y: 1,
        w: 1,
        h: 1,
        color: 5,
        icon: 'https://images.unsplash.com/photo-1558494949-ef010cbdcc31?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_jksv',
        kind: 'Nro',
        title: 'JKSV Save Tool',
        data: 'sdmc:/switch/jksv.nro',
        x: 1,
        y: 1,
        w: 1,
        h: 1,
        color: 2,
        icon: 'https://images.unsplash.com/photo-1544716278-ca5e3f4abd8c?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_edizon',
        kind: 'Nro',
        title: 'EdiZon SE',
        data: 'sdmc:/switch/edizon.nro',
        x: 2,
        y: 1,
        w: 2,
        h: 1,
        color: 4,
        icon: 'https://images.unsplash.com/photo-1526374965328-7f61d4dc18c5?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_daybreak',
        kind: 'Nro',
        title: 'Daybreak',
        data: 'sdmc:/switch/daybreak.nro',
        x: 4,
        y: 1,
        w: 1,
        h: 1,
        color: 0,
        icon: 'https://images.unsplash.com/photo-1506744038136-46273834b3fb?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_retroarch',
        kind: 'Nro',
        title: 'RetroArch Multi-Core',
        data: 'sdmc:/switch/retroarch.nro',
        x: 0,
        y: 2,
        w: 2,
        h: 1,
        color: 5,
        icon: 'https://images.unsplash.com/photo-1550745165-9bc0b252726f?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_nxmp',
        kind: 'Nro',
        title: 'NXMP Player',
        data: 'sdmc:/switch/nxmp.nro',
        x: 2,
        y: 2,
        w: 2,
        h: 1,
        color: 6,
        icon: 'https://images.unsplash.com/photo-1489599849927-2ee91cede3ba?w=300&auto=format&fit=crop&q=80',
      },
    ],
  },
  {
    id: 'desk_retro',
    name: 'Retro & Media',
    backgroundFit: 0,
    artworkMode: 0,
    labelOpacity: 196,
    accentColor: 3,
    tiles: [
      {
        id: 'tile_cal',
        kind: 'Calendar',
        title: 'Calendar',
        data: 'widget://calendar',
        x: 0,
        y: 0,
        w: 2,
        h: 2,
        color: 3,
      },
      {
        id: 'tile_hollow',
        kind: 'Game',
        title: 'Hollow Knight',
        data: '0100633007D48000',
        x: 2,
        y: 0,
        w: 1,
        h: 1,
        color: 5,
        icon: 'https://images.unsplash.com/photo-1534447677768-be436bb09401?w=300&auto=format&fit=crop&q=80',
      },
      {
        id: 'tile_mk8',
        kind: 'Game',
        title: 'Mario Kart 8 Deluxe',
        data: '0100152000022000',
        x: 3,
        y: 0,
        w: 1,
        h: 1,
        color: 0,
        icon: 'https://images.unsplash.com/photo-1511512578047-dfb367046420?w=300&auto=format&fit=crop&q=80',
      },
    ],
  },
];

// Audio synthesizer for authentic Switch sound effects
class SoundEngine {
  private ctx: AudioContext | null = null;
  public enabled: boolean = true;

  private init() {
    if (!this.ctx && typeof window !== 'undefined') {
      const AudioCtx = window.AudioContext || (window as any).webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume();
    }
  }

  playNav() {
    if (!this.enabled) return;
    this.init();
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'triangle';
    osc.frequency.setValueAtTime(440, this.ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(880, this.ctx.currentTime + 0.04);
    gain.gain.setValueAtTime(0.08, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.05);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.05);
  }

  playSelect() {
    if (!this.enabled) return;
    this.init();
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(587.33, this.ctx.currentTime); // D5
    osc.frequency.setValueAtTime(880, this.ctx.currentTime + 0.05); // A5
    gain.gain.setValueAtTime(0.12, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.14);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.15);
  }

  playBack() {
    if (!this.enabled) return;
    this.init();
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(659.25, this.ctx.currentTime); // E5
    osc.frequency.linearRampToValueAtTime(329.63, this.ctx.currentTime + 0.08); // E4
    gain.gain.setValueAtTime(0.1, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.1);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.1);
  }

  playClick() {
    if (!this.enabled) return;
    this.init();
    if (!this.ctx) return;
    // Nintendo Switch snap / click sound
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'square';
    osc.frequency.setValueAtTime(1200, this.ctx.currentTime);
    gain.gain.setValueAtTime(0.15, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.03);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.035);
  }

  playLaunch() {
    if (!this.enabled) return;
    this.init();
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(440, this.ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(1320, this.ctx.currentTime + 0.22);
    gain.gain.setValueAtTime(0.14, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.25);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.26);
  }
}

export const sound = new SoundEngine();
