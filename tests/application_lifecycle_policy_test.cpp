#include <desknx/ApplicationLifecyclePolicy.hpp>

#include <cassert>

using desknx::runtime::ShouldReleaseApplicationHolder;

int main() {
    // With no pm:shell, AM IsCompleted is still an authoritative completion
    // barrier. A dead game must not remain permanently registered.
    assert(ShouldReleaseApplicationHolder(false, false, true, true));

    // A live/not-completed accessor is never released.
    assert(!ShouldReleaseApplicationHolder(false, false, true, false));

    // A failed AM completion query is ambiguous and must preserve the holder.
    assert(!ShouldReleaseApplicationHolder(false, false, false, true));

    // With pm:shell available, require both process disappearance and AM
    // completion; either signal alone is insufficient.
    assert(!ShouldReleaseApplicationHolder(true, false, true, true));
    assert(!ShouldReleaseApplicationHolder(true, true, false, true));
    assert(!ShouldReleaseApplicationHolder(true, true, true, false));
    assert(ShouldReleaseApplicationHolder(true, true, true, true));

    return 0;
}
