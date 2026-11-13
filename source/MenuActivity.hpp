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

{"content":"#pragma once\n\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n#include <desknx/HomeNXWorkspaceAdapter.hpp>\n#include <desknx/Model.hpp>\n\n#include <SDL2/SDL.h>\n\nnamespace desknx::nxui {\n\n// MenuActivity renders the workspace grid on nxui using SDL2.\n// This is the native frontend that displays the Android-style home screen\n// with independent workspaces, per-desk appearance, drawer, and dock.\n//\n// Grid layout: 6 columns × 3 rows on 1280×720 display.\n// Cell dimensions: ~96 pixels per column, ~120 pixels per row (with spacing).\nclass MenuActivity {\npublic:\n    explicit MenuActivity();\n    ~MenuActivity() = default;\n\n    // Set the workspace provider. Must be called before update/render.\n    void setWorkspaceProvider(homenx::WorkspaceProvider* provider);\n\n    // Update state: pull active workspace and project it.\n    void update(float dt);\n\n    // Render the grid to the SDL2 renderer.\n    void render(SDL_Renderer* renderer);\n\nprivate:\n    static constexpr int GRID_WIDTH = 1280;\n    static constexpr int GRID_HEIGHT = 720;\n    static constexpr int COLS = model::Columns;  // 6\n    static constexpr int ROWS = model::Rows;     // 3\n    static constexpr int CELL_WIDTH = 160;       // ~96 usable + 64 spacing\n    static constexpr int CELL_HEIGHT = 200;      // ~120 usable + 80 spacing\n    static constexpr int PADDING_X = 40;\n    static constexpr int PADDING_Y = 40;\n\n    homenx::WorkspaceProvider* m_provider = nullptr;\n    homenx::WorkspaceProjection m_activeProjection;\n    int m_focusX = 0;\n    int m_focusY = 0;\n    float m_elapsedTime = 0.0f;\n\n    // Helper methods for rendering\n    void drawCell(SDL_Renderer* renderer, int cellX, int cellY, int width, int height,\n                  bool isFocused, bool isEmpty) const;\n    void drawTile(SDL_Renderer* renderer, const homenx::Entry& entry) const;\n    void drawGridLines(SDL_Renderer* renderer) const;\n\n    // Input stubs for future implementation\n    void moveFocus(int dx, int dy);\n    void switchWorkspace(int direction);  // 1 = next, -1 = previous\n};\n\n} // namespace desknx::nxui\n"}