#include "../include/desknx/ConfirmationDialogLayout.hpp"

#include <cassert>

int main() {
    using desknx::ui::WorkspaceDeleteConfirmationLayout;
    const auto& layout=WorkspaceDeleteConfirmationLayout;

    assert(layout.primaryButton.inside(layout.surface));
    assert(layout.secondaryButton.inside(layout.surface));
    assert(layout.footer.inside(layout.surface));

    assert(layout.primaryButton.contains(405,382));
    assert(layout.primaryButton.contains(614,429));
    assert(!layout.primaryButton.contains(615,382));
    assert(!layout.primaryButton.contains(405,430));

    assert(layout.secondaryButton.contains(635,382));
    assert(layout.secondaryButton.contains(874,429));
    assert(!layout.secondaryButton.contains(875,382));
    assert(!layout.secondaryButton.contains(635,430));

    // The gap between destructive and cancel actions is not a hit target.
    assert(!layout.primaryButton.contains(620,400));
    assert(!layout.secondaryButton.contains(620,400));

    // Touch rectangles must remain disjoint and enclosed by the surface.
    const bool overlap =
        layout.primaryButton.x < layout.secondaryButton.x + layout.secondaryButton.width &&
        layout.primaryButton.x + layout.primaryButton.width > layout.secondaryButton.x &&
        layout.primaryButton.y < layout.secondaryButton.y + layout.secondaryButton.height &&
        layout.primaryButton.y + layout.primaryButton.height > layout.secondaryButton.y;
    assert(!overlap);
    return 0;
}
