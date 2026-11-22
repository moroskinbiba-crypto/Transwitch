#pragma once

#include <cstdint>

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

struct SettingsDialogLayout {
    ModalRect surface;
    ModalRect closeButton;
    ItemListLayout rows;
};

inline constexpr SettingsDialogLayout LauncherSettingsDialogControls{
    {300,104,680,520},
    {842,132,92,32},
    {{340,214,600,334},12,26,28}
};

inline constexpr SettingsDialogLayout WorkspaceSettingsDialogControls{
    {300,104,680,520},
    {842,132,92,32},
    {{340,216,600,336},7,40,48}
};

struct RecentWindowLayout {
    static constexpr int VisibleEntries=10;
    ModalRect surface;
    ModalRect backButton;
    ModalRect launchButton;
    ItemListLayout entries;
};

inline constexpr RecentWindowLayout RecentWindowControls{
    {230,78,820,544},
    {838,108,78,30},
    {924,108,88,30},
    {{278,190,724,390},RecentWindowLayout::VisibleEntries,35,39}
};

struct UserPickerDialogLayout {
    static constexpr int MaxUsers=8;
    ModalRect surface;
    ModalRect closeButton;
    ItemListLayout users;
};

inline constexpr UserPickerDialogLayout UserPickerDialogControls{
    {330,116,620,508},
    {844,136,76,34},
    {{370,214,540,368},UserPickerDialogLayout::MaxUsers,40,46}
};

struct PowerDialogLayout {
    ModalRect surface;
    ModalRect closeButton;
    ItemListLayout actions;
    ModalRect executeButton;
};

inline constexpr PowerDialogLayout PowerDialogControls{
    {410,210,460,300},
    {780,238,52,30},
    {{442,289,396,126},3,38,44},
    {590,432,248,42}
};

struct MediaPickerWindowLayout {
    ModalRect surface;
    ModalRect directoryButton;
    ModalRect closeButton;
    ItemListLayout entries;
};

inline constexpr MediaPickerWindowLayout MediaPickerWindowControls{
    {220,58,840,604},
    {782,86,132,34},
    {930,86,92,34},
    {{260,166,760,408},12,30,34}
};

struct SystemToolsWindowLayout {
    ModalRect surface;
    ModalRect closeButton;
    ModalRect cards[6];

    constexpr ModalRect cardRect(int index) const noexcept {
        return index>=0&&index<6?cards[index]:ModalRect{};
    }

    constexpr int itemAt(int x,int y) const noexcept {
        for(int index=0;index<6;++index)
            if(cards[index].contains(x,y))return index;
        return -1;
    }
};

inline constexpr SystemToolsWindowLayout SystemToolsWindowControls{
    {110,104,1060,512},
    {1058,136,82,34},
    {
        {156,218,288,136},
        {496,218,288,136},
        {836,218,288,136},
        {156,382,288,136},
        {496,382,288,136},
        {836,382,288,136}
    }
};

// App Library rows are 35 px high on a 40 px cadence. The 5 px gutters
// are deliberately excluded from touch hit-testing.
inline constexpr ItemListLayout AppLibraryListRows{
    {137,180,730,400},10,35,40
};

// Shared main-screen chrome: visual rectangles and pointer hit areas are
// separate where the existing controls intentionally have forgiving margins.
struct LauncherToolbarLayout {
    static constexpr int ButtonCount=4, ButtonStartX=64, ButtonWidth=82, ButtonStep=90;
    ModalRect surface{56,16,1168,64}, touchBand{0,16,1280,67};
    ModalRect profileSurface{470,24,392,48}, profileHitBounds{470,16,393,67};
    ModalRect nextendoSurface{870,24,142,48}, clockBatterySurface{1022,24,180,48};
    constexpr ModalRect buttonRect(int i) const noexcept {
        return i>=0&&i<ButtonCount?ModalRect{ButtonStartX+i*ButtonStep,24,ButtonWidth,48}:ModalRect{};
    }
    constexpr ModalRect buttonHitBounds(int i) const noexcept {
        return i>=0&&i<ButtonCount?ModalRect{ButtonStartX+i*ButtonStep,16,ButtonWidth,67}:ModalRect{};
    }
    constexpr int buttonAt(int x,int y) const noexcept {
        for(int i=0;i<ButtonCount;++i)if(buttonHitBounds(i).contains(x,y))return i;
        return -1;
    }
};
inline constexpr LauncherToolbarLayout LauncherToolbarControls{};

