// Copyright contributors to the DeskNX project.
// SPDX-License-Identifier: MIT

#include "GridRenderer.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>

namespace desknx::menu::renderer {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void setColor(SDL_Renderer* r, uint32_t rgba) {
    SDL_SetRenderDrawColor(r,
        (rgba >> 24) & 0xFF,
        (rgba >> 16) & 0xFF,
        (rgba >>  8) & 0xFF,
         rgba        & 0xFF);
}

// Draw a hollow rectangle with a given border thickness.
static void drawBorderRect(SDL_Renderer* r, SDL_Rect rect, int thickness) {
    for (int i = 0; i < thickness; ++i) {
        SDL_Rect inner{rect.x + i, rect.y + i,
                       rect.w - 2 * i, rect.h - 2 * i};
        SDL_RenderDrawRect(r, &inner);
    }
}

// ---------------------------------------------------------------------------
// Focus navigation logic (pure, no SDL — also used by host tests)
// ---------------------------------------------------------------------------

// Returns the index (0-based) of the tile occupying grid cell (cx, cy), or -1.
static int tileAt(const model::Workspace& ws, int cx, int cy) {
    for (int i = 0; i < static_cast<int>(ws.tiles.size()); ++i) {
        const auto& t = ws.tiles[i];
        if (cx >= t.x && cx < t.x + t.w &&
            cy >= t.y && cy < t.y + t.h) {
            return i;
        }
    }
    return -1;
}

// Return true if any tile occupies (cx, cy).
static bool cellOccupied(const model::Workspace& ws, int cx, int cy) {
    return tileAt(ws, cx, cy) >= 0;
}

// Scan the grid in top-left order and return the first occupied cell, or
// {-1,-1} when the workspace is empty.
static std::pair<int,int> firstOccupied(const model::Workspace& ws) {
    for (int row = 0; row < model::Rows; ++row)
        for (int col = 0; col < model::Columns; ++col)
            if (cellOccupied(ws, col, row))
                return {col, row};
    return {-1, -1};
}

// Linear index for row-major (reading) order.
static int linearRowMajor(int col, int row) {
    return row * model::Columns + col;
}
// Linear index for column-major order.
static int linearColMajor(int col, int row) {
    return col * model::Rows + row;
}

// Return (col, row) from a row-major linear index.
static std::pair<int,int> fromRowMajor(int idx) {
    return {idx % model::Columns, idx / model::Columns};
}
// Return (col, row) from a column-major linear index.
static std::pair<int,int> fromColMajor(int idx) {
    return {idx / model::Rows, idx % model::Rows};
}

// Move (cx,cy) one step in direction dir, wrapping, and try up to
// Columns*Rows steps.  Returns the first occupied cell found, or the
// original position when no occupied cell exists.
//
// Right/Left scan in reading order (row-major, wrapping rows).
// Down/Up   scan in column order (column-major, wrapping columns).
static std::pair<int,int> nextOccupied(const model::Workspace& ws,
                                        int cx, int cy,
                                        touch::Direction dir) {
    if (ws.tiles.empty()) return {cx, cy};

    const int total = model::Columns * model::Rows;
    const int curRowMajor = linearRowMajor(cx, cy);
    const int curColMajor = linearColMajor(cx, cy);

    for (int step = 1; step <= total; ++step) {
        int idx;
        std::pair<int,int> cell;
        switch (dir) {
            case touch::Direction::Right:
                idx = (curRowMajor + step) % total;
                cell = fromRowMajor(idx);
                break;
            case touch::Direction::Left:
                idx = (curRowMajor - step + total) % total;
                cell = fromRowMajor(idx);
                break;
            case touch::Direction::Down:
                idx = (curColMajor + step) % total;
                cell = fromColMajor(idx);
                break;
            case touch::Direction::Up:
                idx = (curColMajor - step + total) % total;
                cell = fromColMajor(idx);
                break;
        }
        if (cellOccupied(ws, cell.first, cell.second))
            return cell;
    }
    return {cx, cy};
}

// ---------------------------------------------------------------------------
// SDL2 implementation
// ---------------------------------------------------------------------------

class GridRendererSDL2 : public GridRenderer {
public:
    explicit GridRendererSDL2(SDL_Renderer* renderer)
        : m_renderer(renderer) {}

