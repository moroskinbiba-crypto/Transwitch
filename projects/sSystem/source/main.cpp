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
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace {

using desknx::ipc::Command;
using namespace desknx::system;

constexpr const char* kDataDir = "sdmc:/switch/DeskNX";
constexpr const char* kLogDir = "sdmc:/switch/DeskNX/logs";
constexpr const char* kLogPath = "sdmc:/switch/DeskNX/logs/daemon.log";
constexpr const char* kPrevLogPath = "sdmc:/switch/DeskNX/logs/daemon.prev.log";
constexpr u64 kHomePressSettleMs = 1500;
constexpr u64 kMenuFastExitMs = 3000;
constexpr u64 kMenuStartRetryDelayNs = 500'000'000ULL;
constexpr int kMenuStartAttempts = 4;

AccountUid g_SelectedUser{};
u64 g_PendingTitle = 0;
bool g_PendingReturnToGame = false;
bool g_MenuReady = false;
u64 g_MenuLaunchTick = 0;
size_t g_MenuSlotIndex = 0;
bool g_MenuSlotExhausted = false;
bool g_ReopenMenuAfterSleep = false;

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

const desknx::system::ecs::MenuSlot& CurrentMenuSlot() {
    return desknx::system::ecs::MenuSlots[g_MenuSlotIndex];
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
            return true;
        }
    }

    if (g_MenuSlotIndex + 1 < ecs::MenuSlotCount) {
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
                return true;
            }
        }
    }

    g_MenuSlotExhausted = true;
    DaemonLog("FATAL: none of the supported HOME applet slots could be started");
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
            DaemonLog("ipc: DeskNX menu ready");
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
            DaemonLog("ipc: launch title=0x%016llX",
                      static_cast<unsigned long long>(payload.title_id));
            return true;
        }

        case Command::ReturnToGame:
            if (hdr.payload_size != 0) return false;
            g_PendingTitle = 0;
            g_PendingReturnToGame = true;
            DaemonLog("ipc: return-to-game requested");
            return true;

        default:
            DaemonLog("ipc: ignored unknown command=%u", hdr.command);
            return false;
    }
}

void DrainMenuCommands() {
    if (!la::IsMenuAlive()) return;

    for (int n = 0; n < 16; ++n) {
        AppletStorage storage{};
        const Result rc = la::PopOutData(&storage);
        if (R_FAILED(rc)) break;
        ParseMenuCommand(storage);
        appletStorageClose(&storage);
    }
}

void OpenMenuForHomePress() {
    if (la::IsMenuAlive()) {
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
    if (g_ReopenMenuAfterSleep)
        la::StopMenu();

    const Result rc = appletStartSleepSequence(true);
    DaemonLog("power: sleep sequence rc=0x%08X", static_cast<unsigned int>(rc));
}

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
                break;

            default:
                // Avoid flooding logs for routine notifications.
                break;
        }
    }
}

void HandleMenuClosed() {
    const bool didHandshake = g_MenuReady;
    const u64 lifetimeTicks = armGetSystemTick() - g_MenuLaunchTick;
    const u64 lifetimeMs = lifetimeTicks * 1000 / armGetSystemTickFreq();
    la::StopMenu();
    g_MenuReady = false;

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
        g_MenuSlotIndex + 1 < ecs::MenuSlotCount && !g_MenuSlotExhausted) {
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
    DaemonLog("backend: starting; current slot=%s", CurrentMenuSlot().name);
    PickSelectedUser();
    DaemonLog("account: selected user valid=%d", accountUidIsValid(&g_SelectedUser) ? 1 : 0);

    // DeskNX starts only after the operator has installed the qlaunch override.
    // Keeping the original title override untouched is handled by the installer.
    LaunchMenu();

    while (true) {
        if (la::IsMenuAlive())
            DrainMenuCommands();
        PumpAppletMessages();

        if (la::g_MenuRunning && !la::IsMenuAlive()) {
            HandleMenuClosed();
        } else if (!la::g_MenuRunning && app::g_AppRunning && app::Update()) {
            DaemonLog("app: title exited naturally");
            LaunchMenu();
        } else if (!la::g_MenuRunning && !app::g_AppRunning && !g_MenuSlotExhausted) {
            LaunchMenu();
        }

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

    R_ABORT_UNLESS(sm::Initialize());
    R_ABORT_UNLESS(fsInitialize());

    // Query firmware before initializing services that select IPC layouts by HOS version.
    if (hosversionGet() == 0) {
        if (R_SUCCEEDED(setsysInitialize())) {
            SetSysFirmwareVersion fw{};
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
            setsysExit();
        }
    }

    R_ABORT_UNLESS(appletInitialize());
    R_ABORT_UNLESS(hidInitialize());
    R_ABORT_UNLESS(accountInitialize(AccountServiceType_System));
    R_ABORT_UNLESS(nsInitialize());
    R_ABORT_UNLESS(setsysInitialize());
    R_ABORT_UNLESS(ldrShellInitialize());
    R_ABORT_UNLESS(pmshellInitialize());

    // A failure to mount SD means the menu's executable cannot be served.
    R_ABORT_UNLESS(fsdevMountSdmc());
    MakeDirectories();
    DaemonLog("backend: services initialized, firmware=0x%08X", hosversionGet());
}

} // namespace init

void Main() {
    RunMainLoop();
}

NORETURN void Exit(int) {
    AMS_ABORT("DeskNX qlaunch backend must not exit");
}

} // namespace ams
