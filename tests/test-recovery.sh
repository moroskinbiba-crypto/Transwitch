#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_file_content() {
  local path="$1" expected="$2"
  [[ -f "$path" ]] || fail "expected file: $path"
  [[ "$(cat "$path")" == "$expected" ]] || fail "unexpected content in $path"
}
assert_absent() {
  [[ ! -e "$1" ]] || fail "expected path to be absent: $1"
}
make_fake_package() {
  local package="$1"
  mkdir -p "$package/atmosphere/contents/0100000000001000/exefs"
  mkdir -p "$package/switch/DeskNX/bin/Menu" "$package/switch/DeskNX/bin/hbloader"
  printf 'new qlaunch executable\n' > "$package/atmosphere/contents/0100000000001000/exefs/main"
  printf 'new qlaunch npdm\n' > "$package/atmosphere/contents/0100000000001000/exefs/main.npdm"
  printf 'new menu executable\n' > "$package/switch/DeskNX/bin/Menu/main"
  printf 'new menu npdm\n' > "$package/switch/DeskNX/bin/Menu/main.npdm"
  printf 'new hbloader executable\n' > "$package/switch/DeskNX/bin/hbloader/main"
  printf 'new hbloader npdm\n' > "$package/switch/DeskNX/bin/hbloader/main.npdm"
  printf 'new DeskNX NRO\n' > "$package/switch/DeskNX/DeskNX.nro"
}

PACKAGE="$TMP/package"
make_fake_package "$PACKAGE"

# Refuse an accidental host-root target before touching any host paths.
if DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root / --acknowledge-risk; then
  fail "installer accepted / as the SD-card root"
fi
if bash "$ROOT/scripts/uninstall-system.sh" --sd-root / --acknowledge-risk; then
  fail "uninstaller accepted / as the SD-card root"
fi

# Reject destination types that cannot be restored by the manifest contract.
SD_BAD_TYPE="$TMP/sd-bad-target-type"
mkdir -p "$SD_BAD_TYPE/atmosphere/contents" "$SD_BAD_TYPE/switch/DeskNX"
printf 'not a qlaunch directory\n' > "$SD_BAD_TYPE/atmosphere/contents/0100000000001000"
if DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD_BAD_TYPE" --acknowledge-risk --backup-existing; then
  fail "installer accepted a file at the qlaunch directory path"
fi
assert_file_content "$SD_BAD_TYPE/atmosphere/contents/0100000000001000" "not a qlaunch directory"


# A symlinked ancestor must not redirect a write outside the selected SD root.
SD_SYMLINK_PARENT="$TMP/sd-symlink-parent"
OUTSIDE="$TMP/outside-target"
mkdir -p "$SD_SYMLINK_PARENT" "$OUTSIDE"
printf 'outside sentinel\n' > "$OUTSIDE/sentinel"
ln -s "$OUTSIDE" "$SD_SYMLINK_PARENT/switch"
if DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD_SYMLINK_PARENT" --acknowledge-risk; then
  fail "installer accepted a symlinked ancestor directory"
fi
assert_file_content "$OUTSIDE/sentinel" "outside sentinel"
assert_absent "$OUTSIDE/DeskNX"