    // ── GridRenderer interface ───────────────────────────────────────────────

    void render(const model::Workspace& workspace) override {
        setColor(m_renderer, Colors::BackgroundDark);
        SDL_RenderClear(m_renderer);

        for (int i = 0; i < static_cast<int>(workspace.tiles.size()); ++i) {
            const auto& tile = workspace.tiles[i];
            const bool hovered  = (i == m_hoveredTile);
            const bool dragging = (i == m_draggedTile);
            const bool focused  = cellOccupied(workspace, m_focusX, m_focusY)
                                  && (tileAt(workspace, m_focusX, m_focusY) == i);

            // Skip rendering the dragged tile at its original position;
            // we'll draw a ghost there and a preview at the destination.
            if (m_dragPreviewActive && i == m_dragPreviewTile) {
                renderDragGhost(tile);
                renderDragPreview(workspace, tile);
                continue;
            }

            renderTile(tile, hovered, dragging, focused);
        }

        // Draw empty-cell grid dots for orientation (subtle)
        renderGridDots(workspace);

        SDL_RenderPresent(m_renderer);
    }

    void setHoveredTile(int idx) override { m_hoveredTile = idx; }

    void setDraggedTile(int idx, int sx, int sy) override {
        m_draggedTile  = idx;
        m_dragScreenX  = sx;
        m_dragScreenY  = sy;
    }

    // ── Drag preview (Phase 1.2) ──────────────────────────────────────────────

    void beginDragPreview(int tileIndex,
                          int origX, int origY,
                          int destX, int destY,
                          bool destValid) override {
        m_dragPreviewActive = true;
        m_dragPreviewTile   = tileIndex;
        m_dragOrigX = origX;
        m_dragOrigY = origY;
        m_dragDestX = destX;
        m_dragDestY = destY;
        m_dragDestValid = destValid;
    }

    void updateDragPreview(int destX, int destY, bool destValid) override {
        m_dragDestX = destX;
        m_dragDestY = destY;
        m_dragDestValid = destValid;
    }

    void endDragPreview() override {
        m_dragPreviewActive = false;
        m_dragPreviewTile   = -1;
    }

    bool isDragPreviewActive() const override { return m_dragPreviewActive; }

    int screenToGridX(int sx) const override {
        return std::clamp((sx - 20) / (GridLayout::TileWidth  + GridLayout::PaddingX),
                          0, model::Columns - 1);
    }
    int screenToGridY(int sy) const override {
        return std::clamp((sy - 20) / (GridLayout::TileHeight + GridLayout::PaddingY),
                          0, model::Rows - 1);
    }

    void getTileBounds(int gx, int gy, int gw, int gh,
                       int& ox, int& oy, int& ow, int& oh) const override {
        ox = GridLayout::gridToScreenX(gx);
        oy = GridLayout::gridToScreenY(gy);
        ow = GridLayout::gridToScreenW(gw);
        oh = GridLayout::gridToScreenH(gh);
    }

    // ── Focus API ────────────────────────────────────────────────────────────

    void setFocus(int x, int y) override {
        m_focusX = std::clamp(x, 0, model::Columns - 1);
        m_focusY = std::clamp(y, 0, model::Rows    - 1);
    }

    void moveFocus(touch::Direction dir) override {
        // Respect the debounce counter.
        if (m_focusDebounce > 0) { --m_focusDebounce; return; }
        m_focusDebounce = kFocusDebounceFrames;

        // We need the workspace — keep a cached pointer updated each render().
        // If no workspace is cached yet, do nothing.
        if (!m_lastWorkspace) return;

        auto [nx, ny] = nextOccupied(*m_lastWorkspace, m_focusX, m_focusY, dir);
        m_focusX = nx;
        m_focusY = ny;
    }

