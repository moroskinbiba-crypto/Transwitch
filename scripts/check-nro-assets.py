#!/usr/bin/env python3
"""Validate native DeskNX NRO: NACP must be embedded; browser/RomFS must not."""
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
    print(f"[native NRO check] ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    if len(sys.argv) != 2:
        fail(f"usage: {Path(sys.argv[0]).name} DeskNX.nro")

    path = Path(sys.argv[1])
    try:
        data = path.read_bytes()
    except OSError as exc:
        fail(f"cannot read {path}: {exc}")

    if len(data) < NRO_SIZE_OFFSET + 4:
        fail("file is too small to contain an NRO header")
    if data[NRO_MAGIC_OFFSET:NRO_MAGIC_OFFSET + 4] != b"NRO0":
        fail("NRO0 magic is missing at offset 0x10")

    aset = struct.unpack_from("<I", data, NRO_SIZE_OFFSET)[0]
    if aset < 0x80 or aset + ASET_HEADER_SIZE > len(data):
        fail(f"no complete ASET section at 0x{aset:X}")
    if data[aset:aset + 4] != b"ASET":
        fail(f"ASET magic is missing at file offset 0x{aset:X}")
    version = struct.unpack_from("<I", data, aset + 4)[0]
    if version != 0:
        fail(f"unsupported ASET version: {version}")

    nacp_offset, nacp_size = struct.unpack_from("<QQ", data, aset + NACP_FIELD_OFFSET)
    if nacp_size == 0:
        fail("NACP section is empty")
    if nacp_offset < ASET_HEADER_SIZE:
        fail(f"NACP offset points inside ASET header: 0x{nacp_offset:X}")
    if nacp_offset > len(data) - aset or nacp_size > len(data) - aset - nacp_offset:
        fail("NACP section exceeds the NRO file")

    romfs_offset, romfs_size = struct.unpack_from("<QQ", data, aset + ROMFS_FIELD_OFFSET)
    if romfs_size != 0:
        fail(f"native NRO unexpectedly embeds a RomFS section ({romfs_size} bytes)")
    if romfs_offset != 0:
        fail(f"native NRO has an unexpected RomFS offset (0x{romfs_offset:X})")

    forbidden_markers = (
        b"webConfigShow", b"webOfflineCreate", b"WebCommonConfig",
        b"app.htdocs", b"html-document"
    )
    for marker in forbidden_markers:
        if marker in data:
            fail(f"browser/WebKit marker remains in native NRO: {marker.decode('ascii')}")

    print(
        f"[native NRO check] OK: {path} "
        f"(NRO={len(data)} bytes, NACP={nacp_size} bytes, RomFS=none, WebKit=none)"
    )


if __name__ == "__main__":
    main()
