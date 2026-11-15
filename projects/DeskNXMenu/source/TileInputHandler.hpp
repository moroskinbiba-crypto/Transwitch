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
    Resize,  ///< Tile resizing; D-pad adjusts size.
    Marked,  ///< Tile marked for deletion; waiting for X again to confirm.
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

    /// Supported tile sizes: (w, h) pairs from tile-shape contract.
    static constexpr std::pair<int, int> SupportedSizes[] = {
        {1, 1}, {2, 1}, {3, 1}, {2, 2}, {3, 2}
    };
    static constexpr int NumSupportedSizes = 5;

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

    /// Enter resize mode for the tile at index `tileIdx`.
    /// Call when Y-button pressed on focused tile.
    virtual void enterResize(int tileIdx, const model::Workspace& workspace) = 0;

    /// Mark tile for deletion. Double X-press confirms.
    virtual void markForDelete(int tileIdx) = 0;

    /// Confirm deletion (second X-press within timeout).
    virtual bool confirmDelete(model::Workspace& workspace) = 0;

    /// Cancel mark/delete.
    virtual void cancelMark() = 0;

    /// Feed the current A-button held state each frame.
    virtual void setAHeld(bool held) = 0;

    /// A-hold threshold in frames (≈250 ms @ 60 fps).
    static constexpr int kAHoldThreshold = 15;

    /// Frames A has been held (useful for showing a hold-progress indicator).
    virtual int getAHoldFrames() const = 0;

    /// Notify Y-button pressed (to enter resize mode).
    virtual void setYPressed(bool pressed) = 0;

    /// Notify X-button pressed (to mark/delete tiles).
    virtual void setXPressed(bool pressed) = 0;

    /// Get current resize target tile, or -1.
    virtual int getResizeTileIndex() const = 0;

    /// Get current resize target dimensions (w, h).
    virtual std::pair<int, int> getResizeTargetSize() const = 0;

    /// Check if resize target is valid (no overlap/overflow).
    virtual bool isResizeValid() const = 0;

    /// Move to next/prev supported size in resize mode.
    virtual void cycleResizeSize(int delta, const model::Workspace& workspace) = 0;

    /// Commit or cancel resize.
    virtual bool commitResize(model::Workspace& workspace) = 0;
    virtual void cancelResize() = 0;

    /// Get marked-for-delete tile index, or -1.
    virtual int getMarkedTileIndex() const = 0;

    /// Get current drag/drop/resize/remove mode.
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
