#include <desknx/TouchGesture.hpp>

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using desknx::touch::HoldToDrag;

    HoldToDrag gesture;
    gesture.begin(100, 200, 1000);
    assert(!gesture.tick(1419));
    assert(gesture.tick(1420));
    assert(gesture.dragging());
    assert(!gesture.tick(1500));
    gesture.finish();
    assert(!gesture.active());

    // Movement before the hold delay is a tap/scroll candidate, never a drag.
    gesture.begin(100, 200, 2000);
    gesture.observeMotion(111, 200, 2050);
    assert(!gesture.cancelled()); // movement threshold is 12 pixels
    gesture.observeMotion(112, 200, 2060);
    assert(gesture.cancelled());
    assert(!gesture.tick(2600));
    gesture.finish();

    // A later motion does not cancel a hold which has already matured.
    gesture.begin(40, 50, 3000);
    assert(gesture.tick(3420));
    gesture.observeMotion(90, 50, 3450);
    assert(gesture.dragging());
    gesture.finish();

    // Unsigned elapsed-time arithmetic remains valid across SDL tick wraparound.
    const std::uint32_t nearWrap = std::numeric_limits<std::uint32_t>::max() - 100;
    gesture.begin(0, 0, nearWrap);
    assert(!gesture.tick(nearWrap + 419));
    assert(gesture.tick(nearWrap + 420));
    gesture.finish();
}
