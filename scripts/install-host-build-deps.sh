#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <apt-package> [apt-package ...]" >&2
  exit 2
fi

snapshot="${DEBIAN_SNAPSHOT_ID:-20261123T180000Z}"
if [[ ! "$snapshot" =~ ^[0-9]{8}T[0-9]{6}Z$ ]]; then
  echo "Invalid DEBIAN_SNAPSHOT_ID: $snapshot" >&2
  exit 2
fi

test -s /etc/ssl/certs/ca-certificates.crt || {
  echo "CA certificates are required for the immutable Debian snapshot" >&2
  exit 1
}

# Replace mutable base-image mirrors in this ephemeral CI container with a
# single timestamped Debian snapshot. Keep the image's devkitPro pacman repos
# untouched; they are not APT sources.
rm -f /etc/apt/sources.list
if [[ -d /etc/apt/sources.list.d ]]; then
  rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
fi

cat > /etc/apt/sources.list <<EOF
deb [check-valid-until=no] https://snapshot.debian.org/archive/debian/$snapshot/ bookworm main
deb [check-valid-until=no] https://snapshot.debian.org/archive/debian/$snapshot/ bookworm-updates main
deb [check-valid-until=no] https://snapshot.debian.org/archive/debian-security/$snapshot/ bookworm-security main
EOF

apt-get -o Acquire::Check-Valid-Until=false update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"
