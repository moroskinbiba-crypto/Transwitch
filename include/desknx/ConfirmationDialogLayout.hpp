#pragma once

namespace desknx::ui {

struct ModalRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    constexpr bool contains(int px,int py) const noexcept {
        return width>0 && height>0 &&
            px>=x && py>=y && px<x+width && py<y+height;
    }

    constexpr bool inside(const ModalRect& outer) const noexcept {
        return x>=outer.x && y>=outer.y &&
            x+width<=outer.x+outer.width &&
            y+height<=outer.y+outer.height;
    }
};

struct ConfirmationDialogLayout {
    ModalRect surface;
    ModalRect primaryButton;
    ModalRect secondaryButton;
    ModalRect footer;
    int titleX{0},titleY{0};
    int subtitleX{0},subtitleY{0};
    int descriptionX{0},descriptionY{0};
    int primaryLabelX{0},primaryLabelY{0};
    int secondaryLabelX{0},secondaryLabelY{0};
};

inline constexpr ConfirmationDialogLayout WorkspaceDeleteConfirmationLayout{
    {360,242,560,240},
    {405,382,210,48},
    {635,382,240,48},
    {405,444,485,18},
    405,270,
    405,314,
    405,346,
    466,399,
    706,399
};

} // namespace desknx::ui
