import fs from 'fs';
import path from 'path';

const distDir = path.resolve('dist');
const romfsDir = path.resolve('romfs');
const targetDir = path.resolve('romfs/html-document');

function copyRecursiveSync(src, dest) {
  const exists = fs.existsSync(src);
  const stats = exists && fs.statSync(src);
  const isDirectory = exists && stats.isDirectory();
  if (isDirectory) {
    if (!fs.existsSync(dest)) fs.mkdirSync(dest, { recursive: true });
    fs.readdirSync(src).forEach((childItemName) => {
      copyRecursiveSync(path.join(src, childItemName), path.join(dest, childItemName));
    });
  } else {
    fs.copyFileSync(src, dest);
  }
}

console.log('[DeskNX Build] Packaging RomFS for Nintendo Switch...');

if (!fs.existsSync(distDir)) {
  console.error('[DeskNX Build] Error: dist/ directory does not exist! Run "npm run build" first.');
  process.exit(1);
}

// Clean and recreate romfs/html-document
if (fs.existsSync(romfsDir)) {
  fs.rmSync(romfsDir, { recursive: true, force: true });
}
fs.mkdirSync(targetDir, { recursive: true });

// Copy all dist files into romfs/html-document/
copyRecursiveSync(distDir, targetDir);

console.log('[DeskNX Build] Successfully prepared romfs/html-document with all assets for .nro bundling!');
