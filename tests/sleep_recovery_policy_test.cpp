#include <desknx/SleepRecoveryPolicy.hpp>

#include <cassert>

using desknx::runtime::DecideSleepStartFailureRecovery;
using desknx::runtime::DecideSleepWakeRecovery;
using desknx::runtime::SleepRecoveryAction;

int main() {
    // Sleep API failure, menu was open:
    assert(DecideSleepStartFailureRecovery(true, true, true, true, true) ==
           SleepRecoveryAction::KeepMenuActive);
    assert(DecideSleepStartFailureRecovery(true, true, true, false, true) ==
           SleepRecoveryAction::KeepMenuStarting);
    // The holder can be completed but not yet joined/closed. Reap before retry.
    assert(DecideSleepStartFailureRecovery(true, false, true, true, true) ==
           SleepRecoveryAction::ReapMenuHolder);
    assert(DecideSleepStartFailureRecovery(true, false, false, false, true) ==
           SleepRecoveryAction::LaunchMenu);

    // Sleep API failure, no menu was open:
    assert(DecideSleepStartFailureRecovery(false, false, false, false, true) ==
           SleepRecoveryAction::RestoreGame);
    assert(DecideSleepStartFailureRecovery(false, false, false, false, false) ==
           SleepRecoveryAction::LaunchMenu);

    // Wake with menu intent follows the same holder ownership barrier.
    assert(DecideSleepWakeRecovery(true, true, true, true, true, true) ==
           SleepRecoveryAction::KeepMenuActive);
    assert(DecideSleepWakeRecovery(true, true, true, false, true, true) ==
           SleepRecoveryAction::KeepMenuStarting);
    assert(DecideSleepWakeRecovery(true, false, true, false, true, true) ==
           SleepRecoveryAction::ReapMenuHolder);
    assert(DecideSleepWakeRecovery(true, false, false, false, true, true) ==
           SleepRecoveryAction::LaunchMenu);

    // Without menu intent, restore the game only if it still owns a live holder.
    assert(DecideSleepWakeRecovery(false, false, false, false, true, true) ==
           SleepRecoveryAction::RestoreGame);
    assert(DecideSleepWakeRecovery(false, false, false, false, true, false) ==
           SleepRecoveryAction::LaunchMenu);
    assert(DecideSleepWakeRecovery(false, false, false, false, false, true) ==
           SleepRecoveryAction::LaunchMenu);

    return 0;
}