    int focusX() const override { return m_focusX; }
    int focusY() const override { return m_focusY; }

    int focusedTileIndex(const model::Workspace& ws) const override {
        return tileAt(ws, m_focusX, m_focusY);
    }

    void initFocus(const model::Workspace& ws) override {
        m_lastWorkspace = &ws;
        auto [fx, fy] = firstOccupied(ws);
        if (fx >= 0) { m_focusX = fx; m_focusY = fy; }
        else         { m_focusX = 0;  m_focusY = 0;  }
    }

    void setWorkspace(const model::Workspace& ws) override {
        m_lastWorkspace = &ws;
    }

private:
    // ── rendering helpers ────────────────────────────────────────────────────

    void renderTile(const model::Tile& tile,
                    bool hovered, bool dragging, bool focused) {
        int x = GridLayout::gridToScreenX(tile.x);
        int y = GridLayout::gridToScreenY(tile.y);
        int w = GridLayout::gridToScreenW(tile.w);
        int h = GridLayout::gridToScreenH(tile.h);

        if (dragging) { x = m_dragScreenX - w / 2; y = m_dragScreenY - h / 2; }

        SDL_Rect rect{x, y, w, h};

        // Background
        uint32_t bg = focused  ? Colors::TileFocusBg
                    : hovered  ? Colors::TileHover
                    : dragging ? Colors::TileActive
                               : Colors::TileBackground;
        setColor(m_renderer, bg);
        SDL_RenderFillRect(m_renderer, &rect);

        // Border — 2 px thick for focused, 1 px for others
        if (focused) {
            setColor(m_renderer, Colors::TileFocusBorder);
            drawBorderRect(m_renderer, rect, 2);
        } else {
            setColor(m_renderer, Colors::TileBorder);
            SDL_RenderDrawRect(m_renderer, &rect);
        }
    }

    void renderGridDots(const model::Workspace& ws) {
        // 4-pixel dots at the centre of each cell that has no tile.
        setColor(m_renderer, 0x303030FF);
        for (int row = 0; row < model::Rows; ++row) {
            for (int col = 0; col < model::Columns; ++col) {
                if (cellOccupied(ws, col, row)) continue;
                int cx = GridLayout::gridToScreenX(col) + GridLayout::TileWidth  / 2;
                int cy = GridLayout::gridToScreenY(row) + GridLayout::TileHeight / 2;
                SDL_Rect dot{cx - 2, cy - 2, 4, 4};
                SDL_RenderFillRect(m_renderer, &dot);
            }
        }
    }

    // ── Drag preview rendering ───────────────────────────────────────────────

    /// Draw a 40% opacity ghost at the tile's original position.
    void renderDragGhost(const model::Tile& tile) {
        int x = GridLayout::gridToScreenX(tile.x);
        int y = GridLayout::gridToScreenY(tile.y);
        int w = GridLayout::gridToScreenW(tile.w);
        int h = GridLayout::gridToScreenH(tile.h);
        SDL_Rect rect{x, y, w, h};

        setColor(m_renderer, Colors::TileGhost);
        SDL_RenderFillRect(m_renderer, &rect);
        setColor(m_renderer, 0x50505088);
        drawBorderRect(m_renderer, rect, 1);
    }

    /// Draw the drag preview at the destination cell with green/red tint.
    void renderDragPreview(const model::Workspace& ws, const model::Tile& tile) {
        int x = GridLayout::gridToScreenX(m_dragDestX);
        int y = GridLayout::gridToScreenY(m_dragDestY);
        int w = GridLayout::gridToScreenW(tile.w);
        int h = GridLayout::gridToScreenH(tile.h);
        SDL_Rect rect{x, y, w, h};

        uint32_t bg = m_dragDestValid ? Colors::TileDragValid
                                       : Colors::TileDragInvalid;
        uint32_t bd = m_dragDestValid ? Colors::TileDragBorderV
                                       : Colors::TileDragBorderI;

        setColor(m_renderer, bg);
        SDL_RenderFillRect(m_renderer, &rect);
        setColor(m_renderer, bd);
        drawBorderRect(m_renderer, rect, 3);
    }

