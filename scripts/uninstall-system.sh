#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Uninstall the DeskNX HOME backend and restore any qlaunch override backed up by
the installer. The removed DeskNX files are preserved under switch/DeskNX/backup.

Usage:
  bash scripts/uninstall-system.sh --sd-root PATH --acknowledge-risk [--force-changed-files]
USAGE
}

SD_ROOT=""
ACK=0
FORCE_CHANGED=0
while (($#)); do
  case "$1" in
    --sd-root) [[ $# -ge 2 ]] || { usage; exit 2; }; SD_ROOT="$2"; shift 2 ;;
    --acknowledge-risk) ACK=1; shift ;;
    --force-changed-files) FORCE_CHANGED=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done
[[ "$ACK" == 1 ]] || { echo "Refusing: review docs/HOME_INTEGRATION.md, then pass --acknowledge-risk." >&2; exit 2; }
[[ -n "$SD_ROOT" && -d "$SD_ROOT" ]] || { echo "SD root is missing or not a directory." >&2; usage; exit 2; }
SD_ROOT="$(cd "$SD_ROOT" && pwd -P)"

DESKNX_ROOT="$SD_ROOT/switch/DeskNX"
MANIFEST="$DESKNX_ROOT/install-manifest.txt"
TARGET_QLAUNCH="$SD_ROOT/atmosphere/contents/0100000000001000"
TARGET_MENU="$DESKNX_ROOT/bin/Menu"
TARGET_NRO="$DESKNX_ROOT/DeskNX.nro"
[[ -s "$MANIFEST" ]] || { echo "DeskNX install manifest not found: $MANIFEST" >&2; exit 1; }
[[ -f "$TARGET_QLAUNCH/exefs/DeskNX.install-marker" ]] || { echo "DeskNX ownership marker not found; refusing to remove this qlaunch override." >&2; exit 1; }
[[ "$(cat "$TARGET_QLAUNCH/exefs/DeskNX.install-marker")" == "DeskNX qlaunch override v0.2.0" ]] || {
  echo "Unexpected ownership marker contents; refusing." >&2; exit 1;
}

manifest_get() {
  local key="$1"
  awk -F= -v wanted="$key" '$1 == wanted {sub(/^[^=]*=/, ""); print; exit}' "$MANIFEST"
}
backup_rel="$(manifest_get qlaunch_backup)"
menu_backup_rel="$(manifest_get menu_backup)"
nro_backup_rel="$(manifest_get nro_backup)"
expected_main="$(manifest_get main_sha256)"
expected_npdm="$(manifest_get npdm_sha256)"

[[ "$backup_rel" == "none" || "$backup_rel" =~ ^switch/DeskNX/backup/qlaunch-[A-Za-z0-9T_-]+$ ]] || {
  echo "Invalid qlaunch backup path in manifest; refusing." >&2; exit 1;
}
[[ "$menu_backup_rel" == "none" || "$menu_backup_rel" =~ ^switch/DeskNX/backup/menu-[A-Za-z0-9T_-]+$ ]] || {
  echo "Invalid menu backup path in manifest; refusing." >&2; exit 1;
}
[[ "$nro_backup_rel" == "none" || "$nro_backup_rel" =~ ^switch/DeskNX/backup/DeskNX-nro-[A-Za-z0-9T_-]+\.bak$ ]] || {
  echo "Invalid NRO backup path in manifest; refusing." >&2; exit 1;
}

[[ "$backup_rel" == "none" || -d "$SD_ROOT/$backup_rel" ]] || { echo "Recorded qlaunch backup is missing: $backup_rel" >&2; exit 1; }
[[ "$menu_backup_rel" == "none" || -d "$SD_ROOT/$menu_backup_rel" ]] || { echo "Recorded menu backup is missing: $menu_backup_rel" >&2; exit 1; }
[[ "$nro_backup_rel" == "none" || -f "$SD_ROOT/$nro_backup_rel" ]] || { echo "Recorded NRO backup is missing: $nro_backup_rel" >&2; exit 1; }

hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    echo "no-sha256-tool"
  fi
}
actual_main="$(hash_file "$TARGET_QLAUNCH/exefs/main")"
actual_npdm="$(hash_file "$TARGET_QLAUNCH/exefs/main.npdm")"
if [[ "$FORCE_CHANGED" != 1 && ( "$actual_main" != "$expected_main" || "$actual_npdm" != "$expected_npdm" ) ]]; then
  echo "Installed qlaunch files changed since installation." >&2
  echo "Review them first, or pass --force-changed-files to preserve them and continue." >&2
  exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)-$$"
