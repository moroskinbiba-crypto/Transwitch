import fs from 'fs';
import path from 'path';

const distDir = path.resolve('dist');
const romfsDir = path.resolve('romfs');

function copyRecursiveSync(src, dest) {
  const exists = fs.existsSync(src);
  const stats = exists && fs.statSync(src);
  const isDirectory = exists && stats.isDirectory();
  if (isDirectory) {
    if (!fs.existsSync(dest)) fs.mkdirSync(dest, { recursive: true });
    fs.readdirSync(src).forEach((childItemName) => {
      if (childItemName.endsWith('.nro') || childItemName.endsWith('.zip')) return;
      copyRecursiveSync(path.join(src, childItemName), path.join(dest, childItemName));
    });
  } else {
    if (src.endsWith('.nro') || src.endsWith('.zip')) return;
    fs.copyFileSync(src, dest);
  }
}

console.log('[DeskNX Build] Packaging RomFS for Nintendo Switch...');

if (!fs.existsSync(distDir)) {
  console.error('[DeskNX Build] Error: dist/ directory does not exist! Run "npm run build" first.');
  process.exit(1);
}

// Clean and recreate romfs
if (fs.existsSync(romfsDir)) {
  fs.rmSync(romfsDir, { recursive: true, force: true });
}

// Prepare multiple RomFS mount locations for compatibility across all Horizon OS versions:
// 1. romfs/html-document/
// 2. romfs/html-document/app.htdocs/
// 3. romfs/html-document/.htdocs/
// 4. romfs/
const targetPaths = [
  path.resolve('romfs/html-document'),
  path.resolve('romfs/html-document/app.htdocs'),
  path.resolve('romfs/html-document/.htdocs'),
  path.resolve('romfs'),
];

for (const target of targetPaths) {
  fs.mkdirSync(target, { recursive: true });
  copyRecursiveSync(distDir, target);
}

console.log('[DeskNX Build] Successfully prepared comprehensive RomFS layout for Nintendo Switch!');
