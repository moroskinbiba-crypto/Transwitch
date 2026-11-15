#!/usr/bin/env node
import { readFileSync } from "node:fs";
import { spawnSync } from "node:child_process";

function fail(message) {
  console.error("[repo-hygiene] FAIL: " + message);
  process.exitCode = 1;
}

const git = spawnSync("git", ["ls-files", "-z"], { encoding: "utf8" });
if (git.status !== 0) {
  fail("git ls-files failed; run this check from the repository root");
  process.exit(1);
}

const tracked = git.stdout.split("\0").filter(Boolean);
const forbidden = tracked.filter((path) =>
  path === "desknx-release.zip" ||
  path.startsWith(".vite/") ||
  path.startsWith("node_modules/") ||
  path.startsWith("dist/") ||
  path.startsWith("SdOut/") ||
  /(^|\/)\.cache\//.test(path)
);

if (forbidden.length > 0)
  fail("generated/local files are tracked:\n  " + forbidden.join("\n  "));

const ignore = readFileSync(".gitignore", "utf8");
if (/^\s*\{"content":/m.test(ignore))
  fail(".gitignore contains a serialized response envelope, not ignore rules");
for (const rule of [".vite/", "*.zip", "node_modules/", "dist/"]) {
  if (!ignore.split(/\r?\n/).some((line) => line.trim() === rule))
    fail(".gitignore is missing required rule: " + rule);
}

if (process.exitCode)
  process.exit(process.exitCode);
console.log("[repo-hygiene] OK: no generated Vite cache/release bundle is tracked; .gitignore is clean.");
