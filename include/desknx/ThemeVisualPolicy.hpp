#pragma once

namespace desknx::ui {

struct ThemeVisualPolicy {
    bool flatSurfaces{false};
    bool squareButtons{false};
    bool persistentWorkspaceGrid{false};
    bool accentRails{false};
    int maximumButtonRadius{16};
    int maximumSurfaceRadius{16};
};

// Theme 0 is the original 0.1 glass system. Theme 1 is SOCOMA's independent
// modular system. Keep this policy explicit so a palette change cannot
// accidentally masquerade as a complete theme implementation.
constexpr ThemeVisualPolicy ThemePolicyForIndex(int index) noexcept {
    if (index == 1)
        return {true, true, true, true, 4, 0};
    return {false, false, false, false, 16, 16};
}

} // namespace desknx::ui
