#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C TZ=UTC

if [[ $# -ne 1 || ! -d "$1" ]]; then
  echo "Usage: $0 <package-directory>" >&2
  exit 2
fi

package_dir="$(cd -- "$1" && pwd)"
command -v sha256sum >/dev/null 2>&1 || {
  echo "sha256sum is required" >&2
  exit 1
}

(
  cd "$package_dir"
  find . -type f ! -name SHA256SUMS -print0 |
    sort -z |
    xargs -0 -r sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)
