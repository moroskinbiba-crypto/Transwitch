// Copyright contributors to the DeskNX project.
// SPDX-License-Identifier: MIT

#include "TileInputHandler.hpp"
#include <algorithm>
#include <cstdio>

// Debug printf stub for logging (no-op on host, writes to log on Switch)
#ifdef __SWITCH__
#include <desknx/Diagnostics.hpp>
#define debugPrintf(...) desknx::diag::Write("TileInput", "event", __VA_ARGS__)
#else
#define debugPrintf(...) ((void)0)
#endif

namespace desknx::menu::input {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Returns true if placing `tile` at (destX, destY) in `ws` is valid,
/// ignoring the tile's current position (index `ignoreIdx`).
static bool canDrop(const model::Workspace& ws,
                    const model::Tile& tile,
                    int destX, int destY,
                    int ignoreIdx) {
    return model::placeable(ws, tile, destX, destY, ignoreIdx);
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

class TileInputHandlerImpl : public TileInputHandler {
public:
    // ── Per-frame update ─────────────────────────────────────────────────────

    bool update(model::Workspace& /*workspace*/, float /*dt*/) override {
        // Transitions driven by setAHeld() / setBPressed() / moveDragDest().
        // Nothing extra needed here currently.
        return false;
    }

    // ── Touch (legacy stub) ──────────────────────────────────────────────────

    bool onTouchEvent(int /*sx*/, int /*sy*/, bool /*pressed*/,
                      const gesture::Touch& /*g*/) override {
        return false;
    }

    bool onButtonPress(uint32_t /*buttonId*/) override {
        return false;
    }

    // ── A / B button feed ────────────────────────────────────────────────────

    void setAHeld(bool held) override {
        if (held) {
            ++m_aHoldFrames;
        } else {
            m_aHoldFrames = 0;
        }
        m_aHeld = held;
    }

    int getAHoldFrames() const override { return m_aHoldFrames; }

    void setBPressed(bool pressed) {
        m_bPressed = pressed;
    }

    void setYPressed(bool pressed) override {
        m_yPressed = pressed;
    }

    void setXPressed(bool pressed) override {
        m_xPressed = pressed;
    }

    // ── Drag mode control ────────────────────────────────────────────────────

    EditMode getEditMode() const override { return m_editMode; }

    int getDraggedTileIndex() const override { return m_dragTileIdx; }
    int getDragOriginX()      const override { return m_dragOriginX; }
    int getDragOriginY()      const override { return m_dragOriginY; }
    int getDragDestX()        const override { return m_dragDestX; }
    int getDragDestY()        const override { return m_dragDestY; }
    bool isDragDestValid()    const override { return m_dragDestValid; }

    void enterDrag(int tileIdx, int ox, int oy) override {
        m_editMode     = EditMode::Drag;
        m_dragTileIdx  = tileIdx;
        m_dragOriginX  = ox;
        m_dragOriginY  = oy;
        m_dragDestX    = ox;
        m_dragDestY    = oy;
        m_dragDestValid = true; // origin is always valid
        debugPrintf("[Drag] grabbed tile %d at (%d,%d)\n", tileIdx, ox, oy);
    }

    void moveDragDest(int dx, int dy,
                      const model::Workspace& ws) override {
        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0) return;

        int nx = std::clamp(m_dragDestX + dx, 0, model::Columns - 1);
        int ny = std::clamp(m_dragDestY + dy, 0, model::Rows    - 1);
        m_dragDestX = nx;
        m_dragDestY = ny;

        // Validate — ignore the tile's own cells while checking.
        const auto& tile = ws.tiles[static_cast<size_t>(m_dragTileIdx)];
        m_dragDestValid = canDrop(ws, tile, nx, ny, m_dragTileIdx);

        debugPrintf("[Drag] dest (%d,%d) valid=%d\n", nx, ny, m_dragDestValid);
    }

    DropResult commitDrop(model::Workspace& ws) override {
        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0)
            return DropResult::NothingDragged;

        const bool valid = m_dragDestValid
                           && m_dragDestX >= 0 && m_dragDestY >= 0;

        DropResult result;
        if (valid) {
            // Apply the move.
            auto& tile  = ws.tiles[static_cast<size_t>(m_dragTileIdx)];
            tile.x = m_dragDestX;
            tile.y = m_dragDestY;
            result = DropResult::Committed;
            debugPrintf("[Drop] committed tile %d → (%d,%d)\n",
                        m_dragTileIdx, tile.x, tile.y);
        } else {
            // Bounce back — destination was invalid; tile stays at origin.
            debugPrintf("[Drop] invalid dest (%d,%d), bouncing tile %d back to (%d,%d)\n",
                        m_dragDestX, m_dragDestY,
                        m_dragTileIdx, m_dragOriginX, m_dragOriginY);
            result = DropResult::Invalid;
        }

        exitDrag();
        return result;
    }