# Case 1: clean SD root. Install and uninstall should remove the active override
# but preserve the just-installed files under the backup directory.
SD1="$TMP/sd-clean"
mkdir -p "$SD1"
STALE_MENU_STAGE="$SD1/switch/DeskNX/bin/.Menu-stage-\$"
STALE_HBLOADER_STAGE="$SD1/switch/DeskNX/bin/.hbloader-stage-\$"
STALE_MENU_ROLLBACK="$SD1/switch/DeskNX/bin/.Menu-rollback-\$"
STALE_HBLOADER_ROLLBACK="$SD1/switch/DeskNX/bin/.hbloader-rollback-\$"
mkdir -p "$STALE_MENU_STAGE" "$STALE_HBLOADER_STAGE" "$STALE_MENU_ROLLBACK" "$STALE_HBLOADER_ROLLBACK"
printf 'stale menu stage\n' > "$STALE_MENU_STAGE/sentinel"
printf 'stale hbloader stage\n' > "$STALE_HBLOADER_STAGE/sentinel"
printf 'stale menu rollback\n' > "$STALE_MENU_ROLLBACK/sentinel"
printf 'stale hbloader rollback\n' > "$STALE_HBLOADER_ROLLBACK/sentinel"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD1" --acknowledge-risk
assert_file_content "$SD1/atmosphere/contents/0100000000001000/exefs/main" "new qlaunch executable"
assert_file_content "$SD1/switch/DeskNX/bin/Menu/main" "new menu executable"
assert_file_content "$SD1/switch/DeskNX/bin/hbloader/main" "new hbloader executable"
assert_absent "$SD1/switch/DeskNX/bin/Menu/sentinel"
assert_absent "$SD1/switch/DeskNX/bin/hbloader/sentinel"
assert_file_content "$STALE_MENU_STAGE/sentinel" "stale menu stage"
assert_file_content "$STALE_HBLOADER_STAGE/sentinel" "stale hbloader stage"
assert_file_content "$STALE_MENU_ROLLBACK/sentinel" "stale menu rollback"
assert_file_content "$STALE_HBLOADER_ROLLBACK/sentinel" "stale hbloader rollback"
assert_file_content "$SD1/switch/DeskNX/DeskNX.nro" "new DeskNX NRO"
[[ -s "$SD1/switch/DeskNX/install-manifest.txt" ]] || fail "install manifest missing"
bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD1" --acknowledge-risk
assert_absent "$SD1/atmosphere/contents/0100000000001000"
assert_absent "$SD1/switch/DeskNX/bin/Menu"
assert_absent "$SD1/switch/DeskNX/bin/hbloader"
assert_absent "$SD1/switch/DeskNX/DeskNX.nro"
assert_absent "$SD1/switch/DeskNX/install-manifest.txt"
[[ -n "$(find "$SD1/switch/DeskNX/backup" -maxdepth 1 -type d -name 'desknx-installed-*' -print -quit)" ]] || fail "removed qlaunch build was not preserved"

# Case 2: an existing qlaunch override must not be touched without the explicit
# backup flag. With the flag, uninstall restores qlaunch, its NPDM and extra files.
SD2="$TMP/sd-existing"
QLAUNCH="$SD2/atmosphere/contents/0100000000001000"
MENU="$SD2/switch/DeskNX/bin/Menu"
HBLOADER="$SD2/switch/DeskNX/bin/hbloader"
mkdir -p "$QLAUNCH/exefs" "$MENU" "$HBLOADER"
printf 'original qlaunch executable\n' > "$QLAUNCH/exefs/main"
printf 'original qlaunch npdm\n' > "$QLAUNCH/exefs/main.npdm"
printf 'keep me\n' > "$QLAUNCH/extra.txt"
printf 'original menu executable\n' > "$MENU/main"
printf 'original menu npdm\n' > "$MENU/main.npdm"
printf 'original hbloader executable\n' > "$HBLOADER/main"
printf 'original hbloader npdm\n' > "$HBLOADER/main.npdm"
printf 'original DeskNX NRO\n' > "$SD2/switch/DeskNX/DeskNX.nro"

if DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD2" --acknowledge-risk; then
  fail "installer accepted an existing qlaunch override without --backup-existing"
fi
assert_file_content "$QLAUNCH/exefs/main" "original qlaunch executable"
assert_file_content "$QLAUNCH/extra.txt" "keep me"

STALE_MENU_ROLLBACK2="$SD2/switch/DeskNX/bin/.Menu-rollback-\$"
STALE_HBLOADER_ROLLBACK2="$SD2/switch/DeskNX/bin/.hbloader-rollback-\$"
mkdir -p "$STALE_MENU_ROLLBACK2" "$STALE_HBLOADER_ROLLBACK2"
printf 'pre-existing menu rollback path\n' > "$STALE_MENU_ROLLBACK2/sentinel"
printf 'pre-existing hbloader rollback path\n' > "$STALE_HBLOADER_ROLLBACK2/sentinel"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD2" --acknowledge-risk --backup-existing
assert_file_content "$QLAUNCH/exefs/main" "new qlaunch executable"
assert_file_content "$STALE_MENU_ROLLBACK2/sentinel" "pre-existing menu rollback path"
assert_file_content "$STALE_HBLOADER_ROLLBACK2/sentinel" "pre-existing hbloader rollback path"
printf 'tampered after install\n' > "$QLAUNCH/exefs/main"
if bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD2" --acknowledge-risk; then
  fail "uninstaller accepted a modified qlaunch without --force-changed-files"
fi
[[ -s "$SD2/switch/DeskNX/install-manifest.txt" ]] || fail "manifest disappeared after refused uninstall"

bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD2" --acknowledge-risk --force-changed-files
assert_file_content "$QLAUNCH/exefs/main" "original qlaunch executable"
assert_file_content "$QLAUNCH/exefs/main.npdm" "original qlaunch npdm"
assert_file_content "$QLAUNCH/extra.txt" "keep me"
assert_file_content "$MENU/main" "original menu executable"
assert_file_content "$MENU/main.npdm" "original menu npdm"
assert_file_content "$HBLOADER/main" "original hbloader executable"
assert_file_content "$HBLOADER/main.npdm" "original hbloader npdm"
assert_file_content "$SD2/switch/DeskNX/DeskNX.nro" "original DeskNX NRO"
assert_absent "$SD2/switch/DeskNX/install-manifest.txt"

# Case 3: the emergency recovery chord moves qlaunch to the disabled path.
# Uninstall must still validate and archive that DeskNX build while restoring
# the previous qlaunch override to its canonical path.
SD3="$TMP/sd-emergency-disabled"
QLAUNCH3="$SD3/atmosphere/contents/0100000000001000"
DISABLED3="$SD3/atmosphere/contents/0100000000001000.disabled-DeskNX"
mkdir -p "$QLAUNCH3/exefs"
printf 'original qlaunch executable\n' > "$QLAUNCH3/exefs/main"
printf 'original qlaunch npdm\n' > "$QLAUNCH3/exefs/main.npdm"
printf 'keep qlaunch metadata\n' > "$QLAUNCH3/extra.txt"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD3" --acknowledge-risk --backup-existing
mv "$QLAUNCH3" "$DISABLED3"
bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD3" --acknowledge-risk
assert_file_content "$QLAUNCH3/exefs/main" "original qlaunch executable"
assert_file_content "$QLAUNCH3/exefs/main.npdm" "original qlaunch npdm"
assert_file_content "$QLAUNCH3/extra.txt" "keep qlaunch metadata"
assert_absent "$DISABLED3"
assert_absent "$SD3/switch/DeskNX/install-manifest.txt"
[[ -n "$(find "$SD3/switch/DeskNX/backup" -mindepth 1 -maxdepth 1 -type d -name 'desknx-installed-*' -print -quit)" ]] || fail "emergency-disabled DeskNX override was not archived"

# The uninstaller must also refuse symlink ancestors/backups and leave the
# installed files plus manifest untouched until the links are repaired.
SD4="$TMP/sd-symlink-uninstall"
mkdir -p "$SD4"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD4" --acknowledge-risk
mv "$SD4/switch" "$SD4/switch-real"
ln -s switch-real "$SD4/switch"
if bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD4" --acknowledge-risk; then
  fail "uninstaller accepted a symlinked /switch ancestor"
fi
assert_file_content "$SD4/atmosphere/contents/0100000000001000/exefs/DeskNX.install-marker" "DeskNX qlaunch override v0.2.0"
[[ -s "$SD4/switch/switch-not-real" ]] && fail "unexpected sentinel state"
rm "$SD4/switch"
mv "$SD4/switch-real" "$SD4/switch"
mv "$SD4/switch/DeskNX/backup" "$SD4/switch/DeskNX/backup-real"
ln -s backup-real "$SD4/switch/DeskNX/backup"
if bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD4" --acknowledge-risk; then
  fail "uninstaller accepted a symlinked backup directory"
fi
assert_file_content "$SD4/atmosphere/contents/0100000000001000/exefs/DeskNX.install-marker" "DeskNX qlaunch override v0.2.0"
rm "$SD4/switch/DeskNX/backup"
mv "$SD4/switch/DeskNX/backup-real" "$SD4/switch/DeskNX/backup"
bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD4" --acknowledge-risk
assert_absent "$SD4/atmosphere/contents/0100000000001000"
assert_absent "$SD4/switch/DeskNX/install-manifest.txt"

echo "PASS: installer and uninstaller recovery scenarios, including symlink ancestors and emergency-disabled qlaunch"
