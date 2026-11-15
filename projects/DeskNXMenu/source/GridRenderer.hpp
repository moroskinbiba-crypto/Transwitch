#pragma once

#include <desknx/Model.hpp>
#include <desknx/TouchGesture.hpp>
#include <cstdint>
#include <string>

struct SDL_Renderer;

namespace desknx::menu::renderer {

// Tile rendering constants for 1280×720 display (6×3 grid)
struct GridLayout {
    static constexpr int WindowWidth  = 1280;
    static constexpr int WindowHeight = 720;
    static constexpr int GridColumns  = 6;
    static constexpr int GridRows     = 3;

    // Each 1×1 tile is ~200×180 px on a 6×3 grid
    static constexpr int TileWidth  = 200;
    static constexpr int TileHeight = 180;
    static constexpr int PaddingX   = 8;
    static constexpr int PaddingY   = 8;
    static constexpr int CornerRadius = 12;

    static int gridToScreenX(int gridX) { return 20 + gridX * (TileWidth  + PaddingX); }
    static int gridToScreenY(int gridY) { return 20 + gridY * (TileHeight + PaddingY); }
    static int gridToScreenW(int gridW) { return gridW * TileWidth  + (gridW - 1) * PaddingX; }
    static int gridToScreenH(int gridH) { return gridH * TileHeight + (gridH - 1) * PaddingY; }
};

// Color palette for tiles
struct Colors {
    static constexpr uint32_t TileBackground = 0x2C2C2CFF;
    static constexpr uint32_t TileBorder     = 0x404040FF;
    static constexpr uint32_t TileHover      = 0x383838FF;
    static constexpr uint32_t TileActive     = 0x4A4A4AFF;
    // Focus highlight: bright yellow-white border
    static constexpr uint32_t TileFocusBorder = 0xFFE066FF;
    static constexpr uint32_t TileFocusBg     = 0x3A3820FF;
    // Drag preview tints
    static constexpr uint32_t TileDragValid   = 0x1A6B1AFF; // green background
    static constexpr uint32_t TileDragInvalid = 0x6B1A1AFF; // red background
    static constexpr uint32_t TileDragBorderV = 0x44FF44FF; // green border
    static constexpr uint32_t TileDragBorderI = 0xFF4444FF; // red border
    static constexpr uint32_t TileGhost       = 0x2C2C2C66; // 40% opacity ghost
    static constexpr uint32_t TextPrimary    = 0xFFFFFFFF;
    static constexpr uint32_t TextSecondary  = 0xB0B0B0FF;
    static constexpr uint32_t WidgetClock    = 0x1E90FFFF;
    static constexpr uint32_t WidgetBattery  = 0x32CD32FF;
    static constexpr uint32_t WidgetRecent   = 0xFF8C00FF;
    static constexpr uint32_t BackgroundDark = 0x1A1A1AFF;
};

class GridRenderer {
public:
    virtual ~GridRenderer() = default;

    /// Render the entire grid to screen.
    virtual void render(const model::Workspace& workspace) = 0;

    /// Set the tile currently under the cursor (for hover effects).
    virtual void setHoveredTile(int tileIndex) = 0;

    /// Set the tile currently being dragged (screen-coordinate path, legacy).
    virtual void setDraggedTile(int tileIndex, int screenX, int screenY) = 0;

    // ── Drag-preview (Phase 1.2) ──────────────────────────────────────────────

    /// Enter drag-preview mode: ghost at (origX, origY), preview at (destX, destY).
    virtual void beginDragPreview(int tileIndex,
                                  int origX, int origY,
                                  int destX, int destY,
                                  bool destValid) = 0;

    /// Update the preview destination while dragging.
    virtual void updateDragPreview(int destX, int destY, bool destValid) = 0;

    /// End drag preview (commit or cancel).
    virtual void endDragPreview() = 0;

    /// True when drag-preview is active.
    virtual bool isDragPreviewActive() const = 0;

    // ── Resize-preview (Phase 1.3) ────────────────────────────────────────────

    /// Enter resize-preview mode: show tile with new size preview.
    virtual void beginResizePreview(int tileIndex,
                                   int w, int h,
                                   bool valid) = 0;

    /// Update the resize preview with new target size.
    virtual void updateResizePreview(int w, int h, bool valid) = 0;

    /// End resize preview.
    virtual void endResizePreview() = 0;

    /// True when resize-preview is active.
    virtual bool isResizePreviewActive() const = 0;

    /// Convert screen coordinates to grid position. Returns 0..Columns-1 / 0..Rows-1.
    virtual int screenToGridX(int screenX) const = 0;
    virtual int screenToGridY(int screenY) const = 0;

    /// Get the screen bounding box of a tile.
    virtual void getTileBounds(int gridX, int gridY, int gridW, int gridH,
                               int& outX, int& outY, int& outW, int& outH) const = 0;

    // ── Focus / D-pad navigation ──────────────────────────────────────────────

    /// Move focus to an explicit grid cell.  Clamps to [0,Columns) × [0,Rows).
    virtual void setFocus(int x, int y) = 0;

    /// Move focus one step in the given direction, skipping empty cells.
    /// Wraps around the grid.  Has no effect if no tiles exist.
    virtual void moveFocus(touch::Direction dir) = 0;

    /// Return current focused cell coordinates.
    virtual int focusX() const = 0;
    virtual int focusY() const = 0;

    /// Return index of focused tile, or -1 if the focused cell is empty.
    virtual int focusedTileIndex(const model::Workspace& workspace) const = 0;

    /// Place initial focus on the first non-empty cell (top-left scan order).
    /// Call once after workspaces are populated.
    virtual void initFocus(const model::Workspace& workspace) = 0;

    /// Update the cached workspace pointer used by moveFocus().
    /// Call every frame before moveFocus() / render().
    virtual void setWorkspace(const model::Workspace& workspace) = 0;
};

// Factory: create platform-specific renderer (SDL2 on Switch / host).
GridRenderer* createGridRenderer(SDL_Renderer* renderer);

} // namespace desknx::menu::renderer