    DropResult cancelDrop() override {
        if (m_editMode != EditMode::Drag) return DropResult::NothingDragged;
        debugPrintf("[Drag] cancelled, tile %d stays at (%d,%d)\n",
                    m_dragTileIdx, m_dragOriginX, m_dragOriginY);
        exitDrag();
        return DropResult::Cancelled;
    }

    // ── Resize mode ──────────────────────────────────────────────────────────

    int getResizeTileIndex() const override { return m_resizeTileIdx; }

    std::pair<int, int> getResizeTargetSize() const override {
        return m_resizeTargetSize;
    }

    bool isResizeValid() const override { return m_resizeValid; }

    void enterResize(int tileIdx, const model::Workspace& ws) override {
        if (tileIdx < 0 || tileIdx >= static_cast<int>(ws.tiles.size())) return;
        m_editMode = EditMode::Resize;
        m_resizeTileIdx = tileIdx;
        const auto& tile = ws.tiles[static_cast<size_t>(tileIdx)];
        m_resizeTargetSize = {tile.w, tile.h};
        m_resizeSizeIdx = findSizeIndex(tile.w, tile.h);
        validateResize(ws, tile);
        debugPrintf("[Resize] entered for tile %d, current size (%d,%d)\n",
                    tileIdx, tile.w, tile.h);
    }

    void cycleResizeSize(int delta, const model::Workspace& ws) override {
        if (m_editMode != EditMode::Resize || m_resizeTileIdx < 0) return;
        m_resizeSizeIdx = (m_resizeSizeIdx + delta + NumSupportedSizes) % NumSupportedSizes;
        m_resizeTargetSize = SupportedSizes[m_resizeSizeIdx];
        const auto& tile = ws.tiles[static_cast<size_t>(m_resizeTileIdx)];
        validateResize(ws, tile);
        debugPrintf("[Resize] new size (%d,%d) valid=%d\n",
                    m_resizeTargetSize.first, m_resizeTargetSize.second, m_resizeValid);
    }

    bool commitResize(model::Workspace& ws) override {
        if (m_editMode != EditMode::Resize || m_resizeTileIdx < 0) return false;
        if (!m_resizeValid) {
            debugPrintf("[Resize] commit failed, invalid size\n");
            exitResize();
            return false;
        }
        auto& tile = ws.tiles[static_cast<size_t>(m_resizeTileIdx)];
        tile.w = m_resizeTargetSize.first;
        tile.h = m_resizeTargetSize.second;
        debugPrintf("[Resize] committed tile %d → size (%d,%d)\n",
                    m_resizeTileIdx, tile.w, tile.h);
        exitResize();
        return true;
    }

    void cancelResize() override {
        if (m_editMode != EditMode::Resize) return;
        debugPrintf("[Resize] cancelled\n");
        exitResize();
    }

    // ── Remove/mark mode ─────────────────────────────────────────────────────

    int getMarkedTileIndex() const override { return m_markedTileIdx; }

    void markForDelete(int tileIdx) override {
        m_editMode = EditMode::Marked;
        m_markedTileIdx = tileIdx;
        m_markFrames = 0;
        debugPrintf("[Remove] marked tile %d for delete\n", tileIdx);
    }

    bool confirmDelete(model::Workspace& ws) override {
        if (m_editMode != EditMode::Marked || m_markedTileIdx < 0) return false;
        // Remove tile from workspace
        ws.tiles.erase(ws.tiles.begin() + m_markedTileIdx);
        debugPrintf("[Remove] deleted tile %d\n", m_markedTileIdx);
        exitMarked();
        return true;
    }

