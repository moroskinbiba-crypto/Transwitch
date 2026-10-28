#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Install the experimental DeskNX HOME backend onto a mounted SD-card root.

Usage:
  bash scripts/install-system.sh --sd-root PATH --acknowledge-risk [--backup-existing]

The script only copies an already-built package from ./SdOut. It does not build
or validate the package for your firmware. Do not use it on a primary console
before testing recovery on a recoverable setup.
USAGE
}

SD_ROOT=""
ACK=0
BACKUP_EXISTING=0
while (($#)); do
  case "$1" in
    --sd-root) [[ $# -ge 2 ]] || { usage; exit 2; }; SD_ROOT="$2"; shift 2 ;;
    --acknowledge-risk) ACK=1; shift ;;
    --backup-existing) BACKUP_EXISTING=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

[[ "$ACK" == 1 ]] || { echo "Refusing: review docs/HOME_INTEGRATION.md, then pass --acknowledge-risk." >&2; exit 2; }
[[ -n "$SD_ROOT" && -d "$SD_ROOT" ]] || { echo "SD root is missing or not a directory." >&2; usage; exit 2; }
SD_ROOT="$(cd "$SD_ROOT" && pwd -P)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
PACKAGE="${DESKNX_PACKAGE_DIR:-$REPO_ROOT/SdOut}"
SRC_QLAUNCH="$PACKAGE/atmosphere/contents/0100000000001000"
SRC_MENU="$PACKAGE/switch/DeskNX/bin/Menu"
SRC_HBLOADER="$PACKAGE/switch/DeskNX/bin/hbloader"
SRC_NRO="$PACKAGE/switch/DeskNX/DeskNX.nro"
TARGET_QLAUNCH="$SD_ROOT/atmosphere/contents/0100000000001000"
TARGET_MENU="$SD_ROOT/switch/DeskNX/bin/Menu"
TARGET_HBLOADER="$SD_ROOT/switch/DeskNX/bin/hbloader"
TARGET_NRO="$SD_ROOT/switch/DeskNX/DeskNX.nro"
MANIFEST="$SD_ROOT/switch/DeskNX/install-manifest.txt"
MARKER="DeskNX qlaunch override v0.2.0"

for required in "$SRC_QLAUNCH/exefs/main" "$SRC_QLAUNCH/exefs/main.npdm" "$SRC_MENU/main" "$SRC_MENU/main.npdm" "$SRC_HBLOADER/main" "$SRC_HBLOADER/main.npdm" "$SRC_NRO"; do
  [[ -f "$required" && -s "$required" ]] || {
    echo "Required build output is missing or empty: $required" >&2
    echo "Build the standalone app, library applet and matching system backend first." >&2
    exit 1
  }
done

[[ ! -e "$MANIFEST" ]] || {
  echo "An install manifest already exists at $MANIFEST. Uninstall the current DeskNX version first." >&2
  exit 1
}

# Refuse before making any changes if an override already exists and the caller
# did not explicitly request a persistent backup.
if [[ -e "$TARGET_QLAUNCH" && "$BACKUP_EXISTING" != 1 ]]; then
  echo "A qlaunch override already exists at $TARGET_QLAUNCH." >&2
  echo "Use --backup-existing to preserve it before installing DeskNX." >&2
  exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)-$$"
BACKUP_ROOT="$SD_ROOT/switch/DeskNX/backup"
mkdir -p "$BACKUP_ROOT" "$SD_ROOT/atmosphere/contents" "$SD_ROOT/switch/DeskNX/bin"

BACKUP_REL="none"
MENU_BACKUP_REL="none"
HBLOADER_BACKUP_REL="none"
NRO_BACKUP_REL="none"

# Persist backups outside the transaction staging area. They remain available
# after installation so uninstall can restore the exact previous files.
if [[ -e "$TARGET_QLAUNCH" ]]; then
  BACKUP_REL="switch/DeskNX/backup/qlaunch-$STAMP"
  cp -a "$TARGET_QLAUNCH" "$SD_ROOT/$BACKUP_REL"
