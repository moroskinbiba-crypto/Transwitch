#include "MenuActivity.hpp"
#include <algorithm>

namespace desknx::nxui {

MenuActivity::MenuActivity()
    : m_provider(nullptr), m_focusX(0), m_focusY(0), m_elapsedTime(0.0f) {
    // Initialize with an empty projection
    m_activeProjection = homenx::WorkspaceProjection{};
}

void MenuActivity::setWorkspaceProvider(homenx::WorkspaceProvider* provider) {
    m_provider = provider;
}

void MenuActivity::update(float dt) {
    m_elapsedTime += dt;

    if (!m_provider) return;

    // Pull active workspace and project it
    homenx::ProjectionError error;
    if (!m_provider->projectActive(m_activeProjection, &error)) {
        // Projection failed; keep the old one or leave it empty
        return;
    }

    // Clamp focus to valid grid bounds
    m_focusX = std::clamp(m_focusX, 0, COLS - 1);
    m_focusY = std::clamp(m_focusY, 0, ROWS - 1);
}

void MenuActivity::render(SDL_Renderer* renderer) {
    if (!renderer) return;

    // Clear background to black
    SDL_SetRenderDrawColor(renderer, 10, 14, 22, 255);  // BG color from main.cpp
    SDL_RenderClear(renderer);

    // Draw grid lines and cells
    drawGridLines(renderer);

    // Draw tiles from the projection
    for (const auto& entry : m_activeProjection.entries) {
        drawTile(renderer, entry);
    }

    // Draw focus highlight
    {
        const int cellScreenX = PADDING_X + m_focusX * CELL_WIDTH;
        const int cellScreenY = PADDING_Y + m_focusY * CELL_HEIGHT;

        // Find the tile that occupies this cell (if any)
        const homenx::Cell& cell =
            m_activeProjection.cells[m_focusY * COLS + m_focusX];
        bool isEmpty = (cell.role == homenx::CellRole::Empty ||
                        cell.entryIndex < 0);

        // Draw a colored border for focus
        SDL_SetRenderDrawColor(renderer, 83, 157, 255, 255);  // BLUE
        for (int i = 0; i < 4; i++) {
            SDL_Rect highlight{cellScreenX - i, cellScreenY - i,
                               CELL_WIDTH + 2 * i, CELL_HEIGHT + 2 * i};
            SDL_RenderDrawRect(renderer, &highlight);
        }
    }

    SDL_RenderPresent(renderer);
}

void MenuActivity::drawGridLines(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawColor(renderer, 145, 158, 180, 100);  // MUTED with alpha

    // Vertical lines
    for (int x = 0; x <= COLS; x++) {
        const int screenX = PADDING_X + x * CELL_WIDTH;
        SDL_RenderDrawLine(renderer, screenX, PADDING_Y,
                           screenX, PADDING_Y + ROWS * CELL_HEIGHT);
    }

    // Horizontal lines
    for (int y = 0; y <= ROWS; y++) {
        const int screenY = PADDING_Y + y * CELL_HEIGHT;
        SDL_RenderDrawLine(renderer, PADDING_X, screenY,
                           PADDING_X + COLS * CELL_WIDTH, screenY);
    }
}

void MenuActivity::drawTile(SDL_Renderer* renderer,
                             const homenx::Entry& entry) const {
    // Draw a rectangle for each tile, sized by its grid dimensions
    const int cellScreenX = PADDING_X + entry.x * CELL_WIDTH;
    const int cellScreenY = PADDING_Y + entry.y * CELL_HEIGHT;
    const int tileWidth = entry.columns * CELL_WIDTH - 8;  // Small gap
    const int tileHeight = entry.rows * CELL_HEIGHT - 8;

    // Use the tile's color to select from palette
    std::array<SDL_Color, 8> tileColors{{
        {91, 145, 226, 255},   // 0: blue
        {53, 169, 151, 255},   // 1: teal
        {155, 116, 226, 255},  // 2: purple
        {222, 151, 83, 255},   // 3: orange
        {218, 105, 133, 255},  // 4: pink
        {79, 169, 206, 255},   // 5: cyan
        {147, 171, 94, 255},   // 6: green
        {113, 130, 158, 255}   // 7: gray
    }};

    int colorIndex = std::clamp(entry.color, 0, 7);
    const auto& color = tileColors[colorIndex];

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect tileRect{cellScreenX + 4, cellScreenY + 4, tileWidth, tileHeight};
    SDL_RenderFillRect(renderer, &tileRect);

    // Draw a border
    SDL_SetRenderDrawColor(renderer, color.r / 2, color.g / 2, color.b / 2, 200);
    SDL_RenderDrawRect(renderer, &tileRect);
}

void MenuActivity::moveFocus(int dx, int dy) {
    // TODO: Implement D-pad focus movement with focus neighbor logic
    // For now, this is just a stub.
    // The logic should use homenx::focusNeighbour() to respect tile geometry.
    m_focusX += dx;
    m_focusY += dy;
    m_focusX = std::clamp(m_focusX, 0, COLS - 1);
    m_focusY = std::clamp(m_focusY, 0, ROWS - 1);
}

void MenuActivity::switchWorkspace(int direction) {
    // TODO: Implement workspace switching
    // direction: 1 = next, -1 = previous
    // Call m_provider->next() or m_provider->previous()
    if (!m_provider) return;

    if (direction > 0) {
        m_provider->next();
    } else if (direction < 0) {
        m_provider->previous();
    }
}

} // namespace desknx::nxui

