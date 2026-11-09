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

    // A horizontal swipe changes one desktop; vertical movement and short
    // taps never trigger page navigation, and one gesture cannot skip pages.
    desknx::touch::HorizontalPageSwipe swipe;
    swipe.begin(200, 300);
    assert(swipe.observe(245, 304) == 0);
    assert(swipe.observe(120, 365) == 0); // not sufficiently horizontal
    assert(swipe.observe(120, 300) == 1); // left = next desktop
    assert(swipe.observe(20, 300) == 0);  // one page per gesture
    swipe.finish();
    swipe.begin(200, 300);
    assert(swipe.observe(280, 300) == -1); // right = previous desktop
    swipe.finish();

    // Unsigned elapsed-time arithmetic remains valid across SDL tick wraparound.
    const std::uint32_t nearWrap = std::numeric_limits<std::uint32_t>::max() - 100;
    gesture.begin(0, 0, nearWrap);
    assert(!gesture.tick(nearWrap + 419));
    assert(gesture.tick(nearWrap + 420));
    gesture.finish();
}
