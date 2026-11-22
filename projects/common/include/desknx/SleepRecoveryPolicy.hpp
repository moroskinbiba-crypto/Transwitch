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

// Decide how to recover when AM rejects the sleep sequence. A menu applet
// that is still alive must be reused; a completed but not yet joined holder
// must be reaped before LaunchMenu can be called again.
constexpr SleepRecoveryAction DecideSleepStartFailureRecovery(
    bool menuWasOpen,
    bool menuAlive,
    bool menuHolderRunning,
    bool menuReady,
    bool appRunning) noexcept {
    if (menuWasOpen) {
        if (menuAlive)
            return menuReady ? SleepRecoveryAction::KeepMenuActive
                             : SleepRecoveryAction::KeepMenuStarting;
        if (menuHolderRunning)
            return SleepRecoveryAction::ReapMenuHolder;
        return SleepRecoveryAction::LaunchMenu;
    }
    return appRunning ? SleepRecoveryAction::RestoreGame
                      : SleepRecoveryAction::LaunchMenu;
}

// Same ownership rules after a successful wake event. The caller supplies the
// pre-sleep foreground intent separately from the current application state.
constexpr SleepRecoveryAction DecideSleepWakeRecovery(
    bool reopenMenu,
    bool menuAlive,
    bool menuHolderRunning,
    bool menuReady,
    bool appWasRunningBeforeSleep,
    bool appRunning) noexcept {
    if (reopenMenu) {
        if (menuAlive)
            return menuReady ? SleepRecoveryAction::KeepMenuActive
                             : SleepRecoveryAction::KeepMenuStarting;
        if (menuHolderRunning)
            return SleepRecoveryAction::ReapMenuHolder;
        return SleepRecoveryAction::LaunchMenu;
    }
    return appWasRunningBeforeSleep && appRunning
        ? SleepRecoveryAction::RestoreGame
        : SleepRecoveryAction::LaunchMenu;
}

} // namespace desknx::runtime