{"content":"#include \"MenuActivity.hpp\"\n#include <algorithm>\n\nnamespace desknx::nxui {\n\nMenuActivity::MenuActivity()\n    : m_provider(nullptr), m_focusX(0), m_focusY(0), m_elapsedTime(0.0f) {\n    // Initialize with an empty projection\n    m_activeProjection = homenx::WorkspaceProjection{};\n}\n\nvoid MenuActivity::setWorkspaceProvider(homenx::WorkspaceProvider* provider) {\n    m_provider = provider;\n}\n\nvoid MenuActivity::update(float dt) {\n    m_elapsedTime += dt;\n\n    if (!m_provider) return;\n\n    // Pull active workspace and project it\n    homenx::ProjectionError error;\n    if (!m_provider->projectActive(m_activeProjection, &error)) {\n        // Projection failed; keep the old one or leave it empty\n        return;\n    }\n\n    // Clamp focus to valid grid bounds\n    m_focusX = std::clamp(m_focusX, 0, COLS - 1);\n    m_focusY = std::clamp(m_focusY, 0, ROWS - 1);\n}\n\nvoid MenuActivity::render(SDL_Renderer* renderer) {\n    if (!renderer) return;\n\n    // Clear background to black\n    SDL_SetRenderDrawColor(renderer, 10, 14, 22, 255);  // BG color from main.cpp\n    SDL_RenderClear(renderer);\n\n    // Draw grid lines and cells\n    drawGridLines(renderer);\n\n    // Draw tiles from the projection\n    for (const auto& entry : m_activeProjection.entries) {\n        drawTile(renderer, entry);\n    }\n\n    // Draw focus highlight\n    {\n        const int cellScreenX = PADDING_X + m_focusX * CELL_WIDTH;\n        const int cellScreenY = PADDING_Y + m_focusY * CELL_HEIGHT;\n\n        // Find the tile that occupies this cell (if any)\n        const homenx::Cell& cell =\n            m_activeProjection.cells[m_focusY * COLS + m_focusX];\n        bool isEmpty = (cell.role == homenx::CellRole::Empty ||\n                        cell.entryIndex < 0);\n\n        // Draw a colored border for focus\n        SDL_SetRenderDrawColor(renderer, 83, 157, 255, 255);  // BLUE\n        for (int i = 0; i < 4; i++) {\n            SDL_Rect highlight{cellScreenX - i, cellScreenY - i,\n                               CELL_WIDTH + 2 * i, CELL_HEIGHT + 2 * i};\n            SDL_RenderDrawRect(renderer, &highlight);\n        }\n    }\n\n    SDL_RenderPresent(renderer);\n}\n\nvoid MenuActivity::drawGridLines(SDL_Renderer* renderer) const {\n    SDL_SetRenderDrawColor(renderer, 145, 158, 180, 100);  // MUTED with alpha\n\n    // Vertical lines\n    for (int x = 0; x <= COLS; x++) {\n        const int screenX = PADDING_X + x * CELL_WIDTH;\n        SDL_RenderDrawLine(renderer, screenX, PADDING_Y,\n                           screenX, PADDING_Y + ROWS * CELL_HEIGHT);\n    }\n\n    // Horizontal lines\n    for (int y = 0; y <= ROWS; y++) {\n        const int screenY = PADDING_Y + y * CELL_HEIGHT;\n        SDL_RenderDrawLine(renderer, PADDING_X, screenY,\n                           PADDING_X + COLS * CELL_WIDTH, screenY);\n    }\n}\n\nvoid MenuActivity::drawTile(SDL_Renderer* renderer,\n                             const homenx::Entry& entry) const {\n    // Draw a rectangle for each tile, sized by its grid dimensions\n    const int cellScreenX = PADDING_X + entry.x * CELL_WIDTH;\n    const int cellScreenY = PADDING_Y + entry.y * CELL_HEIGHT;\n    const int tileWidth = entry.columns * CELL_WIDTH - 8;  // Small gap\n    const int tileHeight = entry.rows * CELL_HEIGHT - 8;\n\n    // Use the tile's color to select from palette\n    std::array<SDL_Color, 8> tileColors{{\n        {91, 145, 226, 255},   // 0: blue\n        {53, 169, 151, 255},   // 1: teal\n        {155, 116, 226, 255},  // 2: purple\n        {222, 151, 83, 255},   // 3: orange\n        {218, 105, 133, 255},  // 4: pink\n        {79, 169, 206, 255},   // 5: cyan\n        {147, 171, 94, 255},   // 6: green\n        {113, 130, 158, 255}   // 7: gray\n    }};\n\n    int colorIndex = std::clamp(entry.color, 0, 7);\n    const auto& color = tileColors[colorIndex];\n\n    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);\n    SDL_Rect tileRect{cellScreenX + 4, cellScreenY + 4, tileWidth, tileHeight};\n    SDL_RenderFillRect(renderer, &tileRect);\n\n    // Draw a border\n    SDL_SetRenderDrawColor(renderer, color.r / 2, color.g / 2, color.b / 2, 200);\n    SDL_RenderDrawRect(renderer, &tileRect);\n}\n\nvoid MenuActivity::moveFocus(int dx, int dy) {\n    // TODO: Implement D-pad focus movement with focus neighbor logic\n    // For now, this is just a stub.\n    // The logic should use homenx::focusNeighbour() to respect tile geometry.\n    m_focusX += dx;\n    m_focusY += dy;\n    m_focusX = std::clamp(m_focusX, 0, COLS - 1);\n    m_focusY = std::clamp(m_focusY, 0, ROWS - 1);\n}\n\nvoid MenuActivity::switchWorkspace(int direction) {\n    // TODO: Implement workspace switching\n    // direction: 1 = next, -1 = previous\n    // Call m_provider->next() or m_provider->previous()\n    if (!m_provider) return;\n\n    if (direction > 0) {\n        m_provider->next();\n    } else if (direction < 0) {\n        m_provider->previous();\n    }\n}\n\n} // namespace desknx::nxui\n"}