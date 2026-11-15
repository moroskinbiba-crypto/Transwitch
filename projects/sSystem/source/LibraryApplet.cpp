#include <desknx/system/LibraryApplet.hpp>
#include <cstring>

// Applet lifecycle helper follows the public libnx library-applet model.
namespace desknx::system::la {

AppletHolder g_MenuHolder{};
bool g_MenuRunning = false;
static bool g_StopRequested = false;
static u64 g_StopRetryAfterTick = 0;

Result LaunchMenu(AppletId applet_id, const void* initial_data, size_t initial_size) {
    if (g_MenuRunning)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
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
    // request to its accessor. StateChangedEvent itself is not a completion
    // barrier; the wait loop below verifies the terminal state explicitly.
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

    // StateChangedEvent is not an exit event: it also fires for foreground
    // and suspend transitions. Like HomeNX's library-applet runner, treat it
    // only as a wake-up hint and verify appletHolderCheckFinished() after each
    // wake. Never Join/Close a holder until AM confirms actual completion.
    const u64 waitStarted = armGetSystemTick();
    const u64 waitBudgetTicks = frequency * 5;
    while (!appletHolderCheckFinished(&g_MenuHolder)) {
        const u64 elapsed = armGetSystemTick() - waitStarted;
        if (elapsed >= waitBudgetTicks) {
            g_StopRetryAfterTick = armGetSystemTick() + frequency * 2;
            return KERNELRESULT(TimedOut);
        }

        // Use a short bounded wait so a non-terminal state-change signal cannot
        // turn the remaining timeout into an unbounded wait on a stale event.
        const Result waitRc = eventWait(&g_MenuHolder.StateChangedEvent, 250'000'000ULL);
        if (R_FAILED(waitRc) && waitRc != KERNELRESULT(TimedOut)) {
            g_StopRetryAfterTick = armGetSystemTick() + frequency * 2;
            return waitRc;
        }
        if (R_SUCCEEDED(waitRc) && !appletHolderCheckFinished(&g_MenuHolder))
            svcSleepThread(10'000'000ULL);
    }

    appletHolderJoin(&g_MenuHolder);
    appletHolderClose(&g_MenuHolder);
    std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
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

} // namespace desknx::system::la
