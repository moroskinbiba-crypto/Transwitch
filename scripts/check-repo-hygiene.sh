#!/usr/bin/env bash
set -Eeuo pipefail

fail() {
  printf '[repo-hygiene] FAIL: %s\n' "$*" >&2
  exit 1
}

command -v git >/dev/null 2>&1 || fail "git is required"
repo_root="$(pwd -P)"
tracked="$(git -c safe.directory="$repo_root" ls-files)" || fail "git ls-files failed; run from repository root"

forbidden="$(printf '%s\n' "$tracked" | grep -E '^(src/|src-switch/|romfs/|node_modules/|dist/|build-native/|package\.json$|package-lock\.json$|bun\.lock$|index\.html$|vite\.config\.ts$|tsconfig\.json$)|(^|/)\.cache/' || true)"
[[ -z "$forbidden" ]] || fail "web frontend or generated build files are still tracked:\n$forbidden"

[[ -f .gitignore ]] || fail ".gitignore is missing"
for rule in "romfs/" "node_modules/" "dist/" "build-native/" "*.nro" "*.npdm"; do
  grep -Fxq "$rule" .gitignore || fail ".gitignore is missing required rule: $rule"
done

for symbol in "int main(int,char**)" "SDL_Init" "SDL_CreateRenderer"; do
  grep -Fq "$symbol" source/main.cpp || fail "native UI entry point is missing $symbol"
done
if grep -Eq 'webConfigShow|webOfflineCreate|WebCommonConfig|romfsInit' source/main.cpp; then
  fail "native UI entry point still contains a WebKit/RomFS launcher"
fi
grep -Fq 'SOURCES     := source' Makefile.switch ||
  fail "standalone NRO must compile the native source directory"
grep -Fq '$(notdir $(wildcard $(dir)/*.cpp))' Makefile.switch ||
  fail "native source files must be discovered by the Switch build"

printf '%s\n' "[repo-hygiene] OK: native C++/SDL2 NRO only; WebKit/React/RomFS build removed."
