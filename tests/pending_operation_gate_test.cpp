#include <desknx/PendingOperationGate.hpp>

#include <cassert>

using desknx::runtime::PendingOperation;
using desknx::runtime::PendingOperationGate;

int main() {
    PendingOperationGate gate;
    assert(!gate.busy());
    assert(gate.current() == PendingOperation::None);

    // Exactly one deferred operation owns the menu-close transaction.
    assert(gate.TryBegin(PendingOperation::LaunchTitle));
    assert(gate.busy());
    assert(gate.current() == PendingOperation::LaunchTitle);
    assert(!gate.TryBegin(PendingOperation::LaunchTitle));
    assert(!gate.TryBegin(PendingOperation::LaunchHomebrew));
    assert(!gate.TryBegin(PendingOperation::PowerAction));
    assert(!gate.TryBegin(PendingOperation::None));

    // A mismatched completion must not release somebody else's operation.
    assert(!gate.Complete(PendingOperation::CloseApp));
    assert(gate.current() == PendingOperation::LaunchTitle);
    assert(gate.Complete(PendingOperation::LaunchTitle));
    assert(!gate.busy());
    assert(!gate.Complete(PendingOperation::LaunchTitle));

    // All supported operations can be acquired and completed independently.
    const PendingOperation operations[] = {
        PendingOperation::LaunchTitle,
        PendingOperation::LaunchHomebrew,
        PendingOperation::ReturnToGame,
        PendingOperation::CloseApp,
        PendingOperation::PowerAction,
    };
    for (const auto operation : operations) {
        assert(gate.TryBegin(operation));
        assert(gate.current() == operation);
        assert(gate.Complete(operation));
        assert(!gate.busy());
    }

    // Cancellation is used when HOME/sleep/watchdog tears down the menu.
    assert(gate.TryBegin(PendingOperation::LaunchHomebrew));
    gate.Reset();
    assert(!gate.busy());
    assert(gate.current() == PendingOperation::None);
    assert(!gate.Complete(PendingOperation::LaunchHomebrew));

    return 0;
}
