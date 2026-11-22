#include <desknx/CoalescedLayoutSaveScheduler.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <iostream>

using desknx::persistence::CoalescedLayoutSaveScheduler;
using desknx::persistence::LayoutSaveKind;

int main() {
    CoalescedLayoutSaveScheduler scheduler(2000);
    assert(!scheduler.pending());
    assert(!scheduler.ready(100));

    scheduler.request(100, 450, true);
    assert(scheduler.pending());
    assert(scheduler.kind() == LayoutSaveKind::MarkerOnly);
    assert(!scheduler.ready(549));
    assert(scheduler.ready(550));
    assert(scheduler.take() == LayoutSaveKind::MarkerOnly);
    assert(!scheduler.pending());

    // A burst continually moves the quiet deadline, but not the maximum deadline.
    scheduler.request(1000, 450, false);
    scheduler.request(1300, 450, false);
    scheduler.request(1600, 450, false);
    scheduler.request(1900, 450, false);
    assert(!scheduler.ready(2449));
    assert(scheduler.ready(3000)); // max deferral from first request
    assert(scheduler.take() == LayoutSaveKind::Full);

    // Full layout persistence dominates a marker-only request in the same burst.
    scheduler.request(4000, 400, true);
    scheduler.request(4100, 400, false);
    scheduler.request(4200, 300, true);
    assert(scheduler.kind() == LayoutSaveKind::Full);
    assert(!scheduler.ready(4499));
    assert(scheduler.ready(4500));
    assert(scheduler.take() == LayoutSaveKind::Full);

    // A later marker-only request starts a new, independent batch.
    scheduler.request(5000, 300, true);
    scheduler.defer(5100, 1000);
    assert(!scheduler.ready(6099));
    assert(scheduler.ready(6100));
    assert(scheduler.take() == LayoutSaveKind::MarkerOnly);

    // Unsigned tick wraparound must not invert debounce or maximum deadlines.
    const std::uint32_t nearWrap = std::numeric_limits<std::uint32_t>::max() - 100;
    scheduler.request(nearWrap, 200, false);
    assert(!scheduler.ready(nearWrap + 199));
    assert(scheduler.ready(nearWrap + 200));

    scheduler.request(7000, 500, true);
    scheduler.forceDue(7025);
    assert(scheduler.ready(7025));
    assert(scheduler.take() == LayoutSaveKind::MarkerOnly);
    assert(scheduler.take() == LayoutSaveKind::None);

    std::cout << "Coalesced layout save scheduler tests passed\n";
    return 0;
}
