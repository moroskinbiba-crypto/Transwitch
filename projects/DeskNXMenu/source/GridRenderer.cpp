#include "GridRenderer.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <sstream>

namespace desknx::menu::renderer {

class GridRendererSDL2 : public GridRenderer {
public:
    GridRendererSDL2(SDL_Renderer* renderer) : m_renderer(renderer), m_hoveredTile(-1), m_draggedTile(-1) {}
    
    void render(const model::Workspace& workspace) override {
        // Clear background
        SDL_SetRenderDrawColor(m_renderer, 
                               (Colors::BackgroundDark >> 24) & 0xFF,
                               (Colors::BackgroundDark >> 16) & 0xFF,
                               (Colors::BackgroundDark >>  8) & 0xFF,
                               Colors::BackgroundDark & 0xFF);
        SDL_RenderClear(m_renderer);
        
        // Render each tile
        for (size_t i = 0; i < workspace.tiles.size(); ++i) {
            const model::Tile& tile = workspace.tiles[i];
            renderTile(tile, static_cast<int>(i), 
                      i == static_cast<size_t>(m_hoveredTile),
                      i == static_cast<size_t>(m_draggedTile));
        }
        
        // Present to screen
        SDL_RenderPresent(m_renderer);
    }
    
    void setHoveredTile(int tileIndex) override {
        m_hoveredTile = tileIndex;
    }
    
    void setDraggedTile(int tileIndex, int screenX, int screenY) override {
        m_draggedTile = tileIndex;
        m_dragScreenX = screenX;
        m_dragScreenY = screenY;
    }
    
    int screenToGridX(int screenX) const override {
        int gridX = (screenX - 20) / (GridLayout::TileWidth + GridLayout::PaddingX);
        return std::clamp(gridX, 0, GridLayout::GridColumns - 1);
    }
    
    int screenToGridY(int screenY) const override {
        int gridY = (screenY - 20) / (GridLayout::TileHeight + GridLayout::PaddingY);
        return std::clamp(gridY, 0, GridLayout::GridRows - 1);
    }
    
    void getTileBounds(int gridX, int gridY, int gridW, int gridH,
                       int& outX, int& outY, int& outW, int& outH) const override {
        outX = GridLayout::gridToScreenX(gridX);
        outY = GridLayout::gridToScreenY(gridY);
        outW = GridLayout::gridToScreenW(gridW);
        outH = GridLayout::gridToScreenH(gridH);
    }
    
private:
    SDL_Renderer* m_renderer;
    int m_hoveredTile;
    int m_draggedTile;
    int m_dragScreenX = 0;
    int m_dragScreenY = 0;
    
    void renderTile(const model::Tile& tile, int index, bool hovered, bool dragging) {
        int x = GridLayout::gridToScreenX(tile.x);
        int y = GridLayout::gridToScreenY(tile.y);
        int w = GridLayout::gridToScreenW(tile.w);
        int h = GridLayout::gridToScreenH(tile.h);
        
        // If dragging, draw at cursor position
        if (dragging) {
            x = m_dragScreenX - w / 2;
            y = m_dragScreenY - h / 2;
        }
        
        SDL_Rect rect{x, y, w, h};
        
        // Draw tile background
        uint32_t bgColor = hovered ? Colors::TileHover : Colors::TileBackground;
        if (dragging) bgColor = Colors::TileActive;
        
        SDL_SetRenderDrawColor(m_renderer,
                               (bgColor >> 24) & 0xFF,
                               (bgColor >> 16) & 0xFF,
                               (bgColor >>  8) & 0xFF,
                               bgColor & 0xFF);
        SDL_RenderFillRect(m_renderer, &rect);
        
        // Draw border
        SDL_SetRenderDrawColor(m_renderer,
                               (Colors::TileBorder >> 24) & 0xFF,
                               (Colors::TileBorder >> 16) & 0xFF,
                               (Colors::TileBorder >>  8) & 0xFF,
                               Colors::TileBorder & 0xFF);
        SDL_RenderDrawRect(m_renderer, &rect);
        
        // TODO: Render tile content (icon, title, widget data)
        // For now, just show the title as text
    }
};

// Factory function
GridRenderer* createGridRenderer(SDL_Renderer* renderer) {
    return new GridRendererSDL2(renderer);
}

} // namespace desknx::menu::renderer

