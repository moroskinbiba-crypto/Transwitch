#pragma once

// Copyright contributors to the DeskNX project.
// SPDX-License-Identifier: MIT

#include <desknx/Model.hpp>
#include <desknx/TouchGesture.hpp>
#include <cstdint>

namespace desknx::menu::input {

// Minimal touch gesture stub (matches existing code structure)
namespace gesture {
struct Touch {};
} // namespace gesture

/// Edit-mode state machine for drag/drop.
///
///   BROWSE ──(A held ≥15 frames)──► DRAG ──(A released, valid)──► BROWSE
///                                        └──(A released, invalid)─► BROWSE  (bounce back)
///                                        └──(B pressed)───────────► BROWSE  (cancel)
///
enum class EditMode {
    Browse,  ///< Normal navigation; D-pad moves focus cursor.
    Drag,    ///< Tile grabbed; D-pad moves the tile preview.
};

/// Result returned from commitDrop().
enum class DropResult {
    Committed,   ///< Move applied, workspace modified.
    Invalid,     ///< Destination blocked; tile bounced back.
    Cancelled,   ///< User cancelled (B button).
    NothingDragged,
};

/// Handles drag, resize, and selection input on tiles.
class TileInputHandler {
public:
    // Legacy state enum kept for backward compat (used by touch path).
    enum class State { Idle, Selected, Dragging, Resizing };

    struct ResizeEdge {
        enum Type { None = 0, Right = 1, Bottom = 2, RightBottom = 3 };
    };

    virtual ~TileInputHandler() = default;

    /// Update per-frame: increment hold timer, handle transitions. dt in seconds.
    /// Returns true if the workspace was modified.
    virtual bool update(model::Workspace& workspace, float dt) = 0;

    /// Notify of touch event. Returns true if consumed.
    virtual bool onTouchEvent(int screenX, int screenY, bool pressed,
                              const gesture::Touch& gesture) = 0;

    /// Notify of raw button press (HID bitmask). Returns true if consumed.
    virtual bool onButtonPress(uint32_t buttonId) = 0;

    /// Enter drag mode for the tile at index `tileIdx`, currently at (ox, oy).
    /// Call when A-hold threshold is reached.
    virtual void enterDrag(int tileIdx, int ox, int oy) = 0;

    /// Feed the current A-button held state each frame.
    virtual void setAHeld(bool held) = 0;

    /// A-hold threshold in frames (≈250 ms @ 60 fps).
    static constexpr int kAHoldThreshold = 15;

    /// Frames A has been held (useful for showing a hold-progress indicator).
    virtual int getAHoldFrames() const = 0;

    /// Feed the current B-button pressed-this-frame state.
    virtual void setBPressed(bool pressed) = 0;

    /// Get current drag/drop mode.
    virtual EditMode getEditMode() const = 0;

    /// Index of the tile currently being dragged, or -1.
    virtual int getDraggedTileIndex() const = 0;

    /// Original grid position of dragged tile (valid while Drag mode).
    virtual int getDragOriginX() const = 0;
    virtual int getDragOriginY() const = 0;

    /// Preview (cursor) position for the drag destination.
    virtual int getDragDestX() const = 0;
    virtual int getDragDestY() const = 0;

    /// True if the current drag destination is a valid drop target.
    virtual bool isDragDestValid() const = 0;

    /// Move the drag destination by (dx, dy); validates bounds + collision.
    virtual void moveDragDest(int dx, int dy,
                               const model::Workspace& workspace) = 0;

    /// Commit or cancel the pending drop.
    virtual DropResult commitDrop(model::Workspace& workspace) = 0;
    virtual DropResult cancelDrop() = 0;

    // ── Legacy interface (kept for touch path) ───────────────────────────────
    virtual State getCurrentState() const = 0;
    virtual int   getSelectedTileIndex() const = 0;
    virtual void  reset() = 0;
};

/// Factory: create platform-specific input handler.
TileInputHandler* createTileInputHandler();

} // namespace desknx::menu::input

