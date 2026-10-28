#pragma once
#include <switch.h>

namespace desknx::system::la {

// Owns the one foreground library-applet holder used by the HOME menu.
extern AppletHolder g_MenuHolder;
extern bool g_MenuRunning;

Result LaunchMenu(AppletId applet_id, const void* initial_data = nullptr, size_t initial_size = 0);
void StopMenu();
bool IsMenuAlive();
Result PopOutData(AppletStorage* storage);

} // namespace desknx::system::la
