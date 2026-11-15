#pragma once
#include <switch.h>

namespace desknx::system::la {

// Owns the one foreground library-applet holder used by the HOME menu.
extern AppletHolder g_MenuHolder;
extern bool g_MenuRunning;

Result LaunchMenu(AppletId applet_id, const void* initial_data = nullptr, size_t initial_size = 0);
Result StopMenu();
bool IsMenuAlive();
Result PopOutData(AppletStorage* storage); // legacy normal output queue
Result PopInteractiveOutData(AppletStorage* storage);
Event* CommandEvent(); // lazily acquired interactive output readiness event

} // namespace desknx::system::la