    void cancelMark() override {
        if (m_editMode != EditMode::Marked) return;
        debugPrintf("[Remove] mark cancelled\n");
        exitMarked();
    }

    // ── A hold threshold ─────────────────────────────────────────────────────

    static constexpr int kAHoldThreshold = 15; // ≈250 ms @ 60 fps

    bool shouldEnterDrag() const {
        return m_editMode == EditMode::Browse
               && m_aHeld
               && m_aHoldFrames >= kAHoldThreshold;
    }

    bool shouldCommitDrop() const {
        // A was just released while in Drag mode.
        return m_editMode == EditMode::Drag && !m_aHeld && m_aHoldFrames == 0;
    }

    bool shouldCancelDrop() const {
        return m_editMode == EditMode::Drag && m_bPressed;
    }

    // ── Legacy interface ─────────────────────────────────────────────────────

    State getCurrentState() const override {
        switch (m_editMode) {
            case EditMode::Drag:   return State::Dragging;
            default:               return State::Idle;
        }
    }

    int getSelectedTileIndex() const override { return m_dragTileIdx; }

    void reset() override {
        m_editMode     = EditMode::Browse;
        m_dragTileIdx  = -1;
        m_dragOriginX  = 0;
        m_dragOriginY  = 0;
        m_dragDestX    = 0;
        m_dragDestY    = 0;
        m_dragDestValid = false;
        m_aHeld        = false;
        m_aHoldFrames  = 0;
        m_bPressed     = false;
    }

private:
    void exitDrag() {
        m_editMode     = EditMode::Browse;
        m_dragTileIdx  = -1;
        m_aHoldFrames  = 0;
        m_bPressed     = false;
    }

    void exitResize() {
        m_editMode = EditMode::Browse;
        m_resizeTileIdx = -1;
        m_resizeSizeIdx = 0;
        m_resizeValid = false;
    }

    void exitMarked() {
        m_editMode = EditMode::Browse;
        m_markedTileIdx = -1;
        m_markFrames = 0;
    }

    int findSizeIndex(int w, int h) const {
        for (int i = 0; i < NumSupportedSizes; ++i) {
            if (SupportedSizes[i].first == w && SupportedSizes[i].second == h)
                return i;
        }
        return 0; // default to 1×1
    }

    void validateResize(const model::Workspace& ws, const model::Tile& tile) {
        // Create temp tile with new size at same position
        model::Tile temp = tile;
        temp.w = m_resizeTargetSize.first;
        temp.h = m_resizeTargetSize.second;
        m_resizeValid = canDrop(ws, temp, tile.x, tile.y, m_resizeTileIdx);
    }

    EditMode m_editMode      = EditMode::Browse;
    int      m_dragTileIdx   = -1;
    int      m_dragOriginX   = 0;
    int      m_dragOriginY   = 0;
    int      m_dragDestX     = 0;
    int      m_dragDestY     = 0;
    bool     m_dragDestValid = false;

    bool m_aHeld       = false;
    int  m_aHoldFrames = 0;
    bool m_bPressed    = false;
    bool m_yPressed    = false;
    bool m_xPressed    = false;

    // Resize state
    int  m_resizeTileIdx    = -1;
    int  m_resizeSizeIdx    = 0;  // index into SupportedSizes
    std::pair<int, int> m_resizeTargetSize = {1, 1};
    bool m_resizeValid      = false;

    // Mark/delete state
    int  m_markedTileIdx = -1;
    int  m_markFrames    = 0;  // for double-press detection
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

TileInputHandler* createTileInputHandler() {
    return new TileInputHandlerImpl();
}

} // namespace desknx::menu::input

