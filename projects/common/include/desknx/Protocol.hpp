#pragma once

#include <switch.h>
#include <cstddef>
#include <cstdint>
#include <desknx/ProtocolRules.hpp>

namespace desknx::ipc {

// Versioned, size-checked messages between the DeskNX library applet and the
// qlaunch replacement. Keep command values append-only once packages ship.
constexpr u32 Magic = 0x584E4B44; // "DKNX"
constexpr u16 LegacyVersion = 1;
constexpr u16 Version = 2;

enum class Command : u32 {
    MenuReady = 1,
    LaunchTitle = 2,
    ReturnToGame = 3,
    Heartbeat = 4,
    LaunchHomebrew = 5,
    Busy = 6,
};

struct Header {
    u32 magic;
    u16 version;
    u16 header_size;
    u32 command;
    u32 payload_size;
};
static_assert(sizeof(Header) == 16);

struct LaunchTitlePayload {
    u64 title_id;
};
static_assert(sizeof(LaunchTitlePayload) == 8);

constexpr size_t MaxHomebrewPath = 512;
constexpr size_t MaxHomebrewArgv = 512;
struct LaunchHomebrewPayload {
    char nro_path[MaxHomebrewPath];
    char argv[MaxHomebrewArgv];
};
static_assert(sizeof(LaunchHomebrewPayload) == 1024);

inline Result Send(Command command, const void* payload = nullptr, size_t payload_size = 0) {
    if (payload_size > 0x3F00 || (payload_size != 0 && payload == nullptr))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    AppletStorage storage{};
    const size_t total_size = sizeof(Header) + payload_size;
    Result rc = appletCreateStorage(&storage, total_size);
    if (R_FAILED(rc)) return rc;

    const Header header{
        Magic,
        Version,
        static_cast<u16>(sizeof(Header)),
        static_cast<u32>(command),
        static_cast<u32>(payload_size)
    };
    rc = appletStorageWrite(&storage, 0, &header, sizeof(header));
    if (R_SUCCEEDED(rc) && payload_size != 0)
        rc = appletStorageWrite(&storage, sizeof(header), payload, payload_size);

    if (R_SUCCEEDED(rc))
        rc = appletPushOutData(&storage);

    appletStorageClose(&storage);
    return rc;
}

struct BusyPayload {
    u32 seconds;
};
static_assert(sizeof(BusyPayload) == 4);

inline Result SendBusy(u32 seconds) {
    if (!rules::IsValidBusySeconds(seconds))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const BusyPayload payload{seconds};
    return Send(Command::Busy, &payload, sizeof(payload));
}

inline Result SendBusyEnd() {
    // A zero-second Busy message releases an active pause early. The backend
    // rejects this signal when no pause is active.
    const BusyPayload payload{0};
    return Send(Command::Busy, &payload, sizeof(payload));
}

inline Result SendMenuReady() {
    return Send(Command::MenuReady);
}

inline Result SendHeartbeat() {
    return Send(Command::Heartbeat);
}

inline Result SendLaunchTitle(u64 title_id) {
    const LaunchTitlePayload payload{title_id};
    return Send(Command::LaunchTitle, &payload, sizeof(payload));
}

inline Result SendLaunchHomebrew(const char* nro_path, const char* argv = nullptr) {
    if (!nro_path || !nro_path[0] || strlen(nro_path) >= MaxHomebrewPath)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (argv && strlen(argv) >= MaxHomebrewArgv)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    LaunchHomebrewPayload payload{};
    strncpy(payload.nro_path, nro_path, sizeof(payload.nro_path) - 1);
    if (argv)
        strncpy(payload.argv, argv, sizeof(payload.argv) - 1);
    return Send(Command::LaunchHomebrew, &payload, sizeof(payload));
}

} // namespace desknx::ipc