{"content":"#pragma once\n\n// Copyright contributors to the DeskNX project.\n// SPDX-License-Identifier: MIT\n\n#include <desknx/Model.hpp>\n#include <desknx/TouchGesture.hpp>\n#include <cstdint>\n\nnamespace desknx::menu::input {\n\n// Minimal touch gesture stub (matches existing code structure)\nnamespace gesture {\nstruct Touch {};\n} // namespace gesture\n\n/// Edit-mode state machine for drag/drop.\n///\n///   BROWSE ──(A held ≥15 frames)──► DRAG ──(A released, valid)──► BROWSE\n///                                        └──(A released, invalid)─► BROWSE  (bounce back)\n///                                        └──(B pressed)───────────► BROWSE  (cancel)\n///\nenum class EditMode {\n    Browse,  ///< Normal navigation; D-pad moves focus cursor.\n    Drag,    ///< Tile grabbed; D-pad moves the tile preview.\n};\n\n/// Result returned from commitDrop().\nenum class DropResult {\n    Committed,   ///< Move applied, workspace modified.\n    Invalid,     ///< Destination blocked; tile bounced back.\n    Cancelled,   ///< User cancelled (B button).\n    NothingDragged,\n};\n\n/// Handles drag, resize, and selection input on tiles.\nclass TileInputHandler {\npublic:\n    // Legacy state enum kept for backward compat (used by touch path).\n    enum class State { Idle, Selected, Dragging, Resizing };\n\n    struct ResizeEdge {\n        enum Type { None = 0, Right = 1, Bottom = 2, RightBottom = 3 };\n    };\n\n    virtual ~TileInputHandler() = default;\n\n    /// Update per-frame: increment hold timer, handle transitions. dt in seconds.\n    /// Returns true if the workspace was modified.\n    virtual bool update(model::Workspace& workspace, float dt) = 0;\n\n    /// Notify of touch event. Returns true if consumed.\n    virtual bool onTouchEvent(int screenX, int screenY, bool pressed,\n                              const gesture::Touch& gesture) = 0;\n\n    /// Notify of raw button press (HID bitmask). Returns true if consumed.\n    virtual bool onButtonPress(uint32_t buttonId) = 0;\n\n    /// Enter drag mode for the tile at index `tileIdx`, currently at (ox, oy).\n    /// Call when A-hold threshold is reached.\n    virtual void enterDrag(int tileIdx, int ox, int oy) = 0;\n\n    /// Feed the current A-button held state each frame.\n    virtual void setAHeld(bool held) = 0;\n\n    /// A-hold threshold in frames (≈250 ms @ 60 fps).\n    static constexpr int kAHoldThreshold = 15;\n\n    /// Frames A has been held (useful for showing a hold-progress indicator).\n    virtual int getAHoldFrames() const = 0;\n\n    /// Feed the current B-button pressed-this-frame state.\n    virtual void setBPressed(bool pressed) = 0;\n\n    /// Get current drag/drop mode.\n    virtual EditMode getEditMode() const = 0;\n\n    /// Index of the tile currently being dragged, or -1.\n    virtual int getDraggedTileIndex() const = 0;\n\n    /// Original grid position of dragged tile (valid while Drag mode).\n    virtual int getDragOriginX() const = 0;\n    virtual int getDragOriginY() const = 0;\n\n    /// Preview (cursor) position for the drag destination.\n    virtual int getDragDestX() const = 0;\n    virtual int getDragDestY() const = 0;\n\n    /// True if the current drag destination is a valid drop target.\n    virtual bool isDragDestValid() const = 0;\n\n    /// Move the drag destination by (dx, dy); validates bounds + collision.\n    virtual void moveDragDest(int dx, int dy,\n                               const model::Workspace& workspace) = 0;\n\n    /// Commit or cancel the pending drop.\n    virtual DropResult commitDrop(model::Workspace& workspace) = 0;\n    virtual DropResult cancelDrop() = 0;\n\n    // ── Legacy interface (kept for touch path) ───────────────────────────────\n    virtual State getCurrentState() const = 0;\n    virtual int   getSelectedTileIndex() const = 0;\n    virtual void  reset() = 0;\n};\n\n/// Factory: create platform-specific input handler.\nTileInputHandler* createTileInputHandler();\n\n} // namespace desknx::menu::input\n"}