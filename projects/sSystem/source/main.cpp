// DeskNX system backend.
// The design separates the UI applet from qlaunch lifecycle ownership: only this
// process receives HOME/power lifecycle events and starts installed applications.
#include <switch.h>
#include <stratosphere.hpp>

#include <desknx/Protocol.hpp>
#include <desknx/system/Application.hpp>
#include <desknx/system/ExternalContent.hpp>
#include <desknx/system/LibraryApplet.hpp>

#include <algorithm>
#include <cstdarg>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace {

using desknx::ipc::Command;
using namespace desknx::system;

constexpr const char* kDataDir = "sdmc:/switch/DeskNX";
constexpr const char* kLogDir = "sdmc:/switch/DeskNX/logs";
constexpr const char* kLogPath = "sdmc:/switch/DeskNX/logs/daemon.log";
constexpr const char* kPrevLogPath = "sdmc:/switch/DeskNX/logs/daemon.prev.log";
constexpr const char* kHbTargetPath = "sdmc:/switch/DeskNX/hbtarget.txt";
constexpr const char* kHbTargetTemp = "sdmc:/switch/DeskNX/hbtarget.tmp";
constexpr u64 kHomePressSettleMs = 1500;
constexpr u64 kMenuFastExitMs = 3000;
constexpr u64 kMenuReadyTimeoutSeconds = 8;
constexpr u64 kMenuHeartbeatTimeoutSeconds = 15;
constexpr u64 kMenuRetryDelaySeconds = 5;
constexpr u64 kRecoveryChordHoldSeconds = 3;
constexpr const char* kQlaunchOverride = "sdmc:/atmosphere/contents/0100000000001000";
constexpr const char* kQlaunchDisabled = "sdmc:/atmosphere/contents/0100000000001000.disabled-DeskNX";
constexpr const char* kInstallMarker = "sdmc:/atmosphere/contents/0100000000001000/exefs/DeskNX.install-marker";
constexpr const char* kInstallMarkerText = "DeskNX qlaunch override v0.2.0";
constexpr u64 kMenuStartRetryDelayNs = 500'000'000ULL;
constexpr int kMenuStartAttempts = 4;

AccountUid g_SelectedUser{};
u64 g_PendingTitle = 0;
bool g_PendingReturnToGame = false;
bool g_PendingHomebrew = false;
bool g_HomebrewLoaderRunning = false;
char g_PendingHomebrewPath[desknx::ipc::MaxHomebrewPath]{};
char g_PendingHomebrewArgv[desknx::ipc::MaxHomebrewArgv]{};
bool g_MenuReady = false;
u64 g_MenuLaunchTick = 0;
u64 g_LastMenuHeartbeatTick = 0;
u64 g_NextMenuRetryTick = 0;
desknx::system::ecs::MenuSlot g_ActiveMenuSlots[desknx::system::ecs::MenuSlotCount + 1]{};
size_t g_ActiveMenuSlotCount = 0;
size_t g_MenuSlotIndex = 0;
bool g_MenuSlotExhausted = false;
bool g_ReopenMenuAfterSleep = false;
PadState g_RecoveryPad{};
u64 g_RecoveryChordStartTick = 0;
bool g_RecoveryChordAttempted = false;

void MakeDirectories() {
    mkdir("sdmc:/switch", 0777);
    mkdir(kDataDir, 0777);
    mkdir(kLogDir, 0777);
}

void RotateLogIfNeeded() {
    struct stat st{};
    if (stat(kLogPath, &st) == 0 && st.st_size > 256 * 1024) {
        remove(kPrevLogPath);
        rename(kLogPath, kPrevLogPath);
    }
}

} // namespace

void DaemonLog(const char* fmt, ...) {
    char line[512]{};
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > static_cast<int>(sizeof(line) - 2))
        n = static_cast<int>(sizeof(line) - 2);
    line[n++] = '\n';
    line[n] = '\0';

    MakeDirectories();
    RotateLogIfNeeded();
    if (FILE* fp = std::fopen(kLogPath, "a")) {
        std::fwrite(line, 1, static_cast<size_t>(n), fp);
        std::fclose(fp);
    }
}

namespace {

bool IsSafeHomebrewPath(const char* path) {
    if (!path || !path[0] || std::strlen(path) >= desknx::ipc::MaxHomebrewPath)
        return false;
    if (std::strncmp(path, "sdmc:/", 6) != 0 || std::strstr(path, "..") != nullptr)
        return false;

    const size_t length = std::strlen(path);
    if (length < 4) return false;
    const char* ext = path + length - 4;
    if (ext[0] != '.' ||
        std::tolower(static_cast<unsigned char>(ext[1])) != 'n' ||
        std::tolower(static_cast<unsigned char>(ext[2])) != 'r' ||
        std::tolower(static_cast<unsigned char>(ext[3])) != 'o')
        return false;

    struct stat st{};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool WriteHomebrewTarget(const char* path, const char* argv) {
    if (!IsSafeHomebrewPath(path)) {
        DaemonLog("hb: refused invalid NRO path");
        return false;
    }
    if (argv && std::strlen(argv) >= desknx::ipc::MaxHomebrewArgv) {
        DaemonLog("hb: refused oversized argv");
        return false;
    }

    char request[desknx::ipc::MaxHomebrewPath + desknx::ipc::MaxHomebrewArgv + 4]{};
    const int length = std::snprintf(request, sizeof(request),
                                     (argv && argv[0]) ? "%s\n%s\n" : "%s\n",
                                     path, argv ? argv : "");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(request))
        return false;

    MakeDirectories();
    remove(kHbTargetTemp);
    FILE* fp = std::fopen(kHbTargetTemp, "w");
    if (!fp) {
        DaemonLog("hb: cannot create target request (errno=%d)", errno);
        return false;
    }
    const bool wrote = std::fwrite(request, 1, static_cast<size_t>(length), fp) ==
                       static_cast<size_t>(length);
    const bool flushed = std::fflush(fp) == 0;
    const bool closed = std::fclose(fp) == 0;
    if (!wrote || !flushed || !closed) {
        remove(kHbTargetTemp);
        DaemonLog("hb: failed writing complete target request");
        return false;
    }

    // The loader only sees the request after the final rename. Replacing the
    // previous target is safe because the menu slot is free at this point.
    remove(kHbTargetPath);
    if (std::rename(kHbTargetTemp, kHbTargetPath) != 0) {
        remove(kHbTargetTemp);
        DaemonLog("hb: cannot publish target request (errno=%d)", errno);
        return false;
    }
    return true;
}

bool PickSelectedUser() {
    if (R_SUCCEEDED(accountGetPreselectedUser(&g_SelectedUser)) &&
        accountUidIsValid(&g_SelectedUser)) {
        return true;
    }

    s32 userCount = 0;
    if (R_FAILED(accountGetUserCount(&userCount)) || userCount <= 0)
        return false;

    AccountUid users[8]{};
    s32 actual = 0;
    if (R_FAILED(accountListAllUsers(users, 8, &actual)) || actual <= 0)
        return false;

    const int limit = std::min<int>(actual, 8);
    for (int i = 0; i < limit; ++i) {
        if (accountUidIsValid(&users[i])) {
            g_SelectedUser = users[i];
            return true;
        }
    }
    return false;
}

void InitializeMenuSlots() {
    g_ActiveMenuSlotCount = 0;

    // Optional user override: one known shop/offline-web applet program id.
    // The built-in candidates remain available as fallbacks.
    u64 customProgramId = 0;
    AppletId customAppletId = AppletId_None;
    bool customValid = false;
    FILE* fp = std::fopen(ecs::MenuSlotOverridePath, "r");
    if (fp) {
        char line[64]{};
        const bool hasLine = std::fgets(line, sizeof(line), fp) != nullptr;
        if (hasLine) {
            const bool truncated = std::strchr(line, '\n') == nullptr && !std::feof(fp);
            line[std::strcspn(line, "\r\n")] = '\0';
            char* end = nullptr;
            errno = 0;
            const unsigned long long parsed = std::strtoull(line, &end, 0);
            while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;

            bool extraContent = false;
            char extra[64]{};
            while (std::fgets(extra, sizeof(extra), fp)) {
                for (const char* p = extra; *p; ++p) {
                    if (!std::isspace(static_cast<unsigned char>(*p))) {
                        extraContent = true;
                        break;
                    }
                }
            }

            if (!truncated && !extraContent && errno == 0 && end != line &&
                end && *end == '\0' && parsed != 0) {
                const AppletId mapped = ecs::AppletIdForProgramId(static_cast<u64>(parsed));
                // Only slots we deliberately support for takeover are accepted.
                // In particular, never let this setting steal Album, keyboard,
                // error or other system-critical applets.
                if (mapped == AppletId_LibraryAppletShop ||
                    mapped == AppletId_LibraryAppletOfflineWeb) {
                    customProgramId = static_cast<u64>(parsed);
                    customAppletId = mapped;
                    customValid = true;
                } else {
                    DaemonLog("slot: ignored override; program id 0x%016llX is not an approved HOME slot",
                              parsed);
                }
            } else {
                DaemonLog("slot: ignored malformed takeover override");
            }
        }
        std::fclose(fp);
    }

    if (customValid) {
        g_ActiveMenuSlots[g_ActiveMenuSlotCount++] =
            {customAppletId, "custom", {customProgramId, 0}, 1};
        DaemonLog("slot: using configured override program=0x%016llX",
                  static_cast<unsigned long long>(customProgramId));
    }

    for (size_t i = 0; i < ecs::MenuSlotCount; ++i)
        g_ActiveMenuSlots[g_ActiveMenuSlotCount++] = ecs::MenuSlots[i];

    DaemonLog("slot: configured %zu candidates", g_ActiveMenuSlotCount);
}

const desknx::system::ecs::MenuSlot& CurrentMenuSlot() {
    if (g_ActiveMenuSlotCount == 0)
        return ecs::MenuSlots[0];
    const size_t index = std::min(g_MenuSlotIndex, g_ActiveMenuSlotCount - 1);
    return g_ActiveMenuSlots[index];
}

void RegisterCurrentSlot() {
    const auto& slot = CurrentMenuSlot();
    for (u8 i = 0; i < slot.program_id_count; ++i) {
        const Result rc = ecs::RegisterExternalContent(slot.program_ids[i], ecs::MenuExefsDir);
        DaemonLog("ecs register slot=%s program=0x%016llX rc=0x%08X",
                  slot.name,
                  static_cast<unsigned long long>(slot.program_ids[i]),
                  static_cast<unsigned int>(rc));
    }
}

bool LaunchMenu() {
    if (la::IsMenuAlive()) return true;
    g_MenuSlotExhausted = false;
    g_NextMenuRetryTick = 0;
    const auto& slot = CurrentMenuSlot();
    RegisterCurrentSlot();

    Result rc = 0;
    for (int attempt = 0; attempt < kMenuStartAttempts; ++attempt) {
        if (attempt != 0) svcSleepThread(kMenuStartRetryDelayNs);
        rc = la::LaunchMenu(slot.applet_id, nullptr, 0);
        DaemonLog("menu start slot=%s attempt=%d rc=0x%08X",
                  slot.name, attempt + 1, static_cast<unsigned int>(rc));
        if (R_SUCCEEDED(rc)) {
            g_MenuReady = false;
            g_MenuLaunchTick = armGetSystemTick();
            g_LastMenuHeartbeatTick = 0;
            g_MenuSlotExhausted = false;
            return true;
        }
    }

    if (g_MenuSlotIndex + 1 < g_ActiveMenuSlotCount) {
        ++g_MenuSlotIndex;
        DaemonLog("menu start failed; falling back to slot=%s",
                  CurrentMenuSlot().name);
        RegisterCurrentSlot();
        for (int attempt = 0; attempt < kMenuStartAttempts; ++attempt) {
            if (attempt != 0) svcSleepThread(kMenuStartRetryDelayNs);
            rc = la::LaunchMenu(CurrentMenuSlot().applet_id, nullptr, 0);
            DaemonLog("menu fallback start slot=%s attempt=%d rc=0x%08X",
                      CurrentMenuSlot().name, attempt + 1, static_cast<unsigned int>(rc));
            if (R_SUCCEEDED(rc)) {
                g_MenuReady = false;
                g_MenuLaunchTick = armGetSystemTick();
                g_LastMenuHeartbeatTick = 0;
                g_MenuSlotExhausted = false;
                return true;
            }
        }
    }

    g_MenuSlotExhausted = true;
    g_NextMenuRetryTick = armGetSystemTick() + armGetSystemTickFreq() * kMenuRetryDelaySeconds;
    DaemonLog("menu: all supported HOME applet slots failed; retry scheduled in %llu seconds",
              static_cast<unsigned long long>(kMenuRetryDelaySeconds));
    // If a title was suspended when HOME was pressed, restore its focus when
    // no known applet slot can provide the menu. A failed menu must not strand
    // the player on a black screen.
    if (app::g_AppRunning) {
        const Result focusRc = app::FocusApplication();
        DaemonLog("menu: restored title focus after startup failure rc=0x%08X",
                  static_cast<unsigned int>(focusRc));
    }
    return false;
}

bool ParseMenuCommand(AppletStorage& storage) {
    desknx::ipc::Header hdr{};
    if (R_FAILED(appletStorageRead(&storage, 0, &hdr, sizeof(hdr))))
        return false;

    if (hdr.magic != desknx::ipc::Magic ||
        hdr.version != desknx::ipc::Version ||
        hdr.header_size != sizeof(desknx::ipc::Header) ||
        hdr.payload_size > 0x3F00) {
        DaemonLog("ipc: dropped malformed message header");
        return false;
    }

    const auto command = static_cast<Command>(hdr.command);
    switch (command) {
        case Command::MenuReady:
            if (hdr.payload_size != 0) return false;
            g_MenuReady = true;
            g_LastMenuHeartbeatTick = armGetSystemTick();
            DaemonLog("ipc: DeskNX menu ready");
            return true;

        case Command::Heartbeat:
            if (hdr.payload_size != 0) return false;
            if (g_MenuReady)
                g_LastMenuHeartbeatTick = armGetSystemTick();
            return true;

        case Command::LaunchTitle: {
            if (hdr.payload_size != sizeof(desknx::ipc::LaunchTitlePayload))
                return false;
            desknx::ipc::LaunchTitlePayload payload{};
            if (R_FAILED(appletStorageRead(&storage, sizeof(hdr), &payload, sizeof(payload))))
                return false;
            if (payload.title_id == 0) return false;
            g_PendingTitle = payload.title_id;
            g_PendingReturnToGame = false;
            g_PendingHomebrew = false;
            DaemonLog("ipc: launch title=0x%016llX",
                      static_cast<unsigned long long>(payload.title_id));
            return true;
        }

        case Command::LaunchHomebrew: {
            if (hdr.payload_size != sizeof(desknx::ipc::LaunchHomebrewPayload))
                return false;
            desknx::ipc::LaunchHomebrewPayload payload{};
            if (R_FAILED(appletStorageRead(&storage, sizeof(hdr), &payload, sizeof(payload))))
                return false;
            // The message is untrusted input. Require NUL termination, a real
            // SD-card NRO path, and an existing regular file before queueing it.
            if (!std::memchr(payload.nro_path, '\0', sizeof(payload.nro_path)) ||
                !std::memchr(payload.argv, '\0', sizeof(payload.argv)) ||
                !IsSafeHomebrewPath(payload.nro_path))
                return false;
            g_PendingTitle = 0;
            g_PendingReturnToGame = false;
            g_PendingHomebrew = true;
            std::strncpy(g_PendingHomebrewPath, payload.nro_path,
                         sizeof(g_PendingHomebrewPath) - 1);
            std::strncpy(g_PendingHomebrewArgv, payload.argv,
                         sizeof(g_PendingHomebrewArgv) - 1);
            DaemonLog("ipc: validated homebrew launch request: %s", g_PendingHomebrewPath);
            return true;
        }

        case Command::ReturnToGame:
            if (hdr.payload_size != 0) return false;
            g_PendingTitle = 0;
            g_PendingHomebrew = false;
            g_PendingReturnToGame = true;
            DaemonLog("ipc: return-to-game requested");
            return true;

        default:
            DaemonLog("ipc: ignored unknown command=%u", hdr.command);
            return false;
    }
}

void DrainMenuCommands() {
    // Drain once more after a menu applet has finished but before its holder
    // is closed; its final launch request can be queued immediately before exit.
    if (!la::g_MenuRunning) return;

    for (int n = 0; n < 16; ++n) {
        AppletStorage storage{};
        const Result rc = la::PopOutData(&storage);
        if (R_FAILED(rc)) break;
        ParseMenuCommand(storage);
        appletStorageClose(&storage);
    }
}

// Emergency recovery for the one situation where replacing qlaunch can
// otherwise strand the user without a HOME UI: every known menu slot has failed.
// This deliberately difficult chord is disabled while a title or menu is alive
// and only moves a qlaunch directory that carries our own install marker.
void CheckEmergencyRollback() {
    if (!g_MenuSlotExhausted || la::g_MenuRunning || la::IsMenuAlive() || app::g_AppRunning) {
        g_RecoveryChordStartTick = 0;
        g_RecoveryChordAttempted = false;
        return;
    }

    padUpdate(&g_RecoveryPad);
    constexpr u64 chord = HidNpadButton_L | HidNpadButton_R |
                          HidNpadButton_ZL | HidNpadButton_ZR |
                          HidNpadButton_Plus | HidNpadButton_Minus |
                          HidNpadButton_Down;
    const u64 buttons = padGetButtons(&g_RecoveryPad);
    if ((buttons & chord) != chord) {
        g_RecoveryChordStartTick = 0;
        g_RecoveryChordAttempted = false;
        return;
    }
    if (g_RecoveryChordAttempted) return;

    const u64 now = armGetSystemTick();
    if (g_RecoveryChordStartTick == 0) {
        g_RecoveryChordStartTick = now;
        return;
    }
    if (now - g_RecoveryChordStartTick < armGetSystemTickFreq() * kRecoveryChordHoldSeconds)
        return;

    g_RecoveryChordAttempted = true;
    FILE* markerFile = std::fopen(kInstallMarker, "r");
    if (!markerFile) {
        DaemonLog("recovery: chord refused; DeskNX install marker not found");
        return;
    }
    char marker[128]{};
    const bool readOk = std::fgets(marker, sizeof(marker), markerFile) != nullptr;
    std::fclose(markerFile);
    marker[std::strcspn(marker, "\r\n")] = '\0';
    if (!readOk || std::strcmp(marker, kInstallMarkerText) != 0) {
        DaemonLog("recovery: chord refused; install marker did not match");
        return;
    }

    struct stat current{};
    struct stat disabled{};
    if (stat(kQlaunchOverride, &current) != 0 || !S_ISDIR(current.st_mode)) {
        DaemonLog("recovery: chord refused; qlaunch override path is not a directory");
        return;
    }
    if (stat(kQlaunchDisabled, &disabled) == 0) {
        DaemonLog("recovery: chord refused; destination already exists: %s", kQlaunchDisabled);
        return;
    }
    if (std::rename(kQlaunchOverride, kQlaunchDisabled) != 0) {
        DaemonLog("recovery: failed to disable qlaunch override (errno=%d)", errno);
        return;
    }

    DaemonLog("recovery: disabled the marked DeskNX qlaunch override; requesting reboot to stock HOME");
    const Result rebootRc = appletRequestToReboot();
    if (R_FAILED(rebootRc)) {
        if (std::rename(kQlaunchDisabled, kQlaunchOverride) == 0)
            DaemonLog("recovery: reboot request failed (0x%08X); qlaunch override restored",
                      static_cast<unsigned int>(rebootRc));
        else
            DaemonLog("recovery: reboot request failed (0x%08X) and automatic restoration failed; SD-card path remains %s",
                      static_cast<unsigned int>(rebootRc), kQlaunchDisabled);
        return;
    }

    // libnx enters its reboot wait loop on success. If the firmware unexpectedly
    // returns, leave the override disabled rather than restoring a potential boot loop.
    DaemonLog("recovery: reboot API returned success unexpectedly; override remains disabled");
}

void OpenMenuForHomePress() {
    if (la::IsMenuAlive()) {
        if (g_HomebrewLoaderRunning) {
            // A Homebrew NRO shares the hbloader library-applet process. HOME
            // returns to DeskNX by ending that session and restoring the real
            // Album slot immediately.
            la::StopMenu();
            g_HomebrewLoaderRunning = false;
            ecs::UnregisterExternalContent(ecs::HbloaderProgramId);
            remove(kHbTargetPath);
            remove(kHbTargetTemp);
            DaemonLog("home: stopped Homebrew loader and returned to DeskNX");
            LaunchMenu();
            return;
        }
        // HOME while the launcher is already visible dismisses it back to the
        // suspended title, matching the stock HOME toggle where possible.
        if (app::g_AppRunning) {
            la::StopMenu();
            app::FocusApplication();
            DaemonLog("home: dismissed DeskNX, returned to title=0x%016llX",
                      static_cast<unsigned long long>(app::g_AppId));
        }
        return;
    }

    if (app::g_AppRunning) {
        if (!app::CanSuspend()) {
            DaemonLog("home: ignored during title foreground-acquisition window");
            return;
        }
        const Result rc = app::FocusSystem();
        DaemonLog("home: bringing system foreground rc=0x%08X",
                  static_cast<unsigned int>(rc));
    }
    LaunchMenu();
}

void HandleSleepMessage() {
    g_ReopenMenuAfterSleep = la::IsMenuAlive();
    if (g_ReopenMenuAfterSleep) {
        la::StopMenu();
        if (g_HomebrewLoaderRunning) {
            g_HomebrewLoaderRunning = false;
            ecs::UnregisterExternalContent(ecs::HbloaderProgramId);
            remove(kHbTargetPath);
            remove(kHbTargetTemp);
            DaemonLog("power: ended Homebrew session before sleep");
        }
    }

    const Result rc = appletStartSleepSequence(true);
    DaemonLog("power: sleep sequence rc=0x%08X", static_cast<unsigned int>(rc));
}

// qlaunch-only messages omitted from libnx's public AppletMessage enum.
// Values are defined by Horizon AM for the system applet; keep them local so
// normal Homebrew builds do not depend on undocumented libnx names.
enum SystemAppletMessage : u32 {
    Msg_DetectShortPressingHomeButton  = 20,
    Msg_DetectShortPressingPowerButton = 22,
    Msg_RequestToPrepareSleep          = 25,
    Msg_FinishedSleepSequence          = 26,
    Msg_SleepRequiredByHighTemperature = 27,
    Msg_SleepRequiredByLowBattery      = 28,
    Msg_AutoPowerDown                  = 29,
    Msg_SdCardRemoved                  = 33,
};

void PumpAppletMessages() {
    u32 msg = 0;
    while (R_SUCCEEDED(appletGetMessage(&msg))) {
        switch (msg) {
            case Msg_DetectShortPressingHomeButton:
                OpenMenuForHomePress();
                break;

            case Msg_DetectShortPressingPowerButton:
            case Msg_AutoPowerDown:
            case Msg_RequestToPrepareSleep:
            case Msg_SleepRequiredByHighTemperature:
            case Msg_SleepRequiredByLowBattery:
                HandleSleepMessage();
                break;

            case Msg_FinishedSleepSequence:
                DaemonLog("power: wake event");
                if (g_ReopenMenuAfterSleep) {
                    g_ReopenMenuAfterSleep = false;
                    LaunchMenu();
                }
                break;

            case Msg_SdCardRemoved:
                DaemonLog("storage: SD card removed; closing SD-backed menu");
                if (la::IsMenuAlive()) la::StopMenu();
                if (g_HomebrewLoaderRunning) {
                    g_HomebrewLoaderRunning = false;
                    ecs::UnregisterExternalContent(ecs::HbloaderProgramId);
                    remove(kHbTargetPath);
                    remove(kHbTargetTemp);
                }
                break;

            default:
                // Avoid flooding logs for routine notifications.
                break;
        }
    }
}

void HandleMenuClosed() {
    DrainMenuCommands();
    const bool didHandshake = g_MenuReady;
    const u64 launchTick = g_MenuLaunchTick;
    la::StopMenu();
    g_MenuReady = false;
    g_LastMenuHeartbeatTick = 0;

    // The shared AppletHolder also runs nx-hbloader. Its exit is not a failed
    // menu startup: unregister its ECS override and restore DeskNX, even if the
    // chosen NRO exits without chainloading or fails to start.
    if (g_HomebrewLoaderRunning) {
        g_HomebrewLoaderRunning = false;
        ecs::UnregisterExternalContent(ecs::HbloaderProgramId);
        remove(kHbTargetPath);
        remove(kHbTargetTemp);
        DaemonLog("hb: loader session ended; restoring DeskNX");
        LaunchMenu();
        return;
    }

    const u64 lifetimeTicks = launchTick != 0 ? armGetSystemTick() - launchTick : 0;
    const u64 lifetimeMs = lifetimeTicks * 1000 / armGetSystemTickFreq();

    if (g_PendingHomebrew) {
        char path[desknx::ipc::MaxHomebrewPath]{};
        char argv[desknx::ipc::MaxHomebrewArgv]{};
        std::strncpy(path, g_PendingHomebrewPath, sizeof(path) - 1);
        std::strncpy(argv, g_PendingHomebrewArgv, sizeof(argv) - 1);
        g_PendingHomebrew = false;

        if (!WriteHomebrewTarget(path, argv)) {
            DaemonLog("hb: could not stage target NRO; returning to DeskNX");
            LaunchMenu();
            return;
        }

        Result rc = ecs::RegisterExternalContent(ecs::HbloaderProgramId, ecs::HbloaderExefsDir);
        if (R_FAILED(rc)) {
            DaemonLog("hb: ECS register failed rc=0x%08X", static_cast<unsigned int>(rc));
            remove(kHbTargetPath);
            LaunchMenu();
            return;
        }

        g_HomebrewLoaderRunning = true;
        g_MenuReady = false;
        g_MenuLaunchTick = 0; // hbloader does not send MenuReady
        g_LastMenuHeartbeatTick = 0;
        rc = la::LaunchMenu(ecs::HbloaderAppletId, nullptr, 0);
        if (R_FAILED(rc)) {
            g_HomebrewLoaderRunning = false;
            ecs::UnregisterExternalContent(ecs::HbloaderProgramId);
            remove(kHbTargetPath);
            DaemonLog("hb: hbloader launch failed rc=0x%08X", static_cast<unsigned int>(rc));
            LaunchMenu();
            return;
        }
        DaemonLog("hb: launched %s through the album applet slot", path);
        return;
    }

    if (g_PendingTitle != 0) {
        const u64 titleId = g_PendingTitle;
        g_PendingTitle = 0;
        g_PendingReturnToGame = false;

        const Result rc = app::Launch(titleId, g_SelectedUser);
        DaemonLog("app: launch title=0x%016llX rc=0x%08X",
                  static_cast<unsigned long long>(titleId), static_cast<unsigned int>(rc));
        if (R_FAILED(rc))
            LaunchMenu();
        return;
    }

    if (g_PendingReturnToGame) {
        g_PendingReturnToGame = false;
        if (app::g_AppRunning) {
            app::FocusApplication();
            DaemonLog("app: returned to title=0x%016llX",
                      static_cast<unsigned long long>(app::g_AppId));
        } else {
            LaunchMenu();
        }
        return;
    }

    if (!didHandshake && lifetimeMs < kMenuFastExitMs &&
        g_MenuSlotIndex + 1 < g_ActiveMenuSlotCount && !g_MenuSlotExhausted) {
        ++g_MenuSlotIndex;
        DaemonLog("menu: applet exited before ready after %llu ms; trying slot=%s",
                  static_cast<unsigned long long>(lifetimeMs), CurrentMenuSlot().name);
    } else if (!didHandshake) {
        DaemonLog("menu: closed without ready handshake after %llu ms",
                  static_cast<unsigned long long>(lifetimeMs));
    }

    if (app::g_AppRunning)
        app::FocusApplication();
    else
        LaunchMenu();
}

void RunMainLoop() {
    InitializeMenuSlots();
    DaemonLog("backend: starting; current slot=%s", CurrentMenuSlot().name);

    // This policy is part of system-applet startup, not a cosmetic preference.
    // Without it, a library applet can render but fail to take the foreground.
    const Result idleRc = appletLoadAndApplyIdlePolicySettings();
    DaemonLog("backend: apply idle/power policy rc=0x%08X",
              static_cast<unsigned int>(idleRc));

    PickSelectedUser();
    DaemonLog("account: selected user valid=%d", accountUidIsValid(&g_SelectedUser) ? 1 : 0);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_RecoveryPad);

