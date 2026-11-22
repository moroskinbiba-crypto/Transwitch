#pragma once
#include "Model.hpp"
#include <algorithm>
namespace desknx::artwork {
constexpr int LibraryIconPriority = 96;
constexpr int WorkspacePrefetchPriorityBase = 144;
constexpr int VisibleTilePriority = 184;
constexpr int FocusedTilePriority = 248;
inline int workspacePrefetchPriority(const model::Tile& tile,int focusX,int focusY) noexcept {
    const int right=tile.x+std::max(1,tile.w)-1;
    const int bottom=tile.y+std::max(1,tile.h)-1;
    const int dx=focusX<tile.x?tile.x-focusX:focusX>right?focusX-right:0;
    const int dy=focusY<tile.y?tile.y-focusY:focusY>bottom?focusY-bottom:0;
    return std::max(24,WorkspacePrefetchPriorityBase-(dx+dy)*8);
}
inline int tileRenderPriority(bool focused) noexcept {
    return focused?FocusedTilePriority:VisibleTilePriority;
}
} // namespace desknx::artwork
