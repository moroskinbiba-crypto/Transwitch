#!/usr/bin/env bash
set -Eeuo pipefail
fail() { printf '[standalone-entrypoint] FAIL: %s\n' "$*" >&2; exit 1; }

makefile="Makefile.switch"
main="source/main.cpp"
[[ -f "$makefile" && -f "$main" ]] || fail "native standalone sources are missing"

grep -Fq 'SOURCES     := source projects/DeskNXMenu/source' "$makefile" ||
  fail "standalone NRO must search the shared native UI and its support modules"
grep -Fq 'COMMON_CPPFILES := $(notdir $(wildcard source/*.cpp))' "$makefile" ||
  fail "the production source/*.cpp set must be compiled"
grep -Fq 'MENU_CPPFILES   := $(filter-out main.cpp,' "$makefile" ||
  fail "the old prototype main.cpp must be excluded"
grep -Fq 'CPPFILES        := $(COMMON_CPPFILES) $(MENU_CPPFILES)' "$makefile" ||
  fail "the native main.cpp must precede supporting menu modules"
grep -Fq 'SDL2_IMAGE_LIBS' "$makefile" || fail "SDL2_image is not linked"
grep -Fq 'SDL2_TTF_LIBS' "$makefile" || fail "SDL2_ttf is not linked"
grep -Fq -- '--nacp=$(OUTPUT).nacp' "$makefile" || fail "NACP metadata is not embedded"

for symbol in 'int main(int,char**)' 'fsdevMountSdmc()' 'SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)' \
              'SDL_CreateWindow' 'SDL_CreateRenderer' 'SDL_PollEvent' 'touchInput(const SDL_Event'; do
  grep -Fq "$symbol" "$main" || fail "native UI entry point is missing $symbol"
done

if grep -Eq 'webOfflineCreate|webConfigShow|WebCommonConfig|WebDocumentKind|WebKit' "$main" "$makefile"; then
  fail "standalone NRO still references the Horizon Web applet"
fi
if grep -Eq 'DESKNX_SYSTEM_APPLET|ROMFS|romfsdir' "$makefile"; then
  fail "standalone target must not inherit system-applet mode or embed a RomFS"
fi

printf '%s\n' '[standalone-entrypoint] OK: DeskNX.nro links the native SDL2 launcher; no HTML/WebKit runtime.'