{"content":"#include \"GridRenderer.hpp\"\n#include <SDL2/SDL.h>\n#include <algorithm>\n#include <sstream>\n\nnamespace desknx::menu::renderer {\n\nclass GridRendererSDL2 : public GridRenderer {\npublic:\n    GridRendererSDL2(SDL_Renderer* renderer) : m_renderer(renderer), m_hoveredTile(-1), m_draggedTile(-1) {}\n    \n    void render(const model::Workspace& workspace) override {\n        // Clear background\n        SDL_SetRenderDrawColor(m_renderer, \n                               (Colors::BackgroundDark >> 24) & 0xFF,\n                               (Colors::BackgroundDark >> 16) & 0xFF,\n                               (Colors::BackgroundDark >>  8) & 0xFF,\n                               Colors::BackgroundDark & 0xFF);\n        SDL_RenderClear(m_renderer);\n        \n        // Render each tile\n        for (size_t i = 0; i < workspace.tiles.size(); ++i) {\n            const model::Tile& tile = workspace.tiles[i];\n            renderTile(tile, static_cast<int>(i), \n                      i == static_cast<size_t>(m_hoveredTile),\n                      i == static_cast<size_t>(m_draggedTile));\n        }\n        \n        // Present to screen\n        SDL_RenderPresent(m_renderer);\n    }\n    \n    void setHoveredTile(int tileIndex) override {\n        m_hoveredTile = tileIndex;\n    }\n    \n    void setDraggedTile(int tileIndex, int screenX, int screenY) override {\n        m_draggedTile = tileIndex;\n        m_dragScreenX = screenX;\n        m_dragScreenY = screenY;\n    }\n    \n    int screenToGridX(int screenX) const override {\n        int gridX = (screenX - 20) / (GridLayout::TileWidth + GridLayout::PaddingX);\n        return std::clamp(gridX, 0, GridLayout::GridColumns - 1);\n    }\n    \n    int screenToGridY(int screenY) const override {\n        int gridY = (screenY - 20) / (GridLayout::TileHeight + GridLayout::PaddingY);\n        return std::clamp(gridY, 0, GridLayout::GridRows - 1);\n    }\n    \n    void getTileBounds(int gridX, int gridY, int gridW, int gridH,\n                       int& outX, int& outY, int& outW, int& outH) const override {\n        outX = GridLayout::gridToScreenX(gridX);\n        outY = GridLayout::gridToScreenY(gridY);\n        outW = GridLayout::gridToScreenW(gridW);\n        outH = GridLayout::gridToScreenH(gridH);\n    }\n    \nprivate:\n    SDL_Renderer* m_renderer;\n    int m_hoveredTile;\n    int m_draggedTile;\n    int m_dragScreenX = 0;\n    int m_dragScreenY = 0;\n    \n    void renderTile(const model::Tile& tile, int index, bool hovered, bool dragging) {\n        int x = GridLayout::gridToScreenX(tile.x);\n        int y = GridLayout::gridToScreenY(tile.y);\n        int w = GridLayout::gridToScreenW(tile.w);\n        int h = GridLayout::gridToScreenH(tile.h);\n        \n        // If dragging, draw at cursor position\n        if (dragging) {\n            x = m_dragScreenX - w / 2;\n            y = m_dragScreenY - h / 2;\n        }\n        \n        SDL_Rect rect{x, y, w, h};\n        \n        // Draw tile background\n        uint32_t bgColor = hovered ? Colors::TileHover : Colors::TileBackground;\n        if (dragging) bgColor = Colors::TileActive;\n        \n        SDL_SetRenderDrawColor(m_renderer,\n                               (bgColor >> 24) & 0xFF,\n                               (bgColor >> 16) & 0xFF,\n                               (bgColor >>  8) & 0xFF,\n                               bgColor & 0xFF);\n        SDL_RenderFillRect(m_renderer, &rect);\n        \n        // Draw border\n        SDL_SetRenderDrawColor(m_renderer,\n                               (Colors::TileBorder >> 24) & 0xFF,\n                               (Colors::TileBorder >> 16) & 0xFF,\n                               (Colors::TileBorder >>  8) & 0xFF,\n                               Colors::TileBorder & 0xFF);\n        SDL_RenderDrawRect(m_renderer, &rect);\n        \n        // TODO: Render tile content (icon, title, widget data)\n        // For now, just show the title as text\n    }\n};\n\n// Factory function\nGridRenderer* createGridRenderer(SDL_Renderer* renderer) {\n    return new GridRendererSDL2(renderer);\n}\n\n} // namespace desknx::menu::renderer\n"}