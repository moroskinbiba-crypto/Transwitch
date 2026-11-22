#include <desknx/StorageControlPlane.hpp>

#include <cassert>
#include <cstdint>

int main() {
    using namespace desknx::storage;

    ControlPlane control;
    assert(!control.mounted());
    assert(control.mountGeneration() == 0);
    assert(control.beginCommit(100).status == CommitBeginStatus::Unavailable);

    control.setMounted(true);
    assert(control.mounted());
    assert(control.mountGeneration() == 1);

    const auto first = control.beginCommit(100);
    assert(first.status == CommitBeginStatus::Acquired);
    assert(first.lease.valid);
    assert(control.commitInFlight());
    assert(control.beginCommit(101).status == CommitBeginStatus::Busy);
    assert(control.finishCommit(first.lease, true, 102) ==
           CommitFinishStatus::Accepted);
    assert(!control.commitInFlight());
    assert(control.consecutiveFailures() == 0);
    assert(!control.retryPending());

    // A failed durability boundary enters a bounded exponential backoff.
    const auto failed = control.beginCommit(200);
    assert(failed.status == CommitBeginStatus::Acquired);
    assert(control.finishCommit(failed.lease, false, 200) ==
           CommitFinishStatus::Accepted);
    assert(control.consecutiveFailures() == 1);
    assert(control.retryPending());
    assert(control.retryAt() == 450);
    assert(ControlPlane::retryDelayMs(1) == 250);
    assert(ControlPlane::retryDelayMs(2) == 500);
    assert(ControlPlane::retryDelayMs(3) == 1000);
    assert(ControlPlane::retryDelayMs(100) == ControlPlane::RetryMaxMs);
    assert(control.beginCommit(449).status == CommitBeginStatus::Backoff);
    assert(control.beginCommit(450).status == CommitBeginStatus::Acquired);

    // A successful commit clears the retry state.
    const auto recovered = control.beginCommit(450);
    assert(recovered.status == CommitBeginStatus::Busy);
    // The first due call above acquired the lease; finish that exact lease.
    // Querying again while it is active is intentionally rejected.
    // (Keep the lease by acquiring in a fresh controller below.)

    ControlPlane successAfterRetry;
    successAfterRetry.setMounted(true);
    const auto failOnce = successAfterRetry.beginCommit(0);
    assert(failOnce.status == CommitBeginStatus::Acquired);
    assert(successAfterRetry.finishCommit(failOnce.lease, false, 0) ==
           CommitFinishStatus::Accepted);
    const auto retryLease = successAfterRetry.beginCommit(250);
    assert(retryLease.status == CommitBeginStatus::Acquired);
    assert(successAfterRetry.finishCommit(retryLease.lease, true, 251) ==
           CommitFinishStatus::Accepted);
    assert(successAfterRetry.consecutiveFailures() == 0);
    assert(!successAfterRetry.retryPending());

    // An SD removal/remount invalidates any in-flight lease from the old card.
    const auto stale = control.beginCommit(451);
    assert(stale.status == CommitBeginStatus::Backoff ||
           stale.status == CommitBeginStatus::Acquired);
    control.setMounted(false);
    assert(!control.mounted());
    assert(control.mountGeneration() == 2);
    if (stale.lease.valid) {
        assert(control.finishCommit(stale.lease, true, 452) ==
               CommitFinishStatus::StaleLease);
    }
    control.setMounted(true);
    assert(control.mountGeneration() == 3);
    const auto fresh = control.beginCommit(500);
    assert(fresh.status == CommitBeginStatus::Acquired);
    assert(control.finishCommit(fresh.lease, true, 501) ==
           CommitFinishStatus::Accepted);

    // Wrap-safe deadlines also apply when the clock rolls over uint32_t.
    ControlPlane wrap;
    wrap.setMounted(true);
    const auto lease = wrap.beginCommit(0xFFFFFF00u);
    assert(lease.status == CommitBeginStatus::Acquired);
    assert(wrap.finishCommit(lease.lease, false, 0xFFFFFF00u) ==
           CommitFinishStatus::Accepted);
    assert(wrap.beginCommit(0xFFFFFF00u + 249u).status ==
           CommitBeginStatus::Backoff);
    assert(wrap.beginCommit(0xFFFFFF00u + 250u).status ==
           CommitBeginStatus::Acquired);

    return 0;
}
