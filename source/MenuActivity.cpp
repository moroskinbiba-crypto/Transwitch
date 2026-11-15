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
