#include <desknx/ShutdownPersistence.hpp>

#include <cassert>
#include <cstdio>

using desknx::persistence::shouldSaveLayoutOnShutdown;

int main() {
    // A normal session with a loaded layout always writes the final snapshot.
    assert(shouldSaveLayoutOnShutdown(true, false, true, true, false, false));

    // First-run placeholder desks must not be saved before the catalogue scan:
    // otherwise the generated Games/Homebrew desktops would never be built.
    assert(!shouldSaveLayoutOnShutdown(true, false, true, false, true, false));

    // But first-run changes made by the user must survive exit even while the
    // catalogue is still scanning and layoutLoaded has not been set yet.
    assert(shouldSaveLayoutOnShutdown(true, false, true, false, true, true));

    // An explicit user layout mutation is sufficient even if a migration or
    // startup recovery path has not yet marked the layout as loaded.
    assert(shouldSaveLayoutOnShutdown(true, false, true, false, false, true));

    // Never overwrite the user's only recovery copy or attempt SD writes after
    // the card has been detached.
    assert(!shouldSaveLayoutOnShutdown(true, true, true, true, false, true));
    assert(!shouldSaveLayoutOnShutdown(false, false, true, true, false, true));
    assert(!shouldSaveLayoutOnShutdown(true, false, false, true, false, true));

    std::puts("shutdown_persistence_test: ok");
    return 0;
}
