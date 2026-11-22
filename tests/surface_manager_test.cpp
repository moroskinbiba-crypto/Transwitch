#include <desknx/SurfaceManager.hpp>
#include <cassert>
#include <cstdint>
#include <iostream>

using desknx::ui::SurfaceId;
using desknx::ui::SurfaceManager;

int main() {
    SurfaceManager surfaces;
    assert(!surfaces.backdropActive());
    assert(!surfaces.hasBlockingSurface());
    assert(surfaces.top() == nullptr);

    surfaces.beginFrame();
    assert(surfaces.push(SurfaceId::EmptyDesktop, false, false, 0));
    assert(surfaces.contains(SurfaceId::EmptyDesktop));
    assert(!surfaces.backdropActive());
    assert(!surfaces.hasBlockingSurface());
    assert(surfaces.top() == nullptr);
    assert(surfaces.dimAlpha() == 0);

    // The last registered modal owns input; a lower surface cannot steal it.
    assert(surfaces.push(SurfaceId::Library, true, true, 120));
    assert(surfaces.push(SurfaceId::QuickSettings, true, true, 190));
    assert(surfaces.isTop(SurfaceId::QuickSettings));
    assert(!surfaces.isTop(SurfaceId::Library));
    assert(surfaces.hasBlockingSurface());
    assert(surfaces.dimAlpha() == 190);

    // Updating an existing id does not duplicate it or change the z-order.
    assert(surfaces.push(SurfaceId::Library, true, true, 80));
    assert(surfaces.size() == 3);
    assert(surfaces.isTop(SurfaceId::QuickSettings));
    assert(surfaces.dimAlpha() == 190);

    // Updating an existing non-input surface does not duplicate it or steal focus.
    assert(surfaces.push(SurfaceId::EmptyDesktop, false, false, 0));
    assert(surfaces.size() == 3);
    assert(surfaces.isTop(SurfaceId::QuickSettings));

    surfaces.beginFrame();
    assert(surfaces.size() == 0);
    assert(!surfaces.backdropActive());
    assert(!surfaces.hasBlockingSurface());
    assert(surfaces.top() == nullptr);

    // Capacity is fixed and overflow is reported without corrupting the stack.
    for (std::size_t i = 0; i < SurfaceManager::Capacity; ++i) {
        const auto id = static_cast<SurfaceId>(i);
        assert(surfaces.push(id, true, false));
    }
    assert(surfaces.size() == SurfaceManager::Capacity);
    // A fresh id beyond the reserved surface range must fail at capacity.
    assert(!surfaces.push(static_cast<SurfaceId>(SurfaceManager::Capacity), true, true));
    assert(surfaces.size() == SurfaceManager::Capacity);

    std::cout << "Surface manager tests passed\n";
    return 0;
}
