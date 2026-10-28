#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Uninstall the DeskNX HOME backend and restore any qlaunch override backed up by the installer.

Usage:
  scripts/uninstall-system.sh --sd-root PATH --acknowledge-risk [--force-changed-files]

The current DeskNX qlaunch files are retained in switch/DeskNX/backup before removal.
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
[[ "$ACK" == 1 ]] || { echo "Refusing: pass --acknowledge-risk after reviewing docs/HOME_INTEGRATION.md." >&2; exit 2; }
[[ -n "$SD_ROOT" && -d "$SD_ROOT" ]] || { usage; exit 2; }
SD_ROOT="$(cd "$SD_ROOT" && pwd -P)"

MANIFEST="$SD_ROOT/switch/DeskNX/install-manifest.txt"
TARGET_QLAUNCH="$SD_ROOT/atmosphere/contents/0100000000001000"
TARGET_MENU="$SD_ROOT/switch/DeskNX/bin/Menu"
TARGET_NRO="$SD_ROOT/switch/DeskNX/DeskNX.nro"
[[ -s "$MANIFEST" ]] || { echo "DeskNX install manifest not found: $MANIFEST" >&2; exit 1; }
[[ -f "$TARGET_QLAUNCH/exefs/DeskNX.install-marker" ]] || { echo "DeskNX ownership marker not found; refusing to remove this qlaunch override." >&2; exit 1; }
[[ "$(cat "$TARGET_QLAUNCH/exefs/DeskNX.install-marker")" == "DeskNX qlaunch override v0.2.0" ]] || { echo "Unexpected ownership marker contents; refusing." >&2; exit 1; }

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
  echo "Invalid backup path in manifest; refusing to use it." >&2; exit 1;
}
[[ "$menu_backup_rel" == "none" || "$menu_backup_rel" =~ ^switch/DeskNX/backup/menu-[A-Za-z0-9T_-]+$ ]] || {
  echo "Invalid menu backup path in manifest; refusing to use it." >&2; exit 1;
}
[[ "$nro_backup_rel" == "none" || "$nro_backup_rel" =~ ^switch/DeskNX/backup/DeskNX-nro-[A-Za-z0-9T_-]+\.bak$ ]] || {
  echo "Invalid NRO backup path in manifest; refusing to use it." >&2; exit 1;
}

hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else echo "no-sha256-tool"; fi
}
actual_main="$(hash_file "$TARGET_QLAUNCH/exefs/main")"
actual_npdm="$(hash_file "$TARGET_QLAUNCH/exefs/main.npdm")"
if [[ "$FORCE_CHANGED" != 1 && ( "$actual_main" != "$expected_main" || "$actual_npdm" != "$expected_npdm" ) ]]; then
  echo "Installed qlaunch files changed since installation." >&2
  echo "Review them first, or pass --force-changed-files to preserve them and continue." >&2
  exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
CURRENT_BACKUP_REL="switch/DeskNX/backup/desknx-installed-$STAMP"
mkdir -p "$SD_ROOT/switch/DeskNX/backup"

# Moving the entire override preserves unexpected files that another tool may
# have placed beside DeskNX's main/main.npdm after installation.
mv "$TARGET_QLAUNCH" "$SD_ROOT/$CURRENT_BACKUP_REL"

if [[ "$backup_rel" != "none" ]]; then
  if [[ ! -d "$SD_ROOT/$backup_rel" ]]; then
    echo "Recorded qlaunch backup is missing: $backup_rel" >&2
    mv "$SD_ROOT/$CURRENT_BACKUP_REL" "$TARGET_QLAUNCH"
    exit 1
  fi
  mkdir -p "$(dirname "$TARGET_QLAUNCH")"
  mv "$SD_ROOT/$backup_rel" "$TARGET_QLAUNCH"
fi

if [[ "$menu_backup_rel" != "none" && -d "$SD_ROOT/$menu_backup_rel" ]]; then
  [[ ! -e "$TARGET_MENU" ]] || mv "$TARGET_MENU" "$SD_ROOT/switch/DeskNX/backup/desknx-menu-$STAMP"
  mkdir -p "$(dirname "$TARGET_MENU")"
  mv "$SD_ROOT/$menu_backup_rel" "$TARGET_MENU"
fi

if [[ "$nro_backup_rel" != "none" && -f "$SD_ROOT/$nro_backup_rel" ]]; then
  [[ ! -e "$TARGET_NRO" ]] || mv "$TARGET_NRO" "$SD_ROOT/switch/DeskNX/backup/desknx-nro-$STAMP"
  mv "$SD_ROOT/$nro_backup_rel" "$TARGET_NRO"
fi

rm -f "$MANIFEST"
echo "DeskNX HOME override removed."
if [[ "$backup_rel" == "none" ]]; then
  echo "No previous qlaunch override was present; the system's built-in HOME menu should be used on next boot."
else
  echo "Previous qlaunch override restored from $backup_rel."
fi
echo "The removed DeskNX override was preserved at $CURRENT_BACKUP_REL."
