#pragma once

#include <desknx/HomeNXWorkspaceProvider.hpp>
#include <desknx/HomeNXWorkspaceAdapter.hpp>
#include <desknx/Model.hpp>

#include <SDL2/SDL.h>

namespace desknx::nxui {

// MenuActivity renders the workspace grid on nxui using SDL2.
// This is the native frontend that displays the Android-style home screen
// with independent workspaces, per-desk appearance, drawer, and dock.
//
// Grid layout: 6 columns × 3 rows on 1280×720 display.
// Cell dimensions: ~96 pixels per column, ~120 pixels per row (with spacing).
class MenuActivity {
public:
    explicit MenuActivity();
    ~MenuActivity() = default;

    // Set the workspace provider. Must be called before update/render.
    void setWorkspaceProvider(homenx::WorkspaceProvider* provider);

    // Update state: pull active workspace and project it.
    void update(float dt);

    // Render the grid to the SDL2 renderer.
    void render(SDL_Renderer* renderer);

private:
    static constexpr int GRID_WIDTH = 1280;
    static constexpr int GRID_HEIGHT = 720;
    static constexpr int COLS = model::Columns;  // 6
    static constexpr int ROWS = model::Rows;     // 3
    static constexpr int CELL_WIDTH = 160;       // ~96 usable + 64 spacing
    static constexpr int CELL_HEIGHT = 200;      // ~120 usable + 80 spacing
    static constexpr int PADDING_X = 40;
    static constexpr int PADDING_Y = 40;

    homenx::WorkspaceProvider* m_provider = nullptr;
    homenx::WorkspaceProjection m_activeProjection;
    int m_focusX = 0;
    int m_focusY = 0;
    float m_elapsedTime = 0.0f;

    // Helper methods for rendering
    void drawCell(SDL_Renderer* renderer, int cellX, int cellY, int width, int height,
                  bool isFocused, bool isEmpty) const;
    void drawTile(SDL_Renderer* renderer, const homenx::Entry& entry) const;
    void drawGridLines(SDL_Renderer* renderer) const;

    // Input stubs for future implementation
    void moveFocus(int dx, int dy);
    void switchWorkspace(int direction);  // 1 = next, -1 = previous
};

} // namespace desknx::nxui
