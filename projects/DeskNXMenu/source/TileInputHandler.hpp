#pragma once

#include <desknx/Model.hpp>
#include <desknx/TouchGesture.hpp>
#include <cstdint>

// Minimal touch gesture stub
namespace desknx::gesture {
struct Touch { };
} // namespace desknx::gesture

namespace desknx::menu::input {

// Handles drag, resize, and selection input on tiles.
// Input state machine:
// - Idle: no tile selected
// - Selected: one tile highlighted, awaiting drag or A-button press
// - Dragging: tile moving under cursor
// - Resizing: tile being resized from edge

class TileInputHandler {
public:
    enum class State {
        Idle,
        Selected,
        Dragging,
        Resizing,
    };
    
    struct ResizeEdge {
        enum Type {
            None = 0,
            Right = 1,
            Bottom = 2,
            RightBottom = 3,
        };
    };
    
    virtual ~TileInputHandler() = default;
    
    /// Update input state: returns true if the workspace was modified.
    virtual bool update(model::Workspace& workspace, float dt) = 0;
    
    /// Notify of touch event. Returns true if consumed.
    virtual bool onTouchEvent(int screenX, int screenY, bool pressed, const gesture::Touch& gesture) = 0;
    
    /// Notify of button press. Returns true if consumed.
    virtual bool onButtonPress(uint32_t buttonId) = 0;
    
    /// Get current input state.
    virtual State getCurrentState() const = 0;
    
    /// Get the currently selected or dragged tile index (-1 if none).
    virtual int getSelectedTileIndex() const = 0;
    
    /// Reset input state (e.g., when changing workspaces).
    virtual void reset() = 0;
};

// Factory: create platform-specific input handler
TileInputHandler* createTileInputHandler();

} // namespace desknx::menu::input

{"content":"#pragma once\n\n#include <desknx/Model.hpp>\n#include <desknx/TouchGesture.hpp>\n#include <cstdint>\n\n// Minimal touch gesture stub\nnamespace desknx::gesture {\nstruct Touch { };\n} // namespace desknx::gesture\n\nnamespace desknx::menu::input {\n\n// Handles drag, resize, and selection input on tiles.\n// Input state machine:\n// - Idle: no tile selected\n// - Selected: one tile highlighted, awaiting drag or A-button press\n// - Dragging: tile moving under cursor\n// - Resizing: tile being resized from edge\n\nclass TileInputHandler {\npublic:\n    enum class State {\n        Idle,\n        Selected,\n        Dragging,\n        Resizing,\n    };\n    \n    struct ResizeEdge {\n        enum Type {\n            None = 0,\n            Right = 1,\n            Bottom = 2,\n            RightBottom = 3,\n        };\n    };\n    \n    virtual ~TileInputHandler() = default;\n    \n    /// Update input state: returns true if the workspace was modified.\n    virtual bool update(model::Workspace& workspace, float dt) = 0;\n    \n    /// Notify of touch event. Returns true if consumed.\n    virtual bool onTouchEvent(int screenX, int screenY, bool pressed, const gesture::Touch& gesture) = 0;\n    \n    /// Notify of button press. Returns true if consumed.\n    virtual bool onButtonPress(uint32_t buttonId) = 0;\n    \n    /// Get current input state.\n    virtual State getCurrentState() const = 0;\n    \n    /// Get the currently selected or dragged tile index (-1 if none).\n    virtual int getSelectedTileIndex() const = 0;\n    \n    /// Reset input state (e.g., when changing workspaces).\n    virtual void reset() = 0;\n};\n\n// Factory: create platform-specific input handler\nTileInputHandler* createTileInputHandler();\n\n} // namespace desknx::menu::input\n"}