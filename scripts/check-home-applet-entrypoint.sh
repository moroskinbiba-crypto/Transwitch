#!/usr/bin/env bash
set -Eeuo pipefail
fail() { printf '[home-entrypoint] FAIL: %s\n' "$*" >&2; exit 1; }

makefile="projects/DeskNXMenu/Makefile"
main="source/main.cpp"
[[ -f "$makefile" && -f "$main" ]] || fail "required HOME applet sources are missing"

grep -Fq 'SOURCES     := ../../source ./source' "$makefile" ||
  fail "VPATH must search the shared production source directory first"
grep -Fq 'MENU_CPPFILES := $(filter-out main.cpp,' "$makefile" ||
  fail "the prototype menu main.cpp must be excluded"
grep -Fq 'COMMON_CPPFILES := $(notdir $(wildcard ../../source/*.cpp))' "$makefile" ||
  fail "shared production sources must include source/main.cpp"
grep -Fq 'CPPFILES := $(COMMON_CPPFILES) $(MENU_CPPFILES)' "$makefile" ||
  fail "production entry point must take precedence in the source list"

for symbol in 'DESKNX_SYSTEM_APPLET' 'AppletType_LibraryApplet' 'SendMenuReady' 'SendHeartbeat' \
              'SendLaunchTitle' 'SendLaunchHomebrew' 'heartbeatThreadMain' \
              'touchInput(const SDL_Event' 'void keyInput' \
              'fsdevCommitDevice("sdmc")' 'readHistoryFile(HISTORY_BAK_FILE' \
              'ACTIVE_WORKSPACE_BAK'; do
  grep -Fq "$symbol" "$main" || fail "production HOME UI is missing $symbol"
done

# LibraryApplet lifecycle is implemented in the system backend, not in the UI main.
library_applet="projects/sSystem/source/LibraryApplet.cpp"
[[ -f "$library_applet" ]] || fail "system LibraryApplet lifecycle source is missing"
grep -Fq 'appletHolderCheckFinished(&g_MenuHolder)' "$library_applet" ||
  fail "menu holder must verify completion after state-change notifications"

if grep -Fq 'g_store->save()' "$main"; then
  fail "production HOME UI must not save workspace state through legacy JSON store"
fi
printf '%s\n' '[home-entrypoint] OK: system HOME UI with IPC, heartbeat, touch and title/NRO launch is selected.'
