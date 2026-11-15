#!/usr/bin/env python3
"""Validate NRO0 + ASET + NACP and reject an embedded RomFS in the native build."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

NRO_MAGIC_OFFSET = 0x10
NRO_SIZE_OFFSET = 0x18
ASET_HEADER_SIZE = 0x38
NACP_FIELD_OFFSET = 0x18
ROMFS_FIELD_OFFSET = 0x28


def fail(message: str) -> None:
    print(f"[NRO asset check] ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_section(
    data: bytes, base: int, field: int, label: str, *, required: bool = True
) -> tuple[int, int]:
    offset, size = struct.unpack_from("<QQ", data, base + field)
    if size == 0:
        if required:
            fail(f"{label} section is empty")
        return offset, size
    if offset < ASET_HEADER_SIZE:
        fail(f"{label} offset points inside ASET header: 0x{offset:X}")
    if offset > len(data) - base or size > len(data) - base - offset:
        fail(f"{label} section exceeds the NRO file")
    return offset, size


def main() -> None:
    if len(sys.argv) != 2:
        fail(f"usage: {Path(sys.argv[0]).name} application.nro")
    path = Path(sys.argv[1])
    try:
        data = path.read_bytes()
    except OSError as exc:
        fail(f"cannot read {path}: {exc}")
    if len(data) < NRO_SIZE_OFFSET + 4:
        fail("file is too small to contain an NRO header")
    if data[NRO_MAGIC_OFFSET:NRO_MAGIC_OFFSET + 4] != b"NRO0":
        fail("NRO0 magic is missing at offset 0x10")
    base = struct.unpack_from("<I", data, NRO_SIZE_OFFSET)[0]
    if base < 0x80 or base + ASET_HEADER_SIZE > len(data):
        fail(f"no complete ASET section at 0x{base:X} (file size 0x{len(data):X})")
    if data[base:base + 4] != b"ASET":
        fail(f"ASET magic is missing at file offset 0x{base:X}")
    version = struct.unpack_from("<I", data, base + 4)[0]
    if version != 0:
        fail(f"unsupported ASET version: {version}")

    _, nacp_size = read_section(data, base, NACP_FIELD_OFFSET, "NACP")
    _, romfs_size = read_section(data, base, ROMFS_FIELD_OFFSET, "RomFS", required=False)
    if romfs_size != 0:
        fail(f"native standalone NRO must not contain RomFS (found {romfs_size} bytes)")

    print(
        f"[NRO asset check] OK: {path} "
        f"(NRO={len(data)} bytes, NACP={nacp_size} bytes, RomFS=absent)"
    )


if __name__ == "__main__":
    main()