    // DeskNX starts only after the operator has installed the qlaunch override.
    // Keeping the original title override untouched is handled by the installer.
    LaunchMenu();

    while (true) {
        if (la::IsMenuAlive())
            DrainMenuCommands();
        PumpAppletMessages();

        // A slot that starts but never speaks is probably the firmware's own
        // applet or a crashed menu. Stop it and test the next known slot instead
        // of leaving the HOME button attached to a frozen screen forever.
        if (la::IsMenuAlive() && !g_HomebrewLoaderRunning && !g_MenuReady && g_MenuLaunchTick != 0) {
            const u64 liveTicks = armGetSystemTick() - g_MenuLaunchTick;
            if (liveTicks >= armGetSystemTickFreq() * kMenuReadyTimeoutSeconds) {
                DaemonLog("menu: no ready IPC after %llu seconds in slot=%s",
                          static_cast<unsigned long long>(kMenuReadyTimeoutSeconds),
                          CurrentMenuSlot().name);
                la::StopMenu();
                g_MenuReady = false;
                g_LastMenuHeartbeatTick = 0;
                if (app::g_AppRunning) {
                    // Failed HOME applet: restore the game and wait for another
                    // HOME press rather than repeatedly stealing the foreground.
                    app::FocusApplication();
                    g_MenuSlotExhausted = true;
                    g_NextMenuRetryTick = armGetSystemTick() +
                                          armGetSystemTickFreq() * kMenuRetryDelaySeconds;
                } else if (g_MenuSlotIndex + 1 < g_ActiveMenuSlotCount) {
                    ++g_MenuSlotIndex;
                    g_MenuSlotExhausted = false;
                    DaemonLog("menu: testing fallback slot=%s", CurrentMenuSlot().name);
                } else {
                    g_MenuSlotIndex = 0;
                    g_MenuSlotExhausted = true;
                    g_NextMenuRetryTick = armGetSystemTick() +
                                          armGetSystemTickFreq() * kMenuRetryDelaySeconds;
                }
            }
        }

        // A menu can finish startup and then hang. The heartbeat distinguishes
        // a responsive workspace from an applet that remains alive but stopped
        // processing input. When a game is suspended, restore it and wait for
        // the next HOME press; never retry in a loop that steals its foreground.
        if (la::IsMenuAlive() && !g_HomebrewLoaderRunning && g_MenuReady && g_LastMenuHeartbeatTick != 0) {
            const u64 silentTicks = armGetSystemTick() - g_LastMenuHeartbeatTick;
            if (silentTicks >= armGetSystemTickFreq() * kMenuHeartbeatTimeoutSeconds) {
                DaemonLog("menu: heartbeat timed out after %llu seconds; restarting slot=%s",
                          static_cast<unsigned long long>(kMenuHeartbeatTimeoutSeconds),
                          CurrentMenuSlot().name);
                la::StopMenu();
                g_MenuReady = false;
                g_LastMenuHeartbeatTick = 0;
                if (app::g_AppRunning) {
                    app::FocusApplication();
                    g_MenuSlotExhausted = true;
                    g_NextMenuRetryTick = armGetSystemTick() +
                                          armGetSystemTickFreq() * kMenuRetryDelaySeconds;
                } else if (g_MenuSlotIndex + 1 < g_ActiveMenuSlotCount) {
                    ++g_MenuSlotIndex;
                    g_MenuSlotExhausted = false;
                } else {
                    g_MenuSlotIndex = 0;
                    g_MenuSlotExhausted = true;
                    g_NextMenuRetryTick = armGetSystemTick() +
                                          armGetSystemTickFreq() * kMenuRetryDelaySeconds;
                }
            }
        }

        if (la::g_MenuRunning && !la::IsMenuAlive()) {
            HandleMenuClosed();
        } else if (!la::g_MenuRunning && app::g_AppRunning && app::Update()) {
            DaemonLog("app: title exited naturally");
            LaunchMenu();
         } else if (!la::g_MenuRunning && !app::g_AppRunning && g_MenuSlotExhausted &&
                   armGetSystemTick() >= g_NextMenuRetryTick) {
            // Reset to the preferred slot on every retry so transient startup
            // failures don't permanently pin a console to the fallback slot.
            g_MenuSlotIndex = 0;
            g_MenuSlotExhausted = false;
            LaunchMenu();
        } else if (!la::g_MenuRunning && !app::g_AppRunning && !g_MenuSlotExhausted) {
            LaunchMenu();
        }

        CheckEmergencyRollback();
        svcSleepThread(16'666'666ULL);
    }
}

} // namespace

