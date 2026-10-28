#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Install the DeskNX HOME backend to an SD-card root.

Usage:
  scripts/install-system.sh --sd-root PATH --acknowledge-risk [--backup-existing]

This installs the built qlaunch override from ./SdOut. It is intentionally
separate from the normal DeskNX NRO installer. Test the build on a recoverable
console first. --backup-existing is required if a qlaunch override already exists.
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

[[ "$ACK" == 1 ]] || { echo "Refusing: pass --acknowledge-risk after reviewing docs/HOME_INTEGRATION.md." >&2; exit 2; }
[[ -n "$SD_ROOT" ]] || { usage; exit 2; }
[[ -d "$SD_ROOT" ]] || { echo "SD root does not exist: $SD_ROOT" >&2; exit 2; }
SD_ROOT="$(cd "$SD_ROOT" && pwd -P)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
PACKAGE="$REPO_ROOT/SdOut"
SRC_QLAUNCH="$PACKAGE/atmosphere/contents/0100000000001000"
SRC_MENU="$PACKAGE/switch/DeskNX/bin/Menu"
SRC_NRO="$PACKAGE/switch/DeskNX/DeskNX.nro"
TARGET_QLAUNCH="$SD_ROOT/atmosphere/contents/0100000000001000"
TARGET_MENU="$SD_ROOT/switch/DeskNX/bin/Menu"
MANIFEST="$SD_ROOT/switch/DeskNX/install-manifest.txt"
MARKER="DeskNX qlaunch override v0.2.0"

for required in "$SRC_QLAUNCH/exefs/main" "$SRC_QLAUNCH/exefs/main.npdm" "$SRC_MENU/main" "$SRC_MENU/main.npdm" "$SRC_NRO"; do
  [[ -s "$required" ]] || { echo "Required build output is missing or empty: $required" >&2; echo "Run make system-package first." >&2; exit 1; }
done

mkdir -p "$SD_ROOT/switch/DeskNX/backup" "$SD_ROOT/atmosphere/contents"
[[ ! -e "$MANIFEST" ]] || { echo "A DeskNX install manifest already exists at $MANIFEST. Uninstall the current version first." >&2; exit 1; }

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_REL="none"
if [[ -e "$TARGET_QLAUNCH" ]]; then
  if [[ "$BACKUP_EXISTING" != 1 ]]; then
    echo "A qlaunch override already exists at $TARGET_QLAUNCH." >&2
    echo "Use --backup-existing to preserve it before installing DeskNX." >&2
    exit 1
  fi
  BACKUP_REL="switch/DeskNX/backup/qlaunch-$STAMP"
  [[ ! -e "$SD_ROOT/$BACKUP_REL" ]] || { echo "Backup path already exists: $BACKUP_REL" >&2; exit 1; }
  cp -a "$TARGET_QLAUNCH" "$SD_ROOT/$BACKUP_REL"
fi

STAGE="$SD_ROOT/atmosphere/contents/.0100000000001000.desknx-stage-$$"
ROLLBACK="$SD_ROOT/atmosphere/contents/.0100000000001000.desknx-rollback-$$"
cleanup() {
  [[ ! -e "$STAGE" ]] || rm -rf "$STAGE"
  if [[ -e "$ROLLBACK" && ! -e "$TARGET_QLAUNCH" ]]; then
    mv "$ROLLBACK" "$TARGET_QLAUNCH"
  fi
}
trap cleanup EXIT

mkdir -p "$STAGE/exefs"
cp -f "$SRC_QLAUNCH/exefs/main" "$STAGE/exefs/main"
cp -f "$SRC_QLAUNCH/exefs/main.npdm" "$STAGE/exefs/main.npdm"
printf '%s\n' "$MARKER" > "$STAGE/exefs/DeskNX.install-marker"

# Stage every non-qlaunch file first, then replace the title directory by rename.
mkdir -p "$SD_ROOT/switch/DeskNX/bin"
MENU_STAGE="$SD_ROOT/switch/DeskNX/bin/.Menu-stage-$$"
mkdir -p "$MENU_STAGE"
cp -f "$SRC_MENU/main" "$MENU_STAGE/main"
cp -f "$SRC_MENU/main.npdm" "$MENU_STAGE/main.npdm"
rm -rf "$TARGET_MENU"
mv "$MENU_STAGE" "$TARGET_MENU"
cp -f "$SRC_NRO" "$SD_ROOT/switch/DeskNX/DeskNX.nro"

if [[ -e "$TARGET_QLAUNCH" ]]; then
  mv "$TARGET_QLAUNCH" "$ROLLBACK"
fi
if ! mv "$STAGE" "$TARGET_QLAUNCH"; then
  [[ ! -e "$ROLLBACK" ]] || mv "$ROLLBACK" "$TARGET_QLAUNCH"
  echo "Could not place the qlaunch override; original override restored where possible." >&2
  exit 1
fi
[[ ! -e "$ROLLBACK" ]] || rm -rf "$ROLLBACK"

hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else echo "no-sha256-tool"; fi
}
mkdir -p "$(dirname "$MANIFEST")"
cat > "$MANIFEST" <<EOF
DESKNX_MANIFEST_V1
version=0.2.0
qlaunch_backup=$BACKUP_REL
main_sha256=$(hash_file "$TARGET_QLAUNCH/exefs/main")
npdm_sha256=$(hash_file "$TARGET_QLAUNCH/exefs/main.npdm")
EOF

trap - EXIT
echo "DeskNX system files were copied to: $SD_ROOT"
echo "Previous qlaunch override backup: $BACKUP_REL"
echo "This does not prove the HOME override works on this firmware. Keep the backup and recovery instructions available."