fi
if [[ -e "$TARGET_MENU" ]]; then
  MENU_BACKUP_REL="switch/DeskNX/backup/menu-$STAMP"
  cp -a "$TARGET_MENU" "$SD_ROOT/$MENU_BACKUP_REL"
fi
if [[ -e "$TARGET_HBLOADER" ]]; then
  HBLOADER_BACKUP_REL="switch/DeskNX/backup/hbloader-$STAMP"
  cp -a "$TARGET_HBLOADER" "$SD_ROOT/$HBLOADER_BACKUP_REL"
fi
if [[ -f "$TARGET_NRO" ]]; then
  NRO_BACKUP_REL="switch/DeskNX/backup/DeskNX-nro-$STAMP.bak"
  cp -p "$TARGET_NRO" "$SD_ROOT/$NRO_BACKUP_REL"
fi

STAGE="$SD_ROOT/atmosphere/contents/.0100000000001000.desknx-stage-$$"
ROLLBACK="$SD_ROOT/atmosphere/contents/.0100000000001000.desknx-rollback-$$"
MENU_STAGE="$SD_ROOT/switch/DeskNX/bin/.Menu-stage-$"
MENU_ROLLBACK="$SD_ROOT/switch/DeskNX/bin/.Menu-rollback-$"
HBLOADER_STAGE="$SD_ROOT/switch/DeskNX/bin/.hbloader-stage-$"
HBLOADER_ROLLBACK="$SD_ROOT/switch/DeskNX/bin/.hbloader-rollback-$"
NRO_STAGE="$SD_ROOT/switch/DeskNX/.DeskNX-nro-stage-$$"
NRO_ROLLBACK="$SD_ROOT/switch/DeskNX/.DeskNX-nro-rollback-$$"
MANIFEST_STAGE="$SD_ROOT/switch/DeskNX/.install-manifest-stage-$$"
QLAUNCH_INSTALLED=0
MENU_INSTALLED=0
HBLOADER_INSTALLED=0
NRO_INSTALLED=0
COMMITTED=0

hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    echo "no-sha256-tool"
  fi
}

rollback_dir() {
  local target="$1" old="$2" installed="$3"
  if [[ -e "$old" ]]; then
    rm -rf "$target"
    mkdir -p "$(dirname "$target")"
    mv "$old" "$target"
  elif [[ "$installed" == 1 ]]; then
    rm -rf "$target"
  fi
}
rollback_file() {
  local target="$1" old="$2" installed="$3"
  if [[ -e "$old" ]]; then
    rm -f "$target"
    mkdir -p "$(dirname "$target")"
    mv "$old" "$target"
  elif [[ "$installed" == 1 ]]; then
    rm -f "$target"
  fi
}
cleanup() {
  local rc=$?
  trap - EXIT
  if [[ "$COMMITTED" != 1 ]]; then
    rollback_dir "$TARGET_QLAUNCH" "$ROLLBACK" "$QLAUNCH_INSTALLED"
    rollback_dir "$TARGET_MENU" "$MENU_ROLLBACK" "$MENU_INSTALLED"
    rollback_dir "$TARGET_HBLOADER" "$HBLOADER_ROLLBACK" "$HBLOADER_INSTALLED"
    rollback_file "$TARGET_NRO" "$NRO_ROLLBACK" "$NRO_INSTALLED"
    rm -f "$MANIFEST"
  fi
  rm -rf "$STAGE" "$MENU_STAGE" "$HBLOADER_STAGE" "$NRO_STAGE" "$MANIFEST_STAGE"
  # Rollback locations should be empty on success. Keep any unexpected
  # contents rather than deleting evidence of a partial failure.
  [[ ! -e "$ROLLBACK" ]] || { echo "Recovery copy remains at $ROLLBACK" >&2; rc=1; }
  [[ ! -e "$MENU_ROLLBACK" ]] || { echo "Recovery copy remains at $MENU_ROLLBACK" >&2; rc=1; }
  [[ ! -e "$HBLOADER_ROLLBACK" ]] || { echo "Recovery copy remains at $HBLOADER_ROLLBACK" >&2; rc=1; }
  [[ ! -e "$NRO_ROLLBACK" ]] || { echo "Recovery copy remains at $NRO_ROLLBACK" >&2; rc=1; }
  exit "$rc"
}
trap cleanup EXIT

