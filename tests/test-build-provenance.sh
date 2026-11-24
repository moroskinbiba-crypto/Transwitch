#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

GITHUB_SHA=0123456789abcdef0123456789abcdef01234567 \
GITHUB_REF=refs/heads/manifest-test \
GITHUB_REF_NAME=manifest-test \
GITHUB_WORKFLOW=Build\ DeskNX \
GITHUB_EVENT_NAME=push \
GITHUB_RUN_ID=12345 \
GITHUB_RUN_ATTEMPT=2 \
RUNNER_OS=Linux \
RUNNER_ARCH=X64 \
BUILD_TARGET=manifest-test \
BUILD_CONTAINER_REF=example/image@sha256:deadbeef \
DEBIAN_SNAPSHOT_ID=20261123T180000Z \
bash "$repo_root/scripts/write-build-manifest.sh" "$tmp_dir/BUILD-MANIFEST.txt"

manifest="$tmp_dir/BUILD-MANIFEST.txt"
test -s "$manifest"
grep -Fx 'manifest_schema=1' "$manifest"
grep -Fx 'source_commit=0123456789abcdef0123456789abcdef01234567' "$manifest"
grep -Fx 'source_ref=refs/heads/manifest-test' "$manifest"
grep -Fx 'build_target=manifest-test' "$manifest"
grep -Fx 'container_image_ref=example/image@sha256:deadbeef' "$manifest"
grep -Fx 'debian_snapshot=20261123T180000Z' "$manifest"
grep -Fq '[tool_versions]' "$manifest"
grep -Fq '[host_packages]' "$manifest"
! grep -q 'GITHUB_TOKEN' "$manifest"

mkdir -p "$tmp_dir/package/nested"
printf 'payload\n' > "$tmp_dir/package/nested/payload.txt"
printf 'metadata\n' > "$tmp_dir/package/BUILD-MANIFEST.txt"
bash "$repo_root/scripts/write-package-checksums.sh" "$tmp_dir/package"
(
  cd "$tmp_dir/package"
  sha256sum -c SHA256SUMS
)
grep -Fq './BUILD-MANIFEST.txt' "$tmp_dir/package/SHA256SUMS"
grep -Fq './nested/payload.txt' "$tmp_dir/package/SHA256SUMS"

echo 'Build provenance and package checksum tests passed.'
