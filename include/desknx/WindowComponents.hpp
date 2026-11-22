#pragma once

#include "ConfirmationDialogLayout.hpp"

#include <array>
#include <cstddef>

namespace desknx::ui {

// One source of truth for repeated rows. Hit testing intentionally excludes
// the vertical gaps between rows, matching the pixels that are actually drawn.
struct ItemListLayout {
    ModalRect bounds;
    int itemCount{0};
    int itemHeight{0};
    int itemStride{0};

    constexpr ModalRect itemRect(int index) const noexcept {
        if(index<0||index>=itemCount||itemHeight<=0||itemStride<itemHeight)
            return {};
        return {bounds.x,bounds.y+index*itemStride,bounds.width,itemHeight};
    }

    constexpr int itemAt(int px,int py) const noexcept {
        if(itemCount<=0||itemHeight<=0||itemStride<itemHeight||
           !bounds.contains(px,py))return -1;
        const int offsetY=py-bounds.y;
        const int index=offsetY/itemStride;
        if(index<0||index>=itemCount||offsetY%itemStride>=itemHeight)
            return -1;
        return index;
    }
};

struct DialogLayout {
    ModalRect surface;
    ModalRect title;
    ModalRect description;
    ItemListLayout items;
    ModalRect footer;
    ModalRect closeButton;
};

struct SidePanelLayout {
    ModalRect surface;
    ModalRect header;
    ModalRect content;
    ModalRect footer;
    ModalRect primaryAction;
};

struct ContextMenuLayout {
    ModalRect surface;
    ModalRect title;
    ItemListLayout items;
    ModalRect footer;
};

inline constexpr DialogLayout WorkspacePickerDialogLayout{
    {320,72,640,580},
    {360,100,560,38},
    {360,136,560,20},
    {{360,168,560,384},12,29,32},
    {360,590,560,38},
    {744,590,176,38}
};

inline constexpr SidePanelLayout AppLibraryDetailsPanelLayout{
    {890,180,252,410},
    {890,180,252,114},
    {890,294,252,226},
    {890,520,252,70},
    {902,532,228,42}
};

inline constexpr ContextMenuLayout WorkspaceOptionsContextMenuLayout{
    {340,176,600,370},
    {390,194,500,36},
    {{390,238,500,258},6,38,44},
    {390,518,500,18}
};

// Twelve settings rows fit between the heading and status footer. The 2 px
// gutters remain non-interactive and are shared by render and touch dispatch.
inline constexpr ItemListLayout LauncherSettingsOptionsLayout{
    {340,214,600,334},12,26,28
};

struct AppLibraryToolbarLayout {
    static constexpr int ButtonCount=7;
    std::array<ModalRect,ButtonCount> buttons{};

    constexpr ModalRect buttonRect(int index) const noexcept {
        return index>=0&&index<ButtonCount
            ?buttons[static_cast<std::size_t>(index)]:ModalRect{};
    }

    constexpr int itemAt(int x,int y) const noexcept {
        for(int index=0;index<ButtonCount;++index)
            if(buttons[static_cast<std::size_t>(index)].contains(x,y))
                return index;
        return -1;
    }
};

inline constexpr AppLibraryToolbarLayout AppLibraryToolbarControls{{{
    {138,122,96,30},  // Search
    {242,122,82,30},  // Sort
    {332,122,112,30}, // Filter
    {452,122,82,30},  // Clear search
    {542,122,126,30}, // Rescan
    {676,122,72,30},  // Close
    {756,122,72,30}   // Add
}}};

} // namespace desknx::ui
