#pragma once

#include <cstdint>

namespace desknx::runtime {

enum class SleepRecoveryAction : std::uint8_t {
    KeepMenuActive = 0,
    KeepMenuStarting,
    ReapMenuHolder,
    LaunchMenu,
    RestoreGame,
};

// Decide how to recover when AM rejects the sleep sequence. A live menu is
// reused even if the pre-sleep snapshot raced; a completed-but-still-owned
// holder is always reaped before a new applet may be created.
constexpr SleepRecoveryAction DecideSleepStartFailureRecovery(
    bool menuWasOpen,
    bool menuAlive,
    bool menuHolderRunning,
    bool menuReady,
    bool appRunning) noexcept {
    if (menuAlive)
        return menuReady ? SleepRecoveryAction::KeepMenuActive
                         : SleepRecoveryAction::KeepMenuStarting;
    if (menuHolderRunning)
        return SleepRecoveryAction::ReapMenuHolder;
    if (menuWasOpen || !appRunning)
        return SleepRecoveryAction::LaunchMenu;
    return SleepRecoveryAction::RestoreGame;
}

// Same holder ownership rules after a successful wake event. Reuse any live
// menu, reap a finished holder, then honor the pre-sleep menu/game intent.
constexpr SleepRecoveryAction DecideSleepWakeRecovery(
    bool reopenMenu,
    bool menuAlive,
    bool menuHolderRunning,
    bool menuReady,
    bool appWasRunningBeforeSleep,
    bool appRunning) noexcept {
    if (menuAlive)
        return menuReady ? SleepRecoveryAction::KeepMenuActive
                         : SleepRecoveryAction::KeepMenuStarting;
    if (menuHolderRunning)
        return SleepRecoveryAction::ReapMenuHolder;
    if (reopenMenu)
        return SleepRecoveryAction::LaunchMenu;
    return appWasRunningBeforeSleep && appRunning
        ? SleepRecoveryAction::RestoreGame
        : SleepRecoveryAction::LaunchMenu;
}

} // namespace desknx::runtime
