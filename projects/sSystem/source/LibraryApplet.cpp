#include <desknx/system/LibraryApplet.hpp>
#include <cstring>

// Applet lifecycle helper follows the public libnx library-applet model.
namespace desknx::system::la {

AppletHolder g_MenuHolder{};
bool g_MenuRunning = false;
static bool g_StopRequested = false;
static u64 g_StopRetryAfterTick = 0;
static bool g_CommandEventInitialized = false;
static bool g_CommandEventAvailable = false;

Result LaunchMenu(AppletId applet_id, const void* initial_data, size_t initial_size) {
    if (g_MenuRunning)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
    g_CommandEventInitialized = false;
    g_CommandEventAvailable = false;
    Result rc = appletCreateLibraryApplet(&g_MenuHolder, applet_id, LibAppletMode_AllForeground);
    if (R_FAILED(rc))
        return rc;

    LibAppletArgs args{};
    libappletArgsCreate(&args, 0);
    libappletArgsSetPlayStartupSound(&args, false);
    rc = libappletArgsPush(&args, &g_MenuHolder);

    if (R_SUCCEEDED(rc) && initial_data != nullptr && initial_size != 0)
        rc = libappletPushInData(&g_MenuHolder, initial_data, initial_size);
    if (R_SUCCEEDED(rc))
        rc = appletHolderStart(&g_MenuHolder);

    if (R_FAILED(rc)) {
        appletHolderClose(&g_MenuHolder);
        std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
        g_CommandEventInitialized = false;
        g_CommandEventAvailable = false;
        return rc;
    }

    g_MenuRunning = true;
    return 0;
}

Result StopMenu() {
    if (!g_MenuRunning) {
        g_StopRequested = false;
        g_StopRetryAfterTick = 0;
        return 0;
    }

    // Retire an applet that completed between retries before issuing another
    // request to its accessor. For a LibraryApplet this StateChangedEvent is
    // libnx's completion event, checked by appletHolderCheckFinished().
    if (appletHolderCheckFinished(&g_MenuHolder)) {
        appletHolderJoin(&g_MenuHolder);
        appletHolderClose(&g_MenuHolder);
        std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
        g_MenuRunning = false;
        g_StopRequested = false;
        g_StopRetryAfterTick = 0;
        return 0;
    }

    const u64 now = armGetSystemTick();
    const u64 frequency = armGetSystemTickFreq();
    if (g_StopRetryAfterTick != 0 && now < g_StopRetryAfterTick)
        return KERNELRESULT(TimedOut);
    g_StopRetryAfterTick = 0;

    if (!g_StopRequested) {
        // libnx waits for RequestExit, but if that times out it only sends the
        // Terminate IPC command and returns when the command is accepted.
        const Result requestRc =
            appletHolderRequestExitOrTerminate(&g_MenuHolder, 5'000'000'000ULL);
        if (R_FAILED(requestRc) && !appletHolderCheckFinished(&g_MenuHolder)) {
            g_StopRetryAfterTick = now + frequency * 2;
            return requestRc;
        }
        g_StopRequested = true;
    }

    // libnx's appletHolderRequestExitOrTerminate() sends RequestExit and waits
    // for StateChangedEvent; on timeout it sends Terminate but returns without
    // waiting for the applet's final exit notification. Wait for the official
    // finished event after that fallback too, then verify with the matching
    // appletHolderCheckFinished() API before Join/Close.
    if (!appletHolderCheckFinished(&g_MenuHolder)) {
        const Result waitRc = eventWait(&g_MenuHolder.StateChangedEvent, 5'000'000'000ULL);
        if (R_FAILED(waitRc)) {
            g_StopRetryAfterTick = armGetSystemTick() + frequency * 2;
            return waitRc;
        }
    }
    if (!appletHolderCheckFinished(&g_MenuHolder)) {
        g_StopRetryAfterTick = armGetSystemTick() + frequency * 2;
        return KERNELRESULT(TimedOut);
    }

    appletHolderJoin(&g_MenuHolder);
    appletHolderClose(&g_MenuHolder);
    std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
    g_CommandEventInitialized = false;
    g_CommandEventAvailable = false;
    g_MenuRunning = false;
    g_StopRequested = false;
    g_StopRetryAfterTick = 0;
    return 0;
}

bool IsMenuAlive() {
    return g_MenuRunning && !appletHolderCheckFinished(&g_MenuHolder);
}

Result PopOutData(AppletStorage* storage) {
    if (!storage || !g_MenuRunning)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return appletHolderPopOutData(&g_MenuHolder, storage);
}

Result PopInteractiveOutData(AppletStorage* storage) {
    if (!storage || !g_MenuRunning)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return appletHolderPopInteractiveOutData(&g_MenuHolder, storage);
}

Event* CommandEvent() {
    if (!g_MenuRunning) return nullptr;
    if (!g_CommandEventInitialized) {
        g_CommandEventInitialized = true;
        Event* event = nullptr;
        const Result rc = appletHolderGetPopInteractiveOutDataEvent(&g_MenuHolder, &event);
        g_CommandEventAvailable = R_SUCCEEDED(rc) && event != nullptr;
        if (!g_CommandEventAvailable) {
            // A missing command-event handle must never be passed to waitObjects.
            return nullptr;
        }
    }
    return g_CommandEventAvailable ? &g_MenuHolder.PopInteractiveOutDataEvent : nullptr;
}

} // namespace desknx::system::la