enum class WorkspaceDockAction : std::uint8_t { None, AddWorkspace, OpenMenu, OpenPicker, ToggleEdit };

struct WorkspaceDockLayout {
    static constexpr int VisibleTabCount=5, TabStartX=50, TabWidth=132, TabStep=142;
    ModalRect surface{28,624,1224,82}, interactionBounds{28,638,1224,57};
    ModalRect tabStripBounds{50,638,VisibleTabCount*TabStep,57};
    ModalRect dragActivationBounds{50,638,1230,57};
    ModalRect addButton{770,641,52,48}, addHitBounds{770,638,52,57};
    ModalRect menuButton{832,641,52,48}, menuHitBounds{832,638,52,57};
    ModalRect pageIndicator{900,641,150,48}, pageIndicatorHitBounds{900,638,150,57};
    ModalRect editButton{1096,638,128,54}, editHitBounds{1096,638,128,57};
    constexpr ModalRect tabRect(int slot) const noexcept {
        return slot>=0&&slot<VisibleTabCount?ModalRect{TabStartX+slot*TabStep,641,TabWidth,48}:ModalRect{};
    }
    constexpr ModalRect tabHitBounds(int slot) const noexcept {
        return slot>=0&&slot<VisibleTabCount?ModalRect{TabStartX+slot*TabStep,638,TabWidth,57}:ModalRect{};
    }
    constexpr int workspaceAt(int x,int y,int first,int count) const noexcept {
        for(int slot=0;slot<VisibleTabCount;++slot)if(tabHitBounds(slot).contains(x,y)) {
            const int target=first+slot;
            return target>=0&&target<count?target:-1;
        }
        return -1;
    }
    constexpr WorkspaceDockAction actionAt(int x,int y) const noexcept {
        if(addHitBounds.contains(x,y))return WorkspaceDockAction::AddWorkspace;
        if(menuHitBounds.contains(x,y))return WorkspaceDockAction::OpenMenu;
        if(pageIndicatorHitBounds.contains(x,y))return WorkspaceDockAction::OpenPicker;
        if(editHitBounds.contains(x,y))return WorkspaceDockAction::ToggleEdit;
        return WorkspaceDockAction::None;
    }
};
inline constexpr WorkspaceDockLayout WorkspaceDockControls{};

enum class WorkspaceFooterAction : std::uint8_t {
    None,
    DecreaseTileSize,
    IncreaseTileSize,
    RemoveTile
};

struct WorkspaceFooterHintLayout {
    ModalRect keyBadge;
    ModalRect actionLabel;
};

// The footer is deliberately a non-interactive status/hint strip except for
// the three explicit tile actions shown while editing a selected tile.
// Button rectangles are both the painted bounds and the touch hit targets.
struct WorkspaceFooterLayout {
    static constexpr int HintCount=4;
    ModalRect surface{28,558,1224,56};
    ModalRect statusIndicator{48,576,7,7};
    ModalRect statusLabel{68,575,552,18};
    ModalRect tileSizeDecreaseButton{650,571,165,28};
    ModalRect tileSizeIncreaseButton{826,571,165,28};
    ModalRect removeTileButton{1002,571,228,28};

    constexpr WorkspaceFooterHintLayout hint(int index) const noexcept {
        if(index<0||index>=HintCount)return {};
        const int x=642+index*148;
        return {{x,571,48,28},{x+56,581,88,16}};
    }

    constexpr WorkspaceFooterAction actionAt(int x,int y) const noexcept {
        if(tileSizeDecreaseButton.contains(x,y))
            return WorkspaceFooterAction::DecreaseTileSize;
        if(tileSizeIncreaseButton.contains(x,y))
            return WorkspaceFooterAction::IncreaseTileSize;
        if(removeTileButton.contains(x,y))
            return WorkspaceFooterAction::RemoveTile;
        return WorkspaceFooterAction::None;
    }
};
inline constexpr WorkspaceFooterLayout WorkspaceFooterControls{};


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
