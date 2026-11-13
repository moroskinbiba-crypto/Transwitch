// Copyright contributors to the DeskNX project.
// SPDX-License-Identifier: MIT

#include "TileInputHandler.hpp"
#include <algorithm>
#include <cstdio>

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

    void setBPressed(bool pressed) override {
        m_bPressed = pressed;
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
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

TileInputHandler* createTileInputHandler() {
    return new TileInputHandlerImpl();
}

} // namespace desknx::menu::input

{"content":"// Copyright contributors to the DeskNX project.\n// SPDX-License-Identifier: MIT\n\n#include \"TileInputHandler.hpp\"\n#include <algorithm>\n#include <cstdio>\n\nnamespace desknx::menu::input {\n\n// ---------------------------------------------------------------------------\n// Helpers\n// ---------------------------------------------------------------------------\n\n/// Returns true if placing `tile` at (destX, destY) in `ws` is valid,\n/// ignoring the tile's current position (index `ignoreIdx`).\nstatic bool canDrop(const model::Workspace& ws,\n                    const model::Tile& tile,\n                    int destX, int destY,\n                    int ignoreIdx) {\n    return model::placeable(ws, tile, destX, destY, ignoreIdx);\n}\n\n// ---------------------------------------------------------------------------\n// Implementation\n// ---------------------------------------------------------------------------\n\nclass TileInputHandlerImpl : public TileInputHandler {\npublic:\n    // ── Per-frame update ─────────────────────────────────────────────────────\n\n    bool update(model::Workspace& /*workspace*/, float /*dt*/) override {\n        // Transitions driven by setAHeld() / setBPressed() / moveDragDest().\n        // Nothing extra needed here currently.\n        return false;\n    }\n\n    // ── Touch (legacy stub) ──────────────────────────────────────────────────\n\n    bool onTouchEvent(int /*sx*/, int /*sy*/, bool /*pressed*/,\n                      const gesture::Touch& /*g*/) override {\n        return false;\n    }\n\n    bool onButtonPress(uint32_t /*buttonId*/) override {\n        return false;\n    }\n\n    // ── A / B button feed ────────────────────────────────────────────────────\n\n    void setAHeld(bool held) override {\n        if (held) {\n            ++m_aHoldFrames;\n        } else {\n            m_aHoldFrames = 0;\n        }\n        m_aHeld = held;\n    }\n\n    int getAHoldFrames() const override { return m_aHoldFrames; }\n\n    void setBPressed(bool pressed) override {\n        m_bPressed = pressed;\n    }\n\n    // ── Drag mode control ────────────────────────────────────────────────────\n\n    EditMode getEditMode() const override { return m_editMode; }\n\n    int getDraggedTileIndex() const override { return m_dragTileIdx; }\n    int getDragOriginX()      const override { return m_dragOriginX; }\n    int getDragOriginY()      const override { return m_dragOriginY; }\n    int getDragDestX()        const override { return m_dragDestX; }\n    int getDragDestY()        const override { return m_dragDestY; }\n    bool isDragDestValid()    const override { return m_dragDestValid; }\n\n    void enterDrag(int tileIdx, int ox, int oy) override {\n        m_editMode     = EditMode::Drag;\n        m_dragTileIdx  = tileIdx;\n        m_dragOriginX  = ox;\n        m_dragOriginY  = oy;\n        m_dragDestX    = ox;\n        m_dragDestY    = oy;\n        m_dragDestValid = true; // origin is always valid\n        debugPrintf(\"[Drag] grabbed tile %d at (%d,%d)\\n\", tileIdx, ox, oy);\n    }\n\n    void moveDragDest(int dx, int dy,\n                      const model::Workspace& ws) override {\n        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0) return;\n\n        int nx = std::clamp(m_dragDestX + dx, 0, model::Columns - 1);\n        int ny = std::clamp(m_dragDestY + dy, 0, model::Rows    - 1);\n        m_dragDestX = nx;\n        m_dragDestY = ny;\n\n        // Validate — ignore the tile's own cells while checking.\n        const auto& tile = ws.tiles[static_cast<size_t>(m_dragTileIdx)];\n        m_dragDestValid = canDrop(ws, tile, nx, ny, m_dragTileIdx);\n\n        debugPrintf(\"[Drag] dest (%d,%d) valid=%d\\n\", nx, ny, m_dragDestValid);\n    }\n\n    DropResult commitDrop(model::Workspace& ws) override {\n        if (m_editMode != EditMode::Drag || m_dragTileIdx < 0)\n            return DropResult::NothingDragged;\n\n        const bool valid = m_dragDestValid\n                           && m_dragDestX >= 0 && m_dragDestY >= 0;\n\n        DropResult result;\n        if (valid) {\n            // Apply the move.\n            auto& tile  = ws.tiles[static_cast<size_t>(m_dragTileIdx)];\n            tile.x = m_dragDestX;\n            tile.y = m_dragDestY;\n            result = DropResult::Committed;\n            debugPrintf(\"[Drop] committed tile %d → (%d,%d)\\n\",\n                        m_dragTileIdx, tile.x, tile.y);\n        } else {\n            // Bounce back — destination was invalid; tile stays at origin.\n            debugPrintf(\"[Drop] invalid dest (%d,%d), bouncing tile %d back to (%d,%d)\\n\",\n                        m_dragDestX, m_dragDestY,\n                        m_dragTileIdx, m_dragOriginX, m_dragOriginY);\n            result = DropResult::Invalid;\n        }\n\n        exitDrag();\n        return result;\n    }\n\n    DropResult cancelDrop() override {\n        if (m_editMode != EditMode::Drag) return DropResult::NothingDragged;\n        debugPrintf(\"[Drag] cancelled, tile %d stays at (%d,%d)\\n\",\n                    m_dragTileIdx, m_dragOriginX, m_dragOriginY);\n        exitDrag();\n        return DropResult::Cancelled;\n    }\n\n    // ── A hold threshold ─────────────────────────────────────────────────────\n\n    static constexpr int kAHoldThreshold = 15; // ≈250 ms @ 60 fps\n\n    bool shouldEnterDrag() const {\n        return m_editMode == EditMode::Browse\n               && m_aHeld\n               && m_aHoldFrames >= kAHoldThreshold;\n    }\n\n    bool shouldCommitDrop() const {\n        // A was just released while in Drag mode.\n        return m_editMode == EditMode::Drag && !m_aHeld && m_aHoldFrames == 0;\n    }\n\n    bool shouldCancelDrop() const {\n        return m_editMode == EditMode::Drag && m_bPressed;\n    }\n\n    // ── Legacy interface ─────────────────────────────────────────────────────\n\n    State getCurrentState() const override {\n        switch (m_editMode) {\n            case EditMode::Drag:   return State::Dragging;\n            default:               return State::Idle;\n        }\n    }\n\n    int getSelectedTileIndex() const override { return m_dragTileIdx; }\n\n    void reset() override {\n        m_editMode     = EditMode::Browse;\n        m_dragTileIdx  = -1;\n        m_dragOriginX  = 0;\n        m_dragOriginY  = 0;\n        m_dragDestX    = 0;\n        m_dragDestY    = 0;\n        m_dragDestValid = false;\n        m_aHeld        = false;\n        m_aHoldFrames  = 0;\n        m_bPressed     = false;\n    }\n\nprivate:\n    void exitDrag() {\n        m_editMode     = EditMode::Browse;\n        m_dragTileIdx  = -1;\n        m_aHoldFrames  = 0;\n        m_bPressed     = false;\n    }\n\n    EditMode m_editMode      = EditMode::Browse;\n    int      m_dragTileIdx   = -1;\n    int      m_dragOriginX   = 0;\n    int      m_dragOriginY   = 0;\n    int      m_dragDestX     = 0;\n    int      m_dragDestY     = 0;\n    bool     m_dragDestValid = false;\n\n    bool m_aHeld       = false;\n    int  m_aHoldFrames = 0;\n    bool m_bPressed    = false;\n};\n\n// ---------------------------------------------------------------------------\n// Factory\n// ---------------------------------------------------------------------------\n\nTileInputHandler* createTileInputHandler() {\n    return new TileInputHandlerImpl();\n}\n\n} // namespace desknx::menu::input\n"}