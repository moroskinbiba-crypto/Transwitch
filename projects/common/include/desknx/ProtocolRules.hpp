#pragma once

#include <cstdint>

namespace desknx::ipc::rules {

constexpr std::uint16_t LegacyVersion = 1;
constexpr std::uint16_t CurrentVersion = 2;
constexpr std::uint32_t MaxPayloadSize = 0x3F00;
constexpr std::uint32_t MaxBusySeconds = 120;

constexpr bool IsSupportedVersion(std::uint16_t version) {
    return version == LegacyVersion || version == CurrentVersion;
}

constexpr bool IsValidPayloadSize(std::uint64_t size) {
    return size <= MaxPayloadSize;
}

constexpr bool IsValidBusySeconds(std::uint32_t seconds) {
    return seconds >= 1 && seconds <= MaxBusySeconds;
}

// Zero is reserved for the explicit early-end signal; it is never a valid
// duration for starting or extending a Busy pause.
constexpr bool IsBusyEndSignal(std::uint32_t seconds) {
    return seconds == 0;
}

// Menu -> backend commands. Values are append-only once packages ship:
// existing numbers must never change (IPC-04).
enum class CommandId : std::uint32_t {
    MenuReady = 1,
    LaunchTitle = 2,
    ReturnToGame = 3,
    Heartbeat = 4,
    LaunchHomebrew = 5,
    Busy = 6,
    CloseApp = 7,
    SelectUser = 8,
    PowerAction = 9,
    LaunchSystemApplet = 10,
    Log = 11,
};

constexpr bool IsKnownCommand(std::uint32_t command) {
    switch (command) {
        case static_cast<std::uint32_t>(CommandId::MenuReady):
        case static_cast<std::uint32_t>(CommandId::LaunchTitle):
        case static_cast<std::uint32_t>(CommandId::ReturnToGame):
        case static_cast<std::uint32_t>(CommandId::Heartbeat):
        case static_cast<std::uint32_t>(CommandId::LaunchHomebrew):
        case static_cast<std::uint32_t>(CommandId::Busy):
        case static_cast<std::uint32_t>(CommandId::CloseApp):
        case static_cast<std::uint32_t>(CommandId::SelectUser):
        case static_cast<std::uint32_t>(CommandId::PowerAction):
        case static_cast<std::uint32_t>(CommandId::LaunchSystemApplet):
        case static_cast<std::uint32_t>(CommandId::Log):
            return true;
        default:
            return false;
    }
}

constexpr std::uint32_t MaxLogLineBytes = 200;

// Payload size each known command expects; 0xFFFFFFFF marks "any valid size"
// for commands without a payload struct here (checked by the parser itself).
constexpr std::uint32_t ExpectedPayloadSize(std::uint32_t command) {
    switch (command) {
        case static_cast<std::uint32_t>(CommandId::MenuReady):
        case static_cast<std::uint32_t>(CommandId::ReturnToGame):
        case static_cast<std::uint32_t>(CommandId::Heartbeat):
        case static_cast<std::uint32_t>(CommandId::CloseApp):
            return 0;
        case static_cast<std::uint32_t>(CommandId::LaunchTitle):
            // v2: title_id(8) + flags(8) + AccountUid(16) = 32 bytes.
            // Legacy v1 senders used a bare 8-byte title_id. The parser uses
            // MatchesLaunchTitleSize() to accept exactly those two formats.
            return 32;
        case static_cast<std::uint32_t>(CommandId::Busy):
            return 4;   // seconds
        case static_cast<std::uint32_t>(CommandId::SelectUser):
            return 16;  // AccountUid
        case static_cast<std::uint32_t>(CommandId::PowerAction):
            return 4;   // action id
        case static_cast<std::uint32_t>(CommandId::LaunchSystemApplet):
            return 4;   // AppletId
        case static_cast<std::uint32_t>(CommandId::Log):
            return 0xFFFFFFFFu; // 1..MaxLogLineBytes, NUL-terminated
        case static_cast<std::uint32_t>(CommandId::LaunchHomebrew):
            return 1024;
        default:
            return 0xFFFFFFFFu;
    }
}

// LaunchTitle accepts the v2 32-byte payload and the legacy v1 8-byte one.
constexpr bool MatchesLaunchTitleSize(std::uint32_t size) {
    // v1 legacy payload is title_id only (8); v2 adds flags + 16-byte uid (32).
    return size == 32 || size == 8;
}

// Backend -> menu events delivered as additional PushIn storages after the
// initial state block. Also append-only (IPC-04).
enum class EventId : std::uint32_t {
    AppExited = 32,
    AppLaunchFailed = 33,
    SdRemoved = 34,
    LowBattery = 35,
};

constexpr bool IsKnownEvent(std::uint32_t event) {
    switch (event) {
        case static_cast<std::uint32_t>(EventId::AppExited):
        case static_cast<std::uint32_t>(EventId::AppLaunchFailed):
        case static_cast<std::uint32_t>(EventId::SdRemoved):
        case static_cast<std::uint32_t>(EventId::LowBattery):
            return true;
        default:
            return false;
    }
}

// PowerAction payload values (ТЗ §4.3 cmd 9). Keep in sync with
// desknx::ipc::PowerKind in Protocol.hpp; validated here so the host-side
// parser tests do not need switch.h.
constexpr bool IsValidPowerAction(std::uint32_t action) {
    return action <= 2; // Sleep, Reboot, Shutdown
}

// LaunchSystemApplet whitelist (SYS-01): only settings-class applets a user
// can legitimately reach from HOME. Anything else is dropped by the backend.
constexpr bool IsAllowedSystemApplet(std::uint32_t applet_id) {
    switch (applet_id) {
        case 0x0A: return true; // LibraryAppletWeb
        case 0x0B: return true; // LibraryAppletShop
        case 0x0D: return true; // LibraryAppletPhotoViewer
        case 0x0E: return true; // LibraryAppletSet
        case 0x0F: return true; // LibraryAppletOfflineWeb
        case 0x10: return true; // LibraryAppletLoginShare (user mgmt entry)
        case 0x11: return true; // LibraryAppletWifiWebAuth
        case 0x13: return true; // LibraryAppletMyPage
        default:   return false;
    }
}

constexpr std::uint32_t ExpectedEventPayloadSize(std::uint32_t event) {
    switch (event) {
        case static_cast<std::uint32_t>(EventId::AppExited):       // title_id + reason
        case static_cast<std::uint32_t>(EventId::AppLaunchFailed): // title_id + rc
            return 12;
        case static_cast<std::uint32_t>(EventId::SdRemoved):
        case static_cast<std::uint32_t>(EventId::LowBattery):
            return 0;
        default:
            return 0xFFFFFFFFu;
    }
}

// Shared payload contract used by the production sysmodule and host tests.
// Unknown commands are handled separately so callers can log/ignore them.
constexpr bool IsValidCommandPayloadSize(std::uint16_t version,
                                         std::uint32_t command,
                                         std::uint64_t size) {
    if (!IsSupportedVersion(version) || !IsKnownCommand(command) ||
        !IsValidPayloadSize(size))
        return false;

    if (version < CurrentVersion) {
        switch (static_cast<CommandId>(command)) {
            case CommandId::Busy:
            case CommandId::CloseApp:
            case CommandId::SelectUser:
            case CommandId::PowerAction:
            case CommandId::LaunchSystemApplet:
            case CommandId::Log:
                return false;
            default:
                break;
        }
    }

    if (command == static_cast<std::uint32_t>(CommandId::LaunchTitle)) {
        return version == LegacyVersion ? size == 8 : MatchesLaunchTitleSize(
            static_cast<std::uint32_t>(size));
    }
    if (command == static_cast<std::uint32_t>(CommandId::Log))
        return size >= 1 && size <= MaxLogLineBytes;

    const std::uint32_t expected = ExpectedPayloadSize(command);
    return expected != 0xFFFFFFFFu && size == expected;
}

} // namespace desknx::ipc::rules
