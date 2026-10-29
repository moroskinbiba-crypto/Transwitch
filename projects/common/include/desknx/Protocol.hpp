#pragma once

#include <switch.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    CloseApp = 7,
    SelectUser = 8,
    PowerAction = 9,
    LaunchSystemApplet = 10,
    Log = 11,
};

// Backend -> menu events (delivered as additional PushIn storages after the
// initial state block). Append-only, like commands.
enum class Event : u32 {
    AppExited = 32,
    AppLaunchFailed = 33,
    SdRemoved = 34,
    LowBattery = 35,
};

enum class PowerKind : u32 {
    Sleep = 0,
    Reboot = 1,
    Shutdown = 2,
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

// Protocol v2 form of LaunchTitle: title_id + flags + selected account uid
// (ТЗ §4.3). The backend also accepts the legacy 8-byte v1 payload.
struct LaunchTitleV2Payload {
    u64 title_id;
    u64 flags;
    AccountUid uid;   // 16 bytes (two u64 halves) on both host and target
};
static_assert(sizeof(LaunchTitleV2Payload) == 32);

// LaunchTitle flag bits (LNC-02): "uid supplied by menu" tells the backend to
// honour the preselected user even when NACP would otherwise decide; "game
// picks players itself" suppresses the preselection parameter entirely.
constexpr u64 LaunchFlagHasUser   = 1u << 0;
constexpr u64 LaunchFlagNoPreuser = 1u << 1;

constexpr size_t MaxHomebrewPath = 512;
constexpr size_t MaxHomebrewArgv = 512;
struct LaunchHomebrewPayload {
    char nro_path[MaxHomebrewPath];
    char argv[MaxHomebrewArgv];
};
static_assert(sizeof(LaunchHomebrewPayload) == 1024);

inline Result Send(Command command, const void* payload = nullptr, size_t payload_size = 0) {
    if (!rules::IsValidPayloadSize(payload_size) ||
        (payload_size != 0 && payload == nullptr))
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

// v2 sender: carries the uid chosen in the menu so the backend launches the
// title as that user (LNC-02/LNC-03). A zero uid means "backend decides".
inline Result SendLaunchTitle(u64 title_id, u64 flags, AccountUid uid) {
    const LaunchTitleV2Payload payload{title_id, flags, uid};
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

// --- protocol v2 additions -------------------------------------------------

struct CloseAppPayload {};

struct SelectUserPayload {
    AccountUid uid;
};
static_assert(sizeof(SelectUserPayload) == 16);

struct PowerActionPayload {
    u32 action; // PowerKind
};
static_assert(sizeof(PowerActionPayload) == 4);

struct LaunchSystemAppletPayload {
    u32 applet_id;
};
static_assert(sizeof(LaunchSystemAppletPayload) == 4);

constexpr u32 MaxLogLineBytes = rules::MaxLogLineBytes;

struct AppExitedEvent {
    u64 title_id;
    u32 reason; // 0 = exited naturally, 1 = terminated by backend
};

struct AppLaunchFailedEvent {
    u64 title_id;
    u32 rc;
};

// Initial state block the backend pushes into every menu launch (IPC-06).
// Fixed size 64 bytes, little-endian. All fields are validated by the menu;
// unknown trailing space stays zero.
constexpr u16 InitialStateSize = 64;
struct InitialState {
    u32 magic;              // Magic
    u16 version;            // protocol version of this block (2)
    u16 header_size;        // == InitialStateSize
    u32 backend_version;    // semantic-ish build id of the sysmodule
    u32 app_state;          // 0 none, 1 starting, 2 foreground, 3 suspended
    u64 app_title_id;
    u32 active_slot;
    u32 degraded;           // REC-04: running without SD-backed assets
    u32 last_launch_rc;     // most recent title-launch failure (0 = none)
    u64 last_launch_title;
    AccountUid selected_user; // 16 bytes
    // Padded so the whole block is exactly InitialStateSize (64):
    // 4+2+2 +4+4+8 +4+4 +4+8 +16 = 64.
};
static_assert(sizeof(InitialState) == InitialStateSize);

inline Result SendCloseApp() {
    return Send(Command::CloseApp);
}

inline Result SendSelectUser(AccountUid uid) {
    const SelectUserPayload payload{uid};
    return Send(Command::SelectUser, &payload, sizeof(payload));
}

inline Result SendPowerAction(PowerKind action) {
    const PowerActionPayload payload{static_cast<u32>(action)};
    return Send(Command::PowerAction, &payload, sizeof(payload));
}

inline Result SendLaunchSystemApplet(AppletId applet_id) {
    const LaunchSystemAppletPayload payload{static_cast<u32>(applet_id)};
    return Send(Command::LaunchSystemApplet, &payload, sizeof(payload));
}

// The line must be NUL-terminated and at most MaxLogLineBytes including the
// terminator (IPC-01/IPC-02). Control characters are rejected so a hostile or
// buggy caller cannot forge log lines.
inline Result SendLog(const char* line) {
    if (!line || !line[0])
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const size_t length = strlen(line) + 1;
    if (length > MaxLogLineBytes)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c < 0x20 && c != '\0')
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    return Send(Command::Log, line, length);
}

// --- backend -> menu events -------------------------------------------------
// The backend is the system applet; it delivers these through
// appletPushOutData, which the library applet reads with appletPopInData.
// Same header framing as menu->backend messages, but carrying an Event id
// (§4.4). Append-only ids keep old menus forward-compatible (IPC-04).
inline Result SendEvent(Event event, const void* payload, size_t payload_size) {
    if (!rules::IsValidPayloadSize(payload_size) ||
        (payload_size != 0 && payload == nullptr))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    AppletStorage storage{};
    const size_t total_size = sizeof(Header) + payload_size;
    Result rc = appletCreateStorage(&storage, total_size);
    if (R_FAILED(rc)) return rc;

    const Header header{
        Magic,
        Version,
        static_cast<u16>(sizeof(Header)),
        static_cast<u32>(event),
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

inline Result NotifyAppExited(u64 title_id, u32 reason) {
    const AppExitedEvent payload{title_id, reason};
    return SendEvent(Event::AppExited, &payload, sizeof(payload));
}

inline Result NotifyAppLaunchFailed(u64 title_id, Result rc) {
    const AppLaunchFailedEvent payload{title_id, static_cast<u32>(rc)};
    return SendEvent(Event::AppLaunchFailed, &payload, sizeof(payload));
}

inline Result NotifySdRemoved() {
    return SendEvent(Event::SdRemoved, nullptr, 0);
}

inline Result NotifyLowBattery() {
    return SendEvent(Event::LowBattery, nullptr, 0);
}

} // namespace desknx::ipc
