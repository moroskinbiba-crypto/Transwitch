#!/usr/bin/env bash
set -Eeuo pipefail

fail() {
  printf '[repo-hygiene] FAIL: %s\n' "$*" >&2
  exit 1
}

command -v git >/dev/null 2>&1 || fail "git is required"
repo_root="$(pwd -P)"
tracked="$(git -c safe.directory="$repo_root" ls-files)" || fail "git ls-files failed; run from repository root"

forbidden_generated="$(printf '%s\n' "$tracked" | grep -E '^(desknx-release\.zip|\.vite/|node_modules/|dist/|SdOut/)|(^|/)\.cache/' || true)"
[[ -z "$forbidden_generated" ]] || fail "generated/local files are tracked:\n$forbidden_generated"

forbidden_web="$(printf '%s\n' "$tracked" | grep -E '^(src/|src-switch/|romfs/|index\.html$|package\.json$|package-lock\.json$|bun\.lock$|vite\.config\.ts$|tsconfig\.json$|scripts/prepare-romfs\.js$|scripts/check-tile-contract\.mjs$)' || true)"
[[ -z "$forbidden_web" ]] || fail "legacy WebKit/React frontend files are still tracked:\n$forbidden_web"

[[ -f .gitignore ]] || fail ".gitignore is missing"
if grep -Eq '^[[:space:]]*\{"content":' .gitignore; then
  fail ".gitignore contains a serialized response envelope, not ignore rules"
fi
for rule in "*.zip" "*.nro" "*.nacp" "node_modules/" "dist/" "build/"; do
  grep -Fxq "$rule" .gitignore || fail ".gitignore is missing required rule: $rule"
done

for file in Makefile.switch .github/workflows/build-nro.yml .github/workflows/build.yml .github/workflows/build-system-backend.yml; do
  if grep -En 'npm ci|npm run build|vite|WebKit|webOfflineCreate|webConfigShow|prepare-romfs|--romfsdir' "$file"; then
    fail "web frontend build/runtime reference remains in $file"
  fi
done

printf '%s\n' "[repo-hygiene] OK: native-only Switch build; no tracked WebKit/React frontend or generated local files."