{"content":"// Copyright contributors to the DeskNX project.\n// SPDX-License-Identifier: MIT\n\n#include \"TileInputHandler.hpp\"\n#include <algorithm>\n#include <cstdio>\n\n// Debug printf stub for logging (no-op on host, writes to log on Switch)\n#ifdef __SWITCH__\n#include <desknx/Diagnostics.hpp>\n#define debugPrintf(...) desknx::diag::Write(\"TileInput\", \"event\", __VA_ARGS__)\n#else\n#define debugPrintf(...) ((void)0)\n#endif\n\nnamespace desknx::menu::input {\n\n// ---------------------------------------------------------------------------\n// Helpers\n// ---------------------------------------------------------------------------\n\n/// Returns true if placing `tile` at (destX, destY) in `ws` is valid,\n/// ignoring the tile's current position (index `ignoreIdx`).\nstatic bool canDrop(const model::Workspace& ws,\n                    const model::Tile& tile,\n                    int destX, int destY,\n                    int ignoreIdx) {\n    return model::placeable(ws, tile, destX, destY, ignoreIdx);\n}\n\n// ---------------------------------------------------------------------------\n// Implementation\n// ---------------------------------------------------------------------------\n\nclass TileInputHandlerImpl : public TileInputHandler {\npublic:\n    // ── Per-frame update ─────────────────────────────────────────────────────\n\n    bool update(model::Workspace& /*workspace*/, float /*dt*/) override {\n        // Transitions driven by setAHeld() / setBPressed() / moveDragDest().\n        // Nothing extra needed here currently.\n        return false;\n    }\n\n    // ── Touch (legacy stub) ──────────────────────────────────────────────────\n\n    bool onTouchEvent(int /*sx*/, int /*sy*/, bool /*pressed*/,\n                      const gesture::Touch& /*g*/) override {\n        return false;\n    }\n\n    bool onButtonPress(uint32_t /*buttonId*/) override {\n        return false;\n    }\n\n    // ── A / B button feed ────────────────────────────────────────────────────\n\n    void setAHeld(bool held) override {\n        if (held) {\n            ++m_aHoldFrames;\n        } else {\n            m_aHoldFrames = 0;\n        }\n        m_aHeld = held;\n    }\n\n    int getAHoldFrames() const override { return m_aHoldFrames; }\n\n    void setBPressed(bool pressed) {\n        m_bPressed = pressed;\n    }\n\n    void setYPressed(bool pressed) override {\n        m_yPressed = pressed;\n    }\n\n    void setXPressed(bool pressed) override {\n        m_xPressed = pressed;\n    }\n\n    // ── Drag mode control ────────────────────────────────────────────────────\n\n    EditMode getEditMode() const override { return m_editMode; }\n\n    int getDraggedTileIndex() const override { return m_dragTileIdx; }\n    int getDragOriginX()      const override { return m_dragOriginX; }\n    int getDragOriginY()      const override { return m_dragOriginY; }\n    int getDragDestX()        const override { return m_dragDestX; }\n    int getDragDestY()        const override { return m_dragDestY; }\n    bool isDragDestValid()    const override { return m_dragDestValid; }\n\n    void enterDrag(int tileIdx, int ox, int oy) override {\n        m_editMode     = EditMode::Drag;\n        m_dragTileIdx  = tileIdx;\n        m_dragOriginX  = ox;\n        m_dragOriginY  = oy;\n        m_dragDestX    = ox;\n        m_dragDestY    = oy;\n        m_dragDestValid = true; // origin is always valid\n        debugPrintf(\"[Drag] grabbed tile %d at (%d,%d)\\n\", tileIdx, ox, oy);\n    }\n\n    void moveDragDest(int dx, int dy,\n                      const model::Workspace& ws) override {\n        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0) return;\n\n        int nx = std::clamp(m_dragDestX + dx, 0, model::Columns - 1);\n        int ny = std::clamp(m_dragDestY + dy, 0, model::Rows    - 1);\n        m_dragDestX = nx;\n        m_dragDestY = ny;\n\n        // Validate — ignore the tile's own cells while checking.\n        const auto& tile = ws.tiles[static_cast<size_t>(m_dragTileIdx)];\n        m_dragDestValid = canDrop(ws, tile, nx, ny, m_dragTileIdx);\n\n        debugPrintf(\"[Drag] dest (%d,%d) valid=%d\\n\", nx, ny, m_dragDestValid);\n    }\n\n    DropResult commitDrop(model::Workspace& ws) override {\n        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0)\n            return DropResult::NothingDragged;\n\n        const bool valid = m_dragDestValid\n                           && m_dragDestX >= 0 && m_dragDestY >= 0;\n\n        DropResult result;\n        if (valid) {\n            // Apply the move.\n            auto& tile  = ws.tiles[static_cast<size_t>(m_dragTileIdx)];\n            tile.x = m_dragDestX;\n            tile.y = m_dragDestY;\n            result = DropResult::Committed;\n            debugPrintf(\"[Drop] committed tile %d → (%d,%d)\\n\",\n                        m_dragTileIdx, tile.x, tile.y);\n        } else {\n            // Bounce back — destination was invalid; tile stays at origin.\n            debugPrintf(\"[Drop] invalid dest (%d,%d), bouncing tile %d back to (%d,%d)\\n\",\n                        m_dragDestX, m_dragDestY,\n                        m_dragTileIdx, m_dragOriginX, m_dragOriginY);\n            result = DropResult::Invalid;\n        }\n\n        exitDrag();\n        return result;\n    }\n\n    DropResult cancelDrop() override {\n        if (m_editMode != EditMode::Drag) return DropResult::NothingDragged;\n        debugPrintf(\"[Drag] cancelled, tile %d stays at (%d,%d)\\n\",\n                    m_dragTileIdx, m_dragOriginX, m_dragOriginY);\n        exitDrag();\n        return DropResult::Cancelled;\n    }\n\n    // ── Resize mode ──────────────────────────────────────────────────────────\n\n    int getResizeTileIndex() const override { return m_resizeTileIdx; }\n\n    std::pair<int, int> getResizeTargetSize() const override {\n        return m_resizeTargetSize;\n    }\n\n    bool isResizeValid() const override { return m_resizeValid; }\n\n    void enterResize(int tileIdx, const model::Workspace& ws) override {\n        if (tileIdx < 0 || tileIdx >= static_cast<int>(ws.tiles.size())) return;\n        m_editMode = EditMode::Resize;\n        m_resizeTileIdx = tileIdx;\n        const auto& tile = ws.tiles[static_cast<size_t>(tileIdx)];\n        m_resizeTargetSize = {tile.w, tile.h};\n        m_resizeSizeIdx = findSizeIndex(tile.w, tile.h);\n        validateResize(ws, tile);\n        debugPrintf(\"[Resize] entered for tile %d, current size (%d,%d)\\n\",\n                    tileIdx, tile.w, tile.h);\n    }\n\n    void cycleResizeSize(int delta, const model::Workspace& ws) override {\n        if (m_editMode != EditMode::Resize || m_resizeTileIdx < 0) return;\n        m_resizeSizeIdx = (m_resizeSizeIdx + delta + NumSupportedSizes) % NumSupportedSizes;\n        m_resizeTargetSize = SupportedSizes[m_resizeSizeIdx];\n        const auto& tile = ws.tiles[static_cast<size_t>(m_resizeTileIdx)];\n        validateResize(ws, tile);\n        debugPrintf(\"[Resize] new size (%d,%d) valid=%d\\n\",\n                    m_resizeTargetSize.first, m_resizeTargetSize.second, m_resizeValid);\n    }\n\n    bool commitResize(model::Workspace& ws) override {\n        if (m_editMode != EditMode::Resize || m_resizeTileIdx < 0) return false;\n        if (!m_resizeValid) {\n            debugPrintf(\"[Resize] commit failed, invalid size\\n\");\n            exitResize();\n            return false;\n        }\n        auto& tile = ws.tiles[static_cast<size_t>(m_resizeTileIdx)];\n        tile.w = m_resizeTargetSize.first;\n        tile.h = m_resizeTargetSize.second;\n        debugPrintf(\"[Resize] committed tile %d → size (%d,%d)\\n\",\n                    m_resizeTileIdx, tile.w, tile.h);\n        exitResize();\n        return true;\n    }\n\n    void cancelResize() override {\n        if (m_editMode != EditMode::Resize) return;\n        debugPrintf(\"[Resize] cancelled\\n\");\n        exitResize();\n    }\n\n    // ── Remove/mark mode ─────────────────────────────────────────────────────\n\n    int getMarkedTileIndex() const override { return m_markedTileIdx; }\n\n    void markForDelete(int tileIdx) override {\n        m_editMode = EditMode::Marked;\n        m_markedTileIdx = tileIdx;\n        m_markFrames = 0;\n        debugPrintf(\"[Remove] marked tile %d for delete\\n\", tileIdx);\n    }\n\n    bool confirmDelete(model::Workspace& ws) override {\n        if (m_editMode != EditMode::Marked || m_markedTileIdx < 0) return false;\n        // Remove tile from workspace\n        ws.tiles.erase(ws.tiles.begin() + m_markedTileIdx);\n        debugPrintf(\"[Remove] deleted tile %d\\n\", m_markedTileIdx);\n        exitMarked();\n        return true;\n    }\n\n    void cancelMark() override {\n        if (m_editMode != EditMode::Marked) return;\n        debugPrintf(\"[Remove] mark cancelled\\n\");\n        exitMarked();\n    }\n\n    // ── A hold threshold ─────────────────────────────────────────────────────\n\n    static constexpr int kAHoldThreshold = 15; // ≈250 ms @ 60 fps\n\n    bool shouldEnterDrag() const {\n        return m_editMode == EditMode::Browse\n               && m_aHeld\n               && m_aHoldFrames >= kAHoldThreshold;\n    }\n\n    bool shouldCommitDrop() const {\n        // A was just released while in Drag mode.\n        return m_editMode == EditMode::Drag && !m_aHeld && m_aHoldFrames == 0;\n    }\n\n    bool shouldCancelDrop() const {\n        return m_editMode == EditMode::Drag && m_bPressed;\n    }\n\n    // ── Legacy interface ─────────────────────────────────────────────────────\n\n    State getCurrentState() const override {\n        switch (m_editMode) {\n            case EditMode::Drag:   return State::Dragging;\n            default:               return State::Idle;\n        }\n    }\n\n    int getSelectedTileIndex() const override { return m_dragTileIdx; }\n\n    void reset() override {\n        m_editMode     = EditMode::Browse;\n        m_dragTileIdx  = -1;\n        m_dragOriginX  = 0;\n        m_dragOriginY  = 0;\n        m_dragDestX    = 0;\n        m_dragDestY    = 0;\n        m_dragDestValid = false;\n        m_aHeld        = false;\n        m_aHoldFrames  = 0;\n        m_bPressed     = false;\n    }\n\nprivate:\n    void exitDrag() {\n        m_editMode     = EditMode::Browse;\n        m_dragTileIdx  = -1;\n        m_aHoldFrames  = 0;\n        m_bPressed     = false;\n    }\n\n    void exitResize() {\n        m_editMode = EditMode::Browse;\n        m_resizeTileIdx = -1;\n        m_resizeSizeIdx = 0;\n        m_resizeValid = false;\n    }\n\n    void exitMarked() {\n        m_editMode = EditMode::Browse;\n        m_markedTileIdx = -1;\n        m_markFrames = 0;\n    }\n\n    int findSizeIndex(int w, int h) const {\n        for (int i = 0; i < NumSupportedSizes; ++i) {\n            if (SupportedSizes[i].first == w && SupportedSizes[i].second == h)\n                return i;\n        }\n        return 0; // default to 1×1\n    }\n\n    void validateResize(const model::Workspace& ws, const model::Tile& tile) {\n        // Create temp tile with new size at same position\n        model::Tile temp = tile;\n        temp.w = m_resizeTargetSize.first;\n        temp.h = m_resizeTargetSize.second;\n        m_resizeValid = canDrop(ws, temp, tile.x, tile.y, m_resizeTileIdx);\n    }\n\n    EditMode m_editMode      = EditMode::Browse;\n    int      m_dragTileIdx   = -1;\n    int      m_dragOriginX   = 0;\n    int      m_dragOriginY   = 0;\n    int      m_dragDestX     = 0;\n    int      m_dragDestY     = 0;\n    bool     m_dragDestValid = false;\n\n    bool m_aHeld       = false;\n    int  m_aHoldFrames = 0;\n    bool m_bPressed    = false;\n    bool m_yPressed    = false;\n    bool m_xPressed    = false;\n\n    // Resize state\n    int  m_resizeTileIdx    = -1;\n    int  m_resizeSizeIdx    = 0;  // index into SupportedSizes\n    std::pair<int, int> m_resizeTargetSize = {1, 1};\n    bool m_resizeValid      = false;\n\n    // Mark/delete state\n    int  m_markedTileIdx = -1;\n    int  m_markFrames    = 0;  // for double-press detection\n};\n\n// ---------------------------------------------------------------------------\n// Factory\n// ---------------------------------------------------------------------------\n\nTileInputHandler* createTileInputHandler() {\n    return new TileInputHandlerImpl();\n}\n\n} // namespace desknx::menu::input\n"}