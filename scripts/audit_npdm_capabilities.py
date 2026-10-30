#!/usr/bin/env python3
"""Inventory the source NPDM declarations passed to npdmtool.

This is a read-only review aid. It does not decode generated NPDM binaries,
compute the effective ACI/ACID permission intersection, or change permissions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

CONFIGS = (
    ("qlaunch backend", "projects/sSystem/DeskNXSystem.json", "0x0100000000001000"),
    ("HOME menu applet", "projects/DeskNXMenu/DeskNXMenu.json", "0x0100000000001042"),
    ("hbloader", "projects/hbloader/hbl.json", "0x010000000000100D"),
)

FS_FLAGS = {
    0: "ApplicationInfo", 1: "BootModeControl", 2: "Calibration",
    3: "SystemSaveData", 4: "GameCard", 5: "SaveDataBackUp",
    6: "SaveDataManagement", 7: "BisAllRaw", 8: "GameCardRaw",
    9: "GameCardPrivate", 10: "SetTime", 11: "ContentManager",
    12: "ImageManager", 13: "CreateSaveData", 14: "SystemSaveDataManagement",
    15: "BisFileSystem", 16: "SystemUpdate", 17: "SaveDataMeta",
    18: "DeviceSaveData", 19: "SettingsControl", 20: "SystemData",
    21: "SdCard", 22: "Host", 23: "FillBis", 24: "CorruptSaveData",
    25: "SaveDataForDebug", 26: "FormatSdCard", 27: "GetRightsId",
    28: "RegisterExternalKey", 29: "RegisterUpdatePartition",
    30: "SaveDataTransfer", 31: "DeviceDetection",
    32: "AccessFailureResolution", 33: "SaveDataTransferVersion2",
    34: "RegisterProgramIndexMapInfo", 35: "CreateOwnSaveData",
    36: "MoveCacheStorage", 37: "DeviceTreeBlob",
    38: "NotifyErrorContextServiceReady", 39: "CalibrationSystemData",
    40: "CalibrationLog", 41: "StorageSecure", 42: "StorageControl",
    43: "GameCardReport", 44: "MarkBeforeEraseBis", 45: "HtmlViewer",
    46: "ApplicationSaveDataBackUp", 47: "StreamPlaySaveData",
    48: "TemporaryDirectory", 62: "Debug (DebugMode-sensitive)",
    63: "FullPermission (special flag)",
}

HIGH_RISK_SVCS = {
    0x2C: "MapPhysicalMemory", 0x2D: "UnmapPhysicalMemory",
    0x48: "MapPhysicalMemoryUnsafe", 0x49: "UnmapPhysicalMemoryUnsafe",
    0x4A: "SetUnsafeLimit", 0x4E: "ReadWriteRegister",
    0x59: "MapDeviceAddressSpaceByForce",
    0x60: "DebugActiveProcess", 0x61: "BreakDebugProcess",
    0x62: "TerminateDebugProcess", 0x63: "GetDebugEvent",
    0x64: "ContinueDebugEvent", 0x69: "QueryDebugProcessMemory",
    0x6A: "ReadDebugProcessMemory", 0x6B: "WriteDebugProcessMemory",
    0x6C: "SetHardwareBreakPoint", 0x70: "CreatePort",
    0x71: "ManageNamedPort", 0x79: "CreateProcess",
    0x7A: "StartProcess", 0x7B: "TerminateProcess",
    0x7F: "CallSecureMonitor",
}

def parse_int(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field}: boolean is not an integer")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{field}: invalid integer literal {value!r}") from exc
    raise ValueError(f"{field}: expected integer or hexadecimal string")

def load_config(root: Path, title: str, relative_path: str, expected_id: str) -> dict[str, Any]:
    path = root / relative_path
    try:
        raw = path.read_bytes()
        data = json.loads(raw.decode("utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"{relative_path}: file not found") from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{relative_path}: invalid UTF-8 JSON: {exc}") from exc

    program_id = data.get("program_id", data.get("title_id"))
    if not isinstance(program_id, str) or program_id.lower() != expected_id.lower():
        raise ValueError(f"{relative_path}: expected ID {expected_id}, got {program_id!r}")

    fs = data.get("filesystem_access")
    if not isinstance(fs, dict) or "permissions" not in fs:
        raise ValueError(f"{relative_path}: filesystem_access.permissions missing")
    fs_mask = parse_int(fs["permissions"], f"{relative_path}: filesystem_access.permissions")
    if fs_mask < 0 or fs_mask >= (1 << 64):
        raise ValueError(f"{relative_path}: FS mask outside uint64")

    access = data.get("service_access", [])
    host = data.get("service_host", [])
    if not isinstance(access, list) or not all(isinstance(x, str) for x in access):
        raise ValueError(f"{relative_path}: service_access must be a string array")
    if not isinstance(host, list) or not all(isinstance(x, str) for x in host):
        raise ValueError(f"{relative_path}: service_host must be a string array")

    capabilities = data.get("kernel_capabilities", [])
    syscall_maps = [x.get("value") for x in capabilities
                    if isinstance(x, dict) and x.get("type") == "syscalls"]
    if len(syscall_maps) > 1:
        raise ValueError(f"{relative_path}: multiple syscall maps")
    syscalls = syscall_maps[0] if syscall_maps else {}
    if not isinstance(syscalls, dict):
        raise ValueError(f"{relative_path}: syscall map must be an object")

    svc_ids: dict[int, str] = {}
    for name, value in syscalls.items():
        svc_id = parse_int(value, f"{relative_path}: {name}")
        if svc_id < 0 or svc_id > 0xBF:
            raise ValueError(f"{relative_path}: SVC ID out of expected range: {name}={value}")
        if svc_id in svc_ids:
            raise ValueError(f"{relative_path}: duplicate SVC ID 0x{svc_id:02X}")
        svc_ids[svc_id] = str(name)

    debug_maps = [x.get("value") for x in capabilities
                  if isinstance(x, dict) and x.get("type") == "debug_flags"]
    debug = debug_maps[0] if debug_maps else {}
    if not isinstance(debug, dict):
        raise ValueError(f"{relative_path}: debug_flags must be an object")

    return {
        "title": title, "path": relative_path,
        "sha256": hashlib.sha256(raw).hexdigest(),
        "program_id": program_id, "filesystem_mask": fs_mask,
        "content_owner_ids": fs.get("content_owner_ids", []),
        "save_data_owner_ids": fs.get("save_data_owner_ids", []),
        "service_access": access, "service_host": host,
        "syscalls": svc_ids, "debug_flags": debug,
    }

def render_report(configs: list[dict[str, Any]]) -> str:
    tick = chr(96)
    lines = [
        "# DeskNX NPDM source capability inventory", "",
        "Generated from JSON inputs passed to npdmtool. This is an inventory, not a proof of least privilege and not a decoder of generated .npdm binaries.", "",
        "## Summary", "",
        "| Component | Program/title ID | FS mask | service_access | service_host | syscall-map entries |",
        "|---|---|---:|---|---|---:|",
    ]
    for c in configs:
        lines.append(
            f"| {c['title']} | {tick}{c['program_id']}{tick} | "
            f"{tick}0x{c['filesystem_mask']:016X}{tick} | "
            f"{tick}{', '.join(c['service_access']) or '(none)'}{tick} | "
            f"{tick}{', '.join(c['service_host']) or '(none)'}{tick} | {len(c['syscalls'])} |"
        )

    lines += [
        "", "## Filesystem access mask", "",
        "All three source configurations currently specify 0xFFFFFFFFFFFFFFFF. That sets every defined FS access bit, reserved bits 49–61, and special bits 62–63 in the JSON input. Special-bit behavior is firmware/debug-mode-sensitive; this inventory does not claim that every bit becomes an effective runtime permission.", "",
        "| Bit | Name | Set in source mask |", "|---:|---|:---:|",
    ]
    for bit, name in FS_FLAGS.items():
        if all(c["filesystem_mask"] & (1 << bit) for c in configs):
            lines.append(f"| {bit} | {name} | yes |")
    lines.append("| 49–61 | Reserved in the published NPDM FS flag table | yes |")

    lines += [
        "", "## Services", "",
        "All three configs use wildcard * for both service_access and service_host. This is a broad declaration, not a reviewed minimal allowlist. The final interpretation must be checked against generated NPDM and the target firmware.", "",
        "| Component | service_access | service_host |", "|---|---|---|",
    ]
    for c in configs:
        lines.append(f"| {c['title']} | {', '.join(c['service_access']) or '(none)'} | {', '.join(c['service_host']) or '(none)'} |")

    lines += [
        "", "## Syscall capability declarations", "",
        "The count is the number of named JSON map entries. It is not a count of exercised SVC calls. Placeholder keys beginning svcUnassigned are reported separately and are not treated as documented callable operations.", "",
        "| Component | JSON map entries | Named high-impact SVC IDs present |", "|---|---:|---|",
    ]
    for c in configs:
        present = [
            f"{tick}0x{svc_id:02X} {HIGH_RISK_SVCS[svc_id]}{tick}"
            for svc_id in sorted(HIGH_RISK_SVCS)
            if svc_id in c["syscalls"] and not c["syscalls"][svc_id].lower().startswith("svcunassigned")
        ]
        lines.append(f"| {c['title']} | {len(c['syscalls'])} | {', '.join(present) or '(none)'} |")

    lines += [
        "", "## Debug flags", "",
        "| Component | allow_debug | force_debug_prod | force_debug |",
        "|---|---:|---:|---:|",
    ]
    for c in configs:
        d = c["debug_flags"]
        lines.append(
            f"| {c['title']} | {d.get('allow_debug', '(unset)')} | "
            f"{d.get('force_debug_prod', '(unset)')} | {d.get('force_debug', '(unset)')} |"
        )

    lines += ["", "## Source file digests", "", "| File | SHA-256 |", "|---|---|"]
    for c in configs:
        lines.append(f"| {tick}{c['path']}{tick} | {tick}{c['sha256']}{tick} |")

    lines += [
        "", "## Review conclusions", "",
        "- Source declarations are broad: all three use a full-width FS mask and wildcard service access/host entries.",
        "- HOME menu and hbloader set force_debug=true, while qlaunch backend sets it false. Interpret the effect only with the generated NPDM encoding and exact target firmware.",
        "- The menu and hbloader have larger syscall maps than qlaunch. Many extra IDs are svcUnassigned placeholders; map-entry count alone must not be read as the number of real callable operations.",
        "- No capabilities are automatically removed. Blindly narrowing them can break boot, HOME takeover, game launch, or hbloader.",
        "- Effective filesystem permissions depend on ACI/ACID structures. This source inventory cannot establish the final binary permission intersection.",
        "", "## Required next checks", "",
        "1. Decode the generated main.npdm for all three components with an Atmosphere-compatible parser. Record ACI/ACID FS masks, owner IDs, SAC, KAC and debug capabilities.",
        "2. Trace actual SVC and service use in each component, including linked libnx/libstratosphere code, then propose per-process allowlists.",
        "3. Verify any reduced profiles against the exact Atmosphere/firmware target on a recoverable console.",
        "", "## References", "",
        "- Switchbrew NPDM: https://switchbrew.org/wiki/NPDM",
        "- Switchbrew SVC: https://switchbrew.org/wiki/SVC",
        "- Pinned sLaunch source comparison: https://github.com/etonedemid/slaunch/tree/15dc44155e45534e58248d6c2e5b7e2ce76f683c (source similarity is not proof of least privilege).",
        "", "Generated by scripts/audit_npdm_capabilities.py.", "",
    ]
    return "\n".join(lines)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, help="write Markdown report; stdout when omitted")
    args = parser.parse_args()
    try:
        configs = [load_config(args.root, *entry) for entry in CONFIGS]
        report = render_report(configs)
    except (OSError, ValueError) as exc:
        print(f"NPDM audit failed: {exc}", file=sys.stderr)
        return 1
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
