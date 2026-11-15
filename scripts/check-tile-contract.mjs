#!/usr/bin/env node
/**
 * Tile-shape contract guard.
 *
 * `include/desknx/Model.hpp` (native) and `src/utils/desknxLayout.ts` (web UI)
 * must accept exactly the same tile shapes. When they drift, the parser on the
 * other side silently rewrites a tile — a 3-wide widget collapses into a single
 * cell on load, and the next save persists the loss.
 *
 * This check fails the build instead of letting that happen silently.
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');

const NATIVE_MODEL = 'include/desknx/Model.hpp';
const WEB_MODEL = 'src/utils/desknxLayout.ts';

function read(relativePath) {
  try {
    return readFileSync(resolve(repoRoot, relativePath), 'utf8');
  } catch (error) {
    console.error(`tile-contract: cannot read ${relativePath}: ${error.message}`);
    process.exit(2);
  }
}

/** Extracts the body of `isSupportedTileSize` from either source file. */
function extractFunctionBody(source, relativePath) {
  const marker = source.indexOf('isSupportedTileSize');
  if (marker === -1) {
    console.error(`tile-contract: isSupportedTileSize not found in ${relativePath}`);
    process.exit(2);
  }

  const bodyStart = source.indexOf('{', marker);
  const bodyEnd = source.indexOf('\n}', bodyStart);
  if (bodyStart === -1 || bodyEnd === -1) {
    console.error(`tile-contract: unbalanced isSupportedTileSize body in ${relativePath}`);
    process.exit(2);
  }

  return source.slice(bodyStart, bodyEnd);
}

/** Collects `(w, h)` pairs from C++ `w == 2 && h == 1` style comparisons. */
function parseNativeShapes() {
  const body = extractFunctionBody(read(NATIVE_MODEL), NATIVE_MODEL);
  const shapes = new Set();

  for (const match of body.matchAll(/w\s*==\s*(\d)\s*&&\s*h\s*==\s*(\d)/g)) {
    shapes.add(`${match[1]}x${match[2]}`);
  }

  return shapes;
}

/** Collects shapes from the TypeScript TILE_SHAPES table. */
function parseWebShapes() {
  const source = read(WEB_MODEL);
  const tableMatch = source.match(/TILE_SHAPES\s*=\s*\[([\s\S]*?)\]/);
  if (!tableMatch) {
    console.error(`tile-contract: TILE_SHAPES table not found in ${WEB_MODEL}`);
    process.exit(2);
  }

  const shapes = new Set();
  for (const match of tableMatch[1].matchAll(/\{\s*w:\s*(\d)\s*,\s*h:\s*(\d)\s*\}/g)) {
    shapes.add(`${match[1]}x${match[2]}`);
  }

  return shapes;
}

const native = parseNativeShapes();
const web = parseWebShapes();

if (native.size === 0 || web.size === 0) {
  console.error('tile-contract: parsed an empty shape set — check the parser, not the sources.');
  process.exit(2);
}

const onlyNative = [...native].filter((shape) => !web.has(shape));
const onlyWeb = [...web].filter((shape) => !native.has(shape));

if (onlyNative.length > 0 || onlyWeb.length > 0) {
  console.error('tile-contract: FAIL — native and web tile shapes disagree.');
  if (onlyNative.length > 0) {
    console.error(`  only in ${NATIVE_MODEL}: ${onlyNative.join(', ')}`);
  }
  if (onlyWeb.length > 0) {
    console.error(`  only in ${WEB_MODEL}: ${onlyWeb.join(', ')}`);
  }
  console.error('Both sides must list the same shapes, or saved layouts will be rewritten on load.');
  process.exit(1);
}

console.log(`tile-contract: OK (${[...native].sort().join(', ')})`);
