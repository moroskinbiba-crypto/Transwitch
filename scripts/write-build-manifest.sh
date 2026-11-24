#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <output-manifest-path>" >&2
  exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
output_path="$1"
output_dir="$(dirname -- "$output_path")"
mkdir -p "$output_dir"

tmp_path="${output_path}.tmp.$$"
trap 'rm -f "$tmp_path"' EXIT

clean_value() {
  printf '%s' "${2:-not-set}" | tr '\r\n' '  '
}

emit_value() {
  printf '%s=' "$1"
  clean_value "$1" "${!1-}"
  printf '\n'
}

first_version_line() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    "$command_name" --version 2>&1 | sed -n '1p' | tr '\r\n' '  '
  else
    printf 'not-installed'
  fi
}

git_value() {
  local expression="$1"
  git -C "$repo_root" rev-parse "$expression" 2>/dev/null || printf 'unavailable'
}

{
  printf '%s\n' 'DeskNX build provenance manifest'
  printf '%s\n' 'manifest_schema=1'
  printf 'build_timestamp_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf 'source_commit=%s\n' "${GITHUB_SHA:-$(git_value HEAD)}"
  printf 'source_tree=%s\n' "$(git_value 'HEAD^{tree}')"
  printf 'source_ref=%s\n' "${GITHUB_REF:-not-set}"
  printf 'source_ref_name=%s\n' "${GITHUB_REF_NAME:-not-set}"
  printf 'source_head_ref=%s\n' "${GITHUB_HEAD_REF:-not-set}"
  printf 'source_base_ref=%s\n' "${GITHUB_BASE_REF:-not-set}"
  printf 'workflow=%s\n' "${GITHUB_WORKFLOW:-not-set}"
  printf 'workflow_event=%s\n' "${GITHUB_EVENT_NAME:-not-set}"
  printf 'workflow_run_id=%s\n' "${GITHUB_RUN_ID:-not-set}"
  printf 'workflow_run_attempt=%s\n' "${GITHUB_RUN_ATTEMPT:-not-set}"
  printf 'build_target=%s\n' "${BUILD_TARGET:-not-set}"
  printf 'runner_os=%s\n' "${RUNNER_OS:-not-set}"
  printf 'runner_arch=%s\n' "${RUNNER_ARCH:-not-set}"
  printf 'runner_image_os=%s\n' "${ImageOS:-not-set}"
  printf 'runner_image_version=%s\n' "${ImageVersion:-not-set}"
  printf 'host_uname=%s\n' "$(uname -smr 2>/dev/null || printf 'unavailable')"
  printf 'host_os_release=%s\n' "$(
    if [[ -r /etc/os-release ]]; then
      sed -nE '/^(PRETTY_NAME|VERSION_ID|VERSION_CODENAME)=/p' /etc/os-release | tr '\n' ';'
    else
      printf 'unavailable'
    fi
  )"
  printf 'container_image_ref=%s\n' "${BUILD_CONTAINER_REF:-not-set}"
  printf 'debian_snapshot=%s\n' "${DEBIAN_SNAPSHOT_ID:-not-used}"
  printf 'checkout_action_ref=%s\n' "${CHECKOUT_ACTION_REF:-not-set}"
  printf 'upload_artifact_action_ref=%s\n' "${UPLOAD_ARTIFACT_ACTION_REF:-not-set}"
  printf 'release_action_ref=%s\n' "${RELEASE_ACTION_REF:-not-used}"
  printf 'source_date_epoch=%s\n' "${SOURCE_DATE_EPOCH:-not-set}"
  printf 'libnx_requested_ref=%s\n' "${LIBNX_REF:-${LIBNX_REQUESTED_REF:-not-used}}"
  printf 'libnx_resolved_commit=%s\n' "${LIBNX_RESOLVED_COMMIT:-not-used}"
  printf 'atmosphere_requested_ref=%s\n' "${ATMOSPHERE_REF:-${ATMOSPHERE_REQUESTED_REF:-not-used}}"
  printf 'atmosphere_resolved_commit=%s\n' "${ATMOSPHERE_RESOLVED_COMMIT:-not-used}"
  printf 'third_party_source=%s\n' "${THIRD_PARTY_SOURCE:-not-used}"
  printf 'third_party_requested_ref=%s\n' "${THIRD_PARTY_REF:-not-used}"
  printf 'third_party_resolved_commit=%s\n' "${THIRD_PARTY_RESOLVED_COMMIT:-not-used}"
  printf '\n%s\n' '[tool_versions]'
  printf 'aarch64_none_elf_gcc=%s\n' "$(first_version_line aarch64-none-elf-gcc)"
  printf 'aarch64_none_elf_gpp=%s\n' "$(first_version_line aarch64-none-elf-g++)"
  printf 'host_gcc=%s\n' "$(first_version_line gcc)"
  printf 'host_cxx=%s\n' "$(first_version_line c++)"
  printf 'make=%s\n' "$(first_version_line make)"
  printf 'python=%s\n' "$(first_version_line python3)"
  printf 'cmake=%s\n' "$(first_version_line cmake)"
  printf '\n%s\n' '[devkitpro_packages]'
  if command -v pacman >/dev/null 2>&1; then
    for package in devkitA64 libnx switch-dev switch-portlibs switch-libsodium switch-curl switch-sdl2 switch-sdl2_image switch-sdl2_ttf; do
      if pacman -Q "$package" >/dev/null 2>&1; then
        pacman -Q "$package"
      else
        printf '%s=not-installed\n' "$package"
      fi
    done
  else
    printf '%s\n' 'pacman=not-installed'
  fi
  printf '\n%s\n' '[host_packages]'
  if command -v dpkg-query >/dev/null 2>&1; then
    for package in zlib1g-dev libcurl4-openssl-dev libsodium-dev pkg-config libsdl2-dev; do
      dpkg-query -W -f='${binary:Package}=${Version}\n' "$package" 2>/dev/null || printf '%s=not-installed\n' "$package"
    done | sort
  else
    printf '%s\n' 'dpkg-query=not-installed'
  fi
} > "$tmp_path"

mv -f -- "$tmp_path" "$output_path"
trap - EXIT