// Stratosphere sysmodule entry. The service is the qlaunch process, so it must
// never return during normal operation. The heaps are intentionally separated:
 // libstratosphere allocator and libnx/newlib allocator.
extern "C" {
    extern u8* fake_heap_start;
    extern u8* fake_heap_end;
    extern AppletType __nx_applet_type;
    extern u32 __nx_fs_num_sessions;
}

namespace {
constexpr size_t kStratosphereHeapSize = 16 * 1024 * 1024;
alignas(ams::os::MemoryPageSize) constinit u8 g_StratosphereHeap[kStratosphereHeapSize];
constexpr size_t kLibnxHeapSize = 4 * 1024 * 1024;
alignas(ams::os::MemoryPageSize) constinit u8 g_LibnxHeap[kLibnxHeapSize];
}

namespace {

struct InitStep {
    const char* name;
    Result rc;
    bool critical;
};
InitStep g_InitSteps[16]{};
size_t g_InitStepCount = 0;

Result RecordInit(const char* name, Result rc, bool critical) {
    if (g_InitStepCount < sizeof(g_InitSteps) / sizeof(g_InitSteps[0]))
        g_InitSteps[g_InitStepCount++] = {name, rc, critical};
    return rc;
}

} // namespace

namespace ams {

namespace init {

void InitializeSystemModuleBeforeConstructors() {
    init::InitializeAllocator(g_StratosphereHeap, kStratosphereHeapSize);
    fake_heap_start = g_LibnxHeap;
    fake_heap_end = g_LibnxHeap + kLibnxHeapSize;
}

void InitializeSystemModule() {
    __nx_applet_type = AppletType_SystemApplet;
    __nx_fs_num_sessions = 3;

    // sm/fs are prerequisites for every other service and for SD-backed logs.
    R_ABORT_UNLESS(sm::Initialize());
    R_ABORT_UNLESS(fsInitialize());

    // libnx chooses IPC command/layouts by HOS version. Discover the installed
    // firmware before initializing account/ns/applet services.
    RecordInit("setsys", setsysInitialize(), true);
    if (hosversionGet() == 0) {
        SetSysFirmwareVersion fw{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
    }

    RecordInit("applet", appletInitialize(), true);
    RecordInit("hid", hidInitialize(), true);
    RecordInit("account", accountInitialize(AccountServiceType_System), true);
    RecordInit("ns", nsInitialize(), true);
    RecordInit("set", setInitialize(), true);
    RecordInit("ldr:shel", ldrShellInitialize(), true);
    // pmshell is needed for reliable tracking/termination but sLaunch treats it
    // as optional so a service refusal cannot turn into a black-screen boot.
    RecordInit("pmshell", pmshellInitialize(), false);

    // The SD card must be mounted before diagnostics can be persisted.
    R_ABORT_UNLESS(fsdevMountSdmc());
    MakeDirectories();
    remove(kPrevLogPath);
    rename(kLogPath, kPrevLogPath);

    DaemonLog("backend: firmware=%u.%u.%u",
              HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()),
              HOSVER_MICRO(hosversionGet()));
    for (size_t i = 0; i < g_InitStepCount; ++i) {
        if (R_SUCCEEDED(g_InitSteps[i].rc)) continue;
        DaemonLog("service init failed: %s rc=0x%08X%s",
                  g_InitSteps[i].name, static_cast<unsigned int>(g_InitSteps[i].rc),
                  g_InitSteps[i].critical ? " (critical)" : " (optional)");
    }
    for (size_t i = 0; i < g_InitStepCount; ++i)
        if (g_InitSteps[i].critical)
            R_ABORT_UNLESS(g_InitSteps[i].rc);

    DaemonLog("backend: core services initialized");
}

void FinalizeSystemModule() {
    pmshellExit();
    ldrShellExit();
    setExit();
    setsysExit();
    nsExit();
    accountExit();
    hidExit();
    appletExit();
    fsdevUnmountAll();
    fsExit();
}

} // namespace init

void Main() {
    RunMainLoop();
}

NORETURN void Exit(int) {
    AMS_ABORT("DeskNX qlaunch backend must not exit");
}

} // namespace ams
