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

make_one_shot_mv_failure_wrapper() {
  local wrapper_dir="$1"
  mkdir -p "$wrapper_dir"
  cat > "$wrapper_dir/mv" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
real_mv="${DESKNX_TEST_REAL_MV:?}"
dest="${DESKNX_TEST_MV_FAIL_DEST:?}"
needle="${DESKNX_TEST_MV_FAIL_SOURCE_NEEDLE:?}"
marker="${DESKNX_TEST_MV_FAIL_MARKER:?}"
if [[ "${2:-}" == "$dest" && "${1:-}" == *"$needle"* && ! -e "$marker" ]]; then
  : > "$marker"
  echo "Injected one-shot mv failure: $1 -> $2" >&2
  exit 73
fi
exec "$real_mv" "$@"
WRAPPER
  chmod +x "$wrapper_dir/mv"
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


# Case 5: inject a one-shot rename failure while publishing the staged qlaunch
# directory. The install trap must restore qlaunch and every side-by-side file.
SD5="$TMP/sd-install-failure"
QLAUNCH5="$SD5/atmosphere/contents/0100000000001000"
MENU5="$SD5/switch/DeskNX/bin/Menu"
HBLOADER5="$SD5/switch/DeskNX/bin/hbloader"
NRO5="$SD5/switch/DeskNX/DeskNX.nro"
mkdir -p "$QLAUNCH5/exefs" "$MENU5" "$HBLOADER5" "$(dirname "$NRO5")"
printf 'old qlaunch executable\n' > "$QLAUNCH5/exefs/main"
printf 'old qlaunch npdm\n' > "$QLAUNCH5/exefs/main.npdm"
printf 'old menu executable\n' > "$MENU5/main"
printf 'old menu npdm\n' > "$MENU5/main.npdm"
printf 'old hbloader executable\n' > "$HBLOADER5/main"
printf 'old hbloader npdm\n' > "$HBLOADER5/main.npdm"
printf 'old DeskNX NRO\n' > "$NRO5"
MV_FAIL_BIN="$TMP/fail-mv-install"
make_one_shot_mv_failure_wrapper "$MV_FAIL_BIN"
if DESKNX_PACKAGE_DIR="$PACKAGE" \
   DESKNX_TEST_REAL_MV="$(command -v mv)" \
   DESKNX_TEST_MV_FAIL_DEST="$QLAUNCH5" \
   DESKNX_TEST_MV_FAIL_SOURCE_NEEDLE=".0100000000001000.desknx-stage-" \
   DESKNX_TEST_MV_FAIL_MARKER="$TMP/install-mv-failed" \
   PATH="$MV_FAIL_BIN:$PATH" \
   bash "$ROOT/scripts/install-system.sh" --sd-root "$SD5" --acknowledge-risk --backup-existing; then
  fail "installer succeeded despite injected qlaunch publish failure"
fi
[[ -f "$TMP/install-mv-failed" ]] || fail "installer failure injection did not trigger"
assert_file_content "$QLAUNCH5/exefs/main" "old qlaunch executable"
assert_file_content "$QLAUNCH5/exefs/main.npdm" "old qlaunch npdm"
assert_file_content "$MENU5/main" "old menu executable"
assert_file_content "$HBLOADER5/main" "old hbloader executable"
assert_file_content "$NRO5" "old DeskNX NRO"
assert_absent "$QLAUNCH5/exefs/DeskNX.install-marker"
assert_absent "$SD5/switch/DeskNX/install-manifest.txt"
[[ -z "$(find "$SD5/atmosphere/contents" -maxdepth 1 -name '.0100000000001000.desknx-stage-*' -print -quit)" ]] || fail "installer left qlaunch stage directory"
[[ -z "$(find "$SD5/atmosphere/contents" -maxdepth 1 -name '.0100000000001000.desknx-rollback-*' -print -quit)" ]] || fail "installer left qlaunch rollback directory"

# Case 6: inject a rename failure while the uninstaller is restoring the previous
# qlaunch directory. Its EXIT trap must put the installed DeskNX package back,
# retaining the ownership marker and manifest so recovery can be retried.
SD6="$TMP/sd-uninstall-failure"
QLAUNCH6="$SD6/atmosphere/contents/0100000000001000"
MENU6="$SD6/switch/DeskNX/bin/Menu"
HBLOADER6="$SD6/switch/DeskNX/bin/hbloader"
NRO6="$SD6/switch/DeskNX/DeskNX.nro"
mkdir -p "$QLAUNCH6/exefs" "$MENU6" "$HBLOADER6" "$(dirname "$NRO6")"
printf 'old qlaunch executable\n' > "$QLAUNCH6/exefs/main"
printf 'old qlaunch npdm\n' > "$QLAUNCH6/exefs/main.npdm"
printf 'old menu executable\n' > "$MENU6/main"
printf 'old menu npdm\n' > "$MENU6/main.npdm"
printf 'old hbloader executable\n' > "$HBLOADER6/main"
printf 'old hbloader npdm\n' > "$HBLOADER6/main.npdm"
printf 'old DeskNX NRO\n' > "$NRO6"
DESKNX_PACKAGE_DIR="$PACKAGE" bash "$ROOT/scripts/install-system.sh" --sd-root "$SD6" --acknowledge-risk --backup-existing
MV_FAIL_BIN_UNINSTALL="$TMP/fail-mv-uninstall"
make_one_shot_mv_failure_wrapper "$MV_FAIL_BIN_UNINSTALL"
if DESKNX_TEST_REAL_MV="$(command -v mv)" \
   DESKNX_TEST_MV_FAIL_DEST="$QLAUNCH6" \
   DESKNX_TEST_MV_FAIL_SOURCE_NEEDLE="/.uninstall-stage-" \
   DESKNX_TEST_MV_FAIL_MARKER="$TMP/uninstall-mv-failed" \
   PATH="$MV_FAIL_BIN_UNINSTALL:$PATH" \
   bash "$ROOT/scripts/uninstall-system.sh" --sd-root "$SD6" --acknowledge-risk; then
  fail "uninstaller succeeded despite injected qlaunch restore failure"
fi
[[ -f "$TMP/uninstall-mv-failed" ]] || fail "uninstaller failure injection did not trigger"
assert_file_content "$QLAUNCH6/exefs/main" "new qlaunch executable"
assert_file_content "$QLAUNCH6/exefs/main.npdm" "new qlaunch npdm"
assert_file_content "$QLAUNCH6/exefs/DeskNX.install-marker" "DeskNX qlaunch override v0.2.0"
assert_file_content "$MENU6/main" "new menu executable"
assert_file_content "$HBLOADER6/main" "new hbloader executable"
assert_file_content "$NRO6" "new DeskNX NRO"
[[ -s "$SD6/switch/DeskNX/install-manifest.txt" ]] || fail "manifest lost after failed uninstall"
[[ -n "$(find "$SD6/switch/DeskNX/backup" -mindepth 1 -maxdepth 1 -type d -name 'qlaunch-*' -print -quit)" ]] || fail "original qlaunch backup lost after failed uninstall"

echo "PASS: installer and uninstaller recovery scenarios, including symlinks and injected rename failures"