# Prepare all new files before touching the installed paths.
mkdir -p "$STAGE/exefs" "$MENU_STAGE" "$HBLOADER_STAGE"
cp -p "$SRC_QLAUNCH/exefs/main" "$STAGE/exefs/main"
cp -p "$SRC_QLAUNCH/exefs/main.npdm" "$STAGE/exefs/main.npdm"
printf '%s\n' "$MARKER" > "$STAGE/exefs/DeskNX.install-marker"
cp -p "$SRC_MENU/main" "$MENU_STAGE/main"
cp -p "$SRC_MENU/main.npdm" "$MENU_STAGE/main.npdm"
cp -p "$SRC_HBLOADER/main" "$HBLOADER_STAGE/main"
cp -p "$SRC_HBLOADER/main.npdm" "$HBLOADER_STAGE/main.npdm"
cp -p "$SRC_NRO" "$NRO_STAGE"

cat > "$MANIFEST_STAGE" <<EOF
DESKNX_MANIFEST_V1
version=0.2.0
qlaunch_backup=$BACKUP_REL
menu_backup=$MENU_BACKUP_REL
nro_backup=$NRO_BACKUP_REL
hbloader_backup=$HBLOADER_BACKUP_REL
main_sha256=$(hash_file "$STAGE/exefs/main")
npdm_sha256=$(hash_file "$STAGE/exefs/main.npdm")
EOF

# Swap each target through a same-filesystem rename. The EXIT trap restores
# every prior path if any later step fails before the manifest is committed.
if [[ -e "$TARGET_MENU" ]]; then mv "$TARGET_MENU" "$MENU_ROLLBACK"; fi
MENU_INSTALLED=1
mv "$MENU_STAGE" "$TARGET_MENU"

if [[ -e "$TARGET_HBLOADER" ]]; then mv "$TARGET_HBLOADER" "$HBLOADER_ROLLBACK"; fi
HBLOADER_INSTALLED=1
mv "$HBLOADER_STAGE" "$TARGET_HBLOADER"

if [[ -e "$TARGET_NRO" ]]; then mv "$TARGET_NRO" "$NRO_ROLLBACK"; fi
NRO_INSTALLED=1
mv "$NRO_STAGE" "$TARGET_NRO"

if [[ -e "$TARGET_QLAUNCH" ]]; then mv "$TARGET_QLAUNCH" "$ROLLBACK"; fi
QLAUNCH_INSTALLED=1
mv "$STAGE" "$TARGET_QLAUNCH"

mv "$MANIFEST_STAGE" "$MANIFEST"
COMMITTED=1

# Persistent backups now exist, so transient rollback paths may be deleted.
rm -rf "$ROLLBACK" "$MENU_ROLLBACK" "$HBLOADER_ROLLBACK" "$NRO_ROLLBACK"
echo "DeskNX files staged on SD root: $SD_ROOT"
echo "Previous qlaunch override backup: $BACKUP_REL"
echo "Previous menu backup: $MENU_BACKUP_REL"
echo "Previous hbloader backup: $HBLOADER_BACKUP_REL"
echo "Previous DeskNX NRO backup: $NRO_BACKUP_REL"
echo "Install files and manifest were written. This does not prove runtime compatibility; keep recovery access available."
