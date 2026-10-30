# NPDM capability review — DeskNX

Дата среза: 2026-10-30. Это статическая инвентаризация деклараций исходных JSON-файлов, передаваемых npdmtool. Она не декодирует сгенерированные NPDM-бинарники.

## Найдено

| Процесс | Program ID | FS permissions | service_access / service_host | JSON syscall-map entries | debug flags |
|---|---|---|---|---:|---|
| qlaunch backend | 0100000000001000 | FFFFFFFFFFFFFFFF | * / * | 118 | allow=false, force=false |
| HOME menu applet | 0100000000001042 | FFFFFFFFFFFFFFFF | * / * | 192 | allow=false, force_debug_prod=false, force_debug=true |
| hbloader | 010000000000100D | FFFFFFFFFFFFFFFF | * / * | 192 | allow=false, force_debug_prod=false, force_debug=true |

Menu and hbloader also declare content owner ID 0100000000001000. The backend JSON does not declare an explicit content owner list.

The qlaunch backend JSON includes named SVC declarations for physical-memory mapping, unsafe physical-memory mapping, device-address-space operations, debug/process inspection, port management, process creation/start/termination and secure-monitor access. Menu and hbloader maps include these IDs as well, plus many svcUnassigned placeholder keys. The map sizes above count JSON entries, not confirmed usable operations.

All three configs declare wildcard service access and service hosting. The menu and hbloader also set force_debug=true while the qlaunch backend sets it false. The exact meaning/effect of debug flags is dependent on NPDM encoding and target firmware; this finding must be checked in the generated NPDM rather than interpreted from the JSON alone.

## Why permissions were not changed automatically

These are broad source declarations. The same general pattern appears in public sLaunch source, but provenance is not a least-privilege justification. Narrowing service lists or SVC capabilities without tracing actual calls can break boot, qlaunch takeover, game launch, applet launch or hbloader. Effective filesystem access is represented through ACI/ACID structures; a source JSON inventory does not establish the final binary permission set.

## Required review before system install

1. Decode generated main.npdm for the three components with a parser compatible with the selected Atmosphere/libnx. Record ACI/ACID FS masks, content/save owner IDs, service access control, kernel capabilities and debug flags.
2. Trace actual SVC and service use in each process, including linked libnx/libstratosphere dependencies. Derive a separate allowlist for each process.
3. Validate any reduced profile against the target firmware on a recoverable console, preserving stock qlaunch and a tested offline recovery path.

## References

- Switchbrew NPDM: https://switchbrew.org/wiki/NPDM
- Switchbrew SVC: https://switchbrew.org/wiki/SVC
- Pinned sLaunch source comparison:
  - https://github.com/etonedemid/slaunch/blob/15dc44155e45534e58248d6c2e5b7e2ce76f683c/projects/sSystem/sSystem.json
  - https://github.com/etonedemid/slaunch/blob/15dc44155e45534e58248d6c2e5b7e2ce76f683c/projects/sMenu/sMenu.json
  - https://github.com/etonedemid/slaunch/blob/15dc44155e45534e58248d6c2e5b7e2ce76f683c/projects/hbloader/hbl.json

The upstream comparison establishes source similarity only, not suitability or minimality.

## Automated inventory

Run:

    python3 scripts/audit_npdm_capabilities.py --output /tmp/desknx-npdm-review.md

The checker validates the three JSON inputs and expected process IDs, records SHA-256 digests, and writes a Markdown inventory. It deliberately does not modify capability declarations.