BACKUP_ROOT="$DESKNX_ROOT/backup"
mkdir -p "$BACKUP_ROOT"
CURRENT_QLAUNCH="$BACKUP_ROOT/desknx-installed-$STAMP"
CURRENT_MENU="$BACKUP_ROOT/desknx-menu-$STAMP"
CURRENT_NRO="$BACKUP_ROOT/desknx-nro-$STAMP.bak"
STAGE="$DESKNX_ROOT/.uninstall-stage-$$"
MANIFEST_ARCHIVE="$DESKNX_ROOT/.install-manifest-uninstall-$$"

HAD_QLAUNCH=0
HAD_MENU=0
HAD_NRO=0
RESTORED_QLAUNCH=0
RESTORED_MENU=0
RESTORED_NRO=0
COMMITTED=0

rollback_dir() {
  local target="$1" current="$2" had="$3" restored="$4"
  if [[ "$had" == 1 && -e "$current" ]]; then
    rm -rf "$target"
    mkdir -p "$(dirname "$target")"
    mv "$current" "$target"
  elif [[ "$restored" == 1 ]]; then
    rm -rf "$target"
  fi
}
rollback_file() {
  local target="$1" current="$2" had="$3" restored="$4"
  if [[ "$had" == 1 && -e "$current" ]]; then
    rm -f "$target"
    mkdir -p "$(dirname "$target")"
    mv "$current" "$target"
  elif [[ "$restored" == 1 ]]; then
    rm -f "$target"
  fi
}
cleanup() {
  local rc=$?
  trap - EXIT
  if [[ "$COMMITTED" != 1 ]]; then
    rollback_dir "$TARGET_QLAUNCH" "$CURRENT_QLAUNCH" "$HAD_QLAUNCH" "$RESTORED_QLAUNCH"
    rollback_dir "$TARGET_MENU" "$CURRENT_MENU" "$HAD_MENU" "$RESTORED_MENU"
    rollback_file "$TARGET_NRO" "$CURRENT_NRO" "$HAD_NRO" "$RESTORED_NRO"
    if [[ -e "$MANIFEST_ARCHIVE" && ! -e "$MANIFEST" ]]; then
      mv "$MANIFEST_ARCHIVE" "$MANIFEST"
    fi
  fi
  rm -rf "$STAGE"
  exit "$rc"
}
trap cleanup EXIT

mkdir -p "$STAGE"
# Copy prior versions into a staging area first; leave the persistent backups
# untouched until every replacement has been placed successfully.
if [[ "$backup_rel" != "none" ]]; then cp -a "$SD_ROOT/$backup_rel" "$STAGE/qlaunch"; fi
if [[ "$menu_backup_rel" != "none" ]]; then cp -a "$SD_ROOT/$menu_backup_rel" "$STAGE/menu"; fi
if [[ "$nro_backup_rel" != "none" ]]; then cp -p "$SD_ROOT/$nro_backup_rel" "$STAGE/nro"; fi

if [[ -e "$TARGET_QLAUNCH" ]]; then mv "$TARGET_QLAUNCH" "$CURRENT_QLAUNCH"; HAD_QLAUNCH=1; fi
if [[ -e "$TARGET_MENU" ]]; then mv "$TARGET_MENU" "$CURRENT_MENU"; HAD_MENU=1; fi
if [[ -e "$TARGET_NRO" ]]; then mv "$TARGET_NRO" "$CURRENT_NRO"; HAD_NRO=1; fi

if [[ -d "$STAGE/qlaunch" ]]; then
  mkdir -p "$(dirname "$TARGET_QLAUNCH")"
  mv "$STAGE/qlaunch" "$TARGET_QLAUNCH"
  RESTORED_QLAUNCH=1
fi
if [[ -d "$STAGE/menu" ]]; then
  mkdir -p "$(dirname "$TARGET_MENU")"
  mv "$STAGE/menu" "$TARGET_MENU"
  RESTORED_MENU=1
fi
if [[ -f "$STAGE/nro" ]]; then
  mkdir -p "$(dirname "$TARGET_NRO")"
  mv "$STAGE/nro" "$TARGET_NRO"
  RESTORED_NRO=1
fi

# Atomic manifest move is the commit point. If a prior command failed, the
# exit trap restores the just-installed DeskNX files and keeps the manifest.
mv "$MANIFEST" "$MANIFEST_ARCHIVE"
COMMITTED=1
rm -f "$MANIFEST_ARCHIVE"

echo "DeskNX HOME override removed."
if [[ "$backup_rel" == "none" ]]; then
  echo "No previous qlaunch override existed; Atmosphère should fall back to the system HOME implementation."
else
  echo "Previous qlaunch override restored from $backup_rel."
fi
echo "Removed DeskNX qlaunch, menu, and NRO files were preserved under $BACKUP_ROOT."
