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

# Ignore Makefile comments so explanatory text mentioning the HOME build macro
# does not look like an active compiler definition. Check active Makefile lines.
active_makefile="$(grep -Ev '^[[:space:]]*#' "$makefile" || true)"
if printf '%s\n' "$active_makefile" | grep -Eq 'DESKNX_SYSTEM_APPLET|^[[:space:]]*ROMFS[[:space:]]*[:?+]?=|--romfsdir'; then
  fail "standalone target must not define system-applet mode or package a RomFS"
fi

grep -Fq 'extern "C" void userAppInit(void)' "$main" || fail "early standalone NRO diagnostics hook is missing"
grep -Fq 'if(sdMountedByDeskNX)fsdevUnmountDevice("sdmc");' "$main" || fail "standalone cleanup must not unmount libnx-owned SD"

if grep -Eq 'webOfflineCreate|webConfigShow|WebCommonConfig|WebDocumentKind|WebKit' "$main"; then
  fail "standalone native UI source still references the Horizon Web applet"
fi

printf '%s\n' '[standalone-entrypoint] OK: DeskNX.nro links the native SDL2 launcher; no HTML/WebKit runtime.'