    // ── state ────────────────────────────────────────────────────────────────

    SDL_Renderer* m_renderer;

    int m_hoveredTile  = -1;
    int m_draggedTile  = -1;
    int m_dragScreenX  = 0;
    int m_dragScreenY  = 0;

    // Drag preview state
    bool m_dragPreviewActive = false;
    int  m_dragPreviewTile   = -1;
    int  m_dragOrigX = 0, m_dragOrigY = 0;
    int  m_dragDestX = 0, m_dragDestY = 0;
    bool m_dragDestValid = true;

    int m_focusX = 0;
    int m_focusY = 0;

    // We cache the last workspace pointer so moveFocus() can query occupancy
    // without it being passed as a parameter (keeping the existing hot-path
    // render() signature unchanged).
    const model::Workspace* m_lastWorkspace = nullptr;

    // Debounce: ignore repeated D-pad events within N frames (~10 × 16 ms).
    static constexpr int kFocusDebounceFrames = 10;
    int m_focusDebounce = 0;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

GridRenderer* createGridRenderer(SDL_Renderer* renderer) {
    return new GridRendererSDL2(renderer);
}

} // namespace desknx::menu::renderer

{"content":"// Copyright contributors to the DeskNX project.\n// SPDX-License-Identifier: MIT\n\n#include \"GridRenderer.hpp\"\n#include <SDL2/SDL.h>\n#include <algorithm>\n#include <cstdio>\n\nnamespace desknx::menu::renderer {\n\n// ---------------------------------------------------------------------------\n// Helpers\n// ---------------------------------------------------------------------------\n\nstatic void setColor(SDL_Renderer* r, uint32_t rgba) {\n    SDL_SetRenderDrawColor(r,\n        (rgba >> 24) & 0xFF,\n        (rgba >> 16) & 0xFF,\n        (rgba >>  8) & 0xFF,\n         rgba        & 0xFF);\n}\n\n// Draw a hollow rectangle with a given border thickness.\nstatic void drawBorderRect(SDL_Renderer* r, SDL_Rect rect, int thickness) {\n    for (int i = 0; i < thickness; ++i) {\n        SDL_Rect inner{rect.x + i, rect.y + i,\n                       rect.w - 2 * i, rect.h - 2 * i};\n        SDL_RenderDrawRect(r, &inner);\n    }\n}\n\n// ---------------------------------------------------------------------------\n// Focus navigation logic (pure, no SDL — also used by host tests)\n// ---------------------------------------------------------------------------\n\n// Returns the index (0-based) of the tile occupying grid cell (cx, cy), or -1.\nstatic int tileAt(const model::Workspace& ws, int cx, int cy) {\n    for (int i = 0; i < static_cast<int>(ws.tiles.size()); ++i) {\n        const auto& t = ws.tiles[i];\n        if (cx >= t.x && cx < t.x + t.w &&\n            cy >= t.y && cy < t.y + t.h) {\n            return i;\n        }\n    }\n    return -1;\n}\n\n// Return true if any tile occupies (cx, cy).\nstatic bool cellOccupied(const model::Workspace& ws, int cx, int cy) {\n    return tileAt(ws, cx, cy) >= 0;\n}\n\n// Scan the grid in top-left order and return the first occupied cell, or\n// {-1,-1} when the workspace is empty.\nstatic std::pair<int,int> firstOccupied(const model::Workspace& ws) {\n    for (int row = 0; row < model::Rows; ++row)\n        for (int col = 0; col < model::Columns; ++col)\n            if (cellOccupied(ws, col, row))\n                return {col, row};\n    return {-1, -1};\n}\n\n// Linear index for row-major (reading) order.\nstatic int linearRowMajor(int col, int row) {\n    return row * model::Columns + col;\n}\n// Linear index for column-major order.\nstatic int linearColMajor(int col, int row) {\n    return col * model::Rows + row;\n}\n\n// Return (col, row) from a row-major linear index.\nstatic std::pair<int,int> fromRowMajor(int idx) {\n    return {idx % model::Columns, idx / model::Columns};\n}\n// Return (col, row) from a column-major linear index.\nstatic std::pair<int,int> fromColMajor(int idx) {\n    return {idx / model::Rows, idx % model::Rows};\n}\n\n// Move (cx,cy) one step in direction dir, wrapping, and try up to\n// Columns*Rows steps.  Returns the first occupied cell found, or the\n// original position when no occupied cell exists.\n//\n// Right/Left scan in reading order (row-major, wrapping rows).\n// Down/Up   scan in column order (column-major, wrapping columns).\nstatic std::pair<int,int> nextOccupied(const model::Workspace& ws,\n                                        int cx, int cy,\n                                        touch::Direction dir) {\n    if (ws.tiles.empty()) return {cx, cy};\n\n    const int total = model::Columns * model::Rows;\n    const int curRowMajor = linearRowMajor(cx, cy);\n    const int curColMajor = linearColMajor(cx, cy);\n\n    for (int step = 1; step <= total; ++step) {\n        int idx;\n        std::pair<int,int> cell;\n        switch (dir) {\n            case touch::Direction::Right:\n                idx = (curRowMajor + step) % total;\n                cell = fromRowMajor(idx);\n                break;\n            case touch::Direction::Left:\n                idx = (curRowMajor - step + total) % total;\n                cell = fromRowMajor(idx);\n                break;\n            case touch::Direction::Down:\n                idx = (curColMajor + step) % total;\n                cell = fromColMajor(idx);\n                break;\n            case touch::Direction::Up:\n                idx = (curColMajor - step + total) % total;\n                cell = fromColMajor(idx);\n                break;\n        }\n        if (cellOccupied(ws, cell.first, cell.second))\n            return cell;\n    }\n    return {cx, cy};\n}\n\n// ---------------------------------------------------------------------------\n// SDL2 implementation\n// ---------------------------------------------------------------------------\n\nclass GridRendererSDL2 : public GridRenderer {\npublic:\n    explicit GridRendererSDL2(SDL_Renderer* renderer)\n        : m_renderer(renderer) {}\n\n    // ── GridRenderer interface ───────────────────────────────────────────────\n\n    void render(const model::Workspace& workspace) override {\n        setColor(m_renderer, Colors::BackgroundDark);\n        SDL_RenderClear(m_renderer);\n\n        for (int i = 0; i < static_cast<int>(workspace.tiles.size()); ++i) {\n            const auto& tile = workspace.tiles[i];\n            const bool hovered  = (i == m_hoveredTile);\n            const bool dragging = (i == m_draggedTile);\n            const bool focused  = cellOccupied(workspace, m_focusX, m_focusY)\n                                  && (tileAt(workspace, m_focusX, m_focusY) == i);\n\n            // Skip rendering the dragged tile at its original position;\n            // we'll draw a ghost there and a preview at the destination.\n            if (m_dragPreviewActive && i == m_dragPreviewTile) {\n                renderDragGhost(tile);\n                renderDragPreview(workspace, tile);\n                continue;\n            }\n\n            renderTile(tile, hovered, dragging, focused);\n        }\n\n        // Draw empty-cell grid dots for orientation (subtle)\n        renderGridDots(workspace);\n\n        SDL_RenderPresent(m_renderer);\n    }\n\n    void setHoveredTile(int idx) override { m_hoveredTile = idx; }\n\n    void setDraggedTile(int idx, int sx, int sy) override {\n        m_draggedTile  = idx;\n        m_dragScreenX  = sx;\n        m_dragScreenY  = sy;\n    }\n\n    // ── Drag preview (Phase 1.2) ──────────────────────────────────────────────\n\n    void beginDragPreview(int tileIndex,\n                          int origX, int origY,\n                          int destX, int destY,\n                          bool destValid) override {\n        m_dragPreviewActive = true;\n        m_dragPreviewTile   = tileIndex;\n        m_dragOrigX = origX;\n        m_dragOrigY = origY;\n        m_dragDestX = destX;\n        m_dragDestY = destY;\n        m_dragDestValid = destValid;\n    }\n\n    void updateDragPreview(int destX, int destY, bool destValid) override {\n        m_dragDestX = destX;\n        m_dragDestY = destY;\n        m_dragDestValid = destValid;\n    }\n\n    void endDragPreview() override {\n        m_dragPreviewActive = false;\n        m_dragPreviewTile   = -1;\n    }\n\n    bool isDragPreviewActive() const override { return m_dragPreviewActive; }\n\n    int screenToGridX(int sx) const override {\n        return std::clamp((sx - 20) / (GridLayout::TileWidth  + GridLayout::PaddingX),\n                          0, model::Columns - 1);\n    }\n    int screenToGridY(int sy) const override {\n        return std::clamp((sy - 20) / (GridLayout::TileHeight + GridLayout::PaddingY),\n                          0, model::Rows - 1);\n    }\n\n    void getTileBounds(int gx, int gy, int gw, int gh,\n                       int& ox, int& oy, int& ow, int& oh) const override {\n        ox = GridLayout::gridToScreenX(gx);\n        oy = GridLayout::gridToScreenY(gy);\n        ow = GridLayout::gridToScreenW(gw);\n        oh = GridLayout::gridToScreenH(gh);\n    }\n\n    // ── Focus API ────────────────────────────────────────────────────────────\n\n    void setFocus(int x, int y) override {\n        m_focusX = std::clamp(x, 0, model::Columns - 1);\n        m_focusY = std::clamp(y, 0, model::Rows    - 1);\n    }\n\n    void moveFocus(touch::Direction dir) override {\n        // Respect the debounce counter.\n        if (m_focusDebounce > 0) { --m_focusDebounce; return; }\n        m_focusDebounce = kFocusDebounceFrames;\n\n        // We need the workspace — keep a cached pointer updated each render().\n        // If no workspace is cached yet, do nothing.\n        if (!m_lastWorkspace) return;\n\n        auto [nx, ny] = nextOccupied(*m_lastWorkspace, m_focusX, m_focusY, dir);\n        m_focusX = nx;\n        m_focusY = ny;\n    }\n\n    int focusX() const override { return m_focusX; }\n    int focusY() const override { return m_focusY; }\n\n    int focusedTileIndex(const model::Workspace& ws) const override {\n        return tileAt(ws, m_focusX, m_focusY);\n    }\n\n    void initFocus(const model::Workspace& ws) override {\n        m_lastWorkspace = &ws;\n        auto [fx, fy] = firstOccupied(ws);\n        if (fx >= 0) { m_focusX = fx; m_focusY = fy; }\n        else         { m_focusX = 0;  m_focusY = 0;  }\n    }\n\n    void setWorkspace(const model::Workspace& ws) override {\n        m_lastWorkspace = &ws;\n    }\n\nprivate:\n    // ── rendering helpers ────────────────────────────────────────────────────\n\n    void renderTile(const model::Tile& tile,\n                    bool hovered, bool dragging, bool focused) {\n        int x = GridLayout::gridToScreenX(tile.x);\n        int y = GridLayout::gridToScreenY(tile.y);\n        int w = GridLayout::gridToScreenW(tile.w);\n        int h = GridLayout::gridToScreenH(tile.h);\n\n        if (dragging) { x = m_dragScreenX - w / 2; y = m_dragScreenY - h / 2; }\n\n        SDL_Rect rect{x, y, w, h};\n\n        // Background\n        uint32_t bg = focused  ? Colors::TileFocusBg\n                    : hovered  ? Colors::TileHover\n                    : dragging ? Colors::TileActive\n                               : Colors::TileBackground;\n        setColor(m_renderer, bg);\n        SDL_RenderFillRect(m_renderer, &rect);\n\n        // Border — 2 px thick for focused, 1 px for others\n        if (focused) {\n            setColor(m_renderer, Colors::TileFocusBorder);\n            drawBorderRect(m_renderer, rect, 2);\n        } else {\n            setColor(m_renderer, Colors::TileBorder);\n            SDL_RenderDrawRect(m_renderer, &rect);\n        }\n    }\n\n    void renderGridDots(const model::Workspace& ws) {\n        // 4-pixel dots at the centre of each cell that has no tile.\n        setColor(m_renderer, 0x303030FF);\n        for (int row = 0; row < model::Rows; ++row) {\n            for (int col = 0; col < model::Columns; ++col) {\n                if (cellOccupied(ws, col, row)) continue;\n                int cx = GridLayout::gridToScreenX(col) + GridLayout::TileWidth  / 2;\n                int cy = GridLayout::gridToScreenY(row) + GridLayout::TileHeight / 2;\n                SDL_Rect dot{cx - 2, cy - 2, 4, 4};\n                SDL_RenderFillRect(m_renderer, &dot);\n            }\n        }\n    }\n\n    // ── Drag preview rendering ───────────────────────────────────────────────\n\n    /// Draw a 40% opacity ghost at the tile's original position.\n    void renderDragGhost(const model::Tile& tile) {\n        int x = GridLayout::gridToScreenX(tile.x);\n        int y = GridLayout::gridToScreenY(tile.y);\n        int w = GridLayout::gridToScreenW(tile.w);\n        int h = GridLayout::gridToScreenH(tile.h);\n        SDL_Rect rect{x, y, w, h};\n\n        setColor(m_renderer, Colors::TileGhost);\n        SDL_RenderFillRect(m_renderer, &rect);\n        setColor(m_renderer, 0x50505088);\n        drawBorderRect(m_renderer, rect, 1);\n    }\n\n    /// Draw the drag preview at the destination cell with green/red tint.\n    void renderDragPreview(const model::Workspace& ws, const model::Tile& tile) {\n        int x = GridLayout::gridToScreenX(m_dragDestX);\n        int y = GridLayout::gridToScreenY(m_dragDestY);\n        int w = GridLayout::gridToScreenW(tile.w);\n        int h = GridLayout::gridToScreenH(tile.h);\n        SDL_Rect rect{x, y, w, h};\n\n        uint32_t bg = m_dragDestValid ? Colors::TileDragValid\n                                       : Colors::TileDragInvalid;\n        uint32_t bd = m_dragDestValid ? Colors::TileDragBorderV\n                                       : Colors::TileDragBorderI;\n\n        setColor(m_renderer, bg);\n        SDL_RenderFillRect(m_renderer, &rect);\n        setColor(m_renderer, bd);\n        drawBorderRect(m_renderer, rect, 3);\n    }\n\n    // ── state ────────────────────────────────────────────────────────────────\n\n    SDL_Renderer* m_renderer;\n\n    int m_hoveredTile  = -1;\n    int m_draggedTile  = -1;\n    int m_dragScreenX  = 0;\n    int m_dragScreenY  = 0;\n\n    // Drag preview state\n    bool m_dragPreviewActive = false;\n    int  m_dragPreviewTile   = -1;\n    int  m_dragOrigX = 0, m_dragOrigY = 0;\n    int  m_dragDestX = 0, m_dragDestY = 0;\n    bool m_dragDestValid = true;\n\n    int m_focusX = 0;\n    int m_focusY = 0;\n\n    // We cache the last workspace pointer so moveFocus() can query occupancy\n    // without it being passed as a parameter (keeping the existing hot-path\n    // render() signature unchanged).\n    const model::Workspace* m_lastWorkspace = nullptr;\n\n    // Debounce: ignore repeated D-pad events within N frames (~10 × 16 ms).\n    static constexpr int kFocusDebounceFrames = 10;\n    int m_focusDebounce = 0;\n};\n\n// ---------------------------------------------------------------------------\n// Factory\n// ---------------------------------------------------------------------------\n\nGridRenderer* createGridRenderer(SDL_Renderer* renderer) {\n    return new GridRendererSDL2(renderer);\n}\n\n} // namespace desknx::menu::renderer\n"}