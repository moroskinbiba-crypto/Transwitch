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

# Case 1: clean SD root. Install and uninstall should remove the active override
# but preserve the just-installed files under the backup directory.
SD1="$TMP/sd-clean"
mkdir -p "$SD1"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD1" --acknowledge-risk
assert_file_content "$SD1/atmosphere/contents/0100000000001000/exefs/main" "new qlaunch executable"
assert_file_content "$SD1/switch/DeskNX/bin/Menu/main" "new menu executable"
assert_file_content "$SD1/switch/DeskNX/bin/hbloader/main" "new hbloader executable"
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

DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD2" --acknowledge-risk --backup-existing
assert_file_content "$QLAUNCH/exefs/main" "new qlaunch executable"
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

echo "PASS: installer and uninstaller recovery scenarios"
