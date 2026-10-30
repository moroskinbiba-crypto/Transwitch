#pragma once

// Derived from sLaunch (GPL-3.0-or-later); see THIRD_PARTY.md.
#include <switch.h>

namespace desknx::system::app {

    // Tracks the currently running application (if any).
    // Applications use the dedicated AppletApplication type (distinct from the
    // AppletHolder used for library applets) in the current libnx applet API.
    extern AppletApplication g_AppHolder;
    extern u64          g_AppId;
    extern bool         g_AppRunning;
    extern bool         g_AppHasFocus;

    // Title id of the most recent natural exit reported by Update(), so the
    // backend can name the game in the AppExited event (the holder and
    // g_AppId are already cleared by then). Reset on every Launch/Terminate.
    extern u64          g_ExitAppId;

    // Launch a game. Sends the preselected-user parameter.
    // Caller must have already called SetSelectedUser.
    // A save could not be created for lack of space (Launch returns it).
    bool IsNoSpace(Result rc);

    // The game's NACP asks for a user at launch (StartupUserAccount set).
    bool WantsUser(u64 app_id);

    Result Launch(u64 app_id, AccountUid user, u64 flags = 0);

    // Give foreground back to the currently suspended application
    Result Resume();

    // Let the PM service confirm process exit when available. If it is not
    // available, use AM's direct terminate command instead of treating a failed
    // PID query as proof that the game has exited.
    void   SetProcessMonitorAvailable(bool available);
    Result Terminate();

    // Called each frame from the main loop; returns true when the app exited
    bool   Update(bool request_foreground = true);

    // Give foreground to the application (take it from ourselves)
    Result FocusApplication();

    // Take foreground back from the application (bring menu up)
    Result FocusSystem();

    // True once a freshly launched app has had time to acquire the foreground.
    // Suspending (HOME) before this races the app's launch/foreground handshake
    // and faults am, so the daemon must wait for it.
    bool   CanSuspend();

} // namespace desknx::system::app
