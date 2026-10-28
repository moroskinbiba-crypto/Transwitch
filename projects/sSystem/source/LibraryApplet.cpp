#include <desknx/system/LibraryApplet.hpp>
#include <cstring>

// Applet lifecycle helper follows the public libnx library-applet model.
namespace desknx::system::la {

AppletHolder g_MenuHolder{};
bool g_MenuRunning = false;

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

void StopMenu() {
    if (!g_MenuRunning)
        return;

    // Try a graceful close first; libnx terminates the applet if it doesn't exit.
    appletHolderRequestExitOrTerminate(&g_MenuHolder, 5'000'000'000ULL);
    appletHolderClose(&g_MenuHolder);
    std::memset(&g_MenuHolder, 0, sizeof(g_MenuHolder));
    g_MenuRunning = false;
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
