#pragma once

#include <desknx/Model.hpp>
#include <cstdint>
#include <string>

struct SDL_Renderer;

namespace desknx::menu::renderer {

// Tile rendering constants for 1280×720 display (6×3 grid)
struct GridLayout {
    static constexpr int WindowWidth = 1280;
    static constexpr int WindowHeight = 720;
    static constexpr int GridColumns = 6;
    static constexpr int GridRows = 3;
    
    // Tile sizing: each 1×1 tile is ~200px wide and ~180px tall on a 6×3 grid
    static constexpr int TileWidth = 200;
    static constexpr int TileHeight = 180;
    static constexpr int PaddingX = 8;
    static constexpr int PaddingY = 8;
    static constexpr int CornerRadius = 12;
    
    static int gridToScreenX(int gridX) {
        return 20 + gridX * (TileWidth + PaddingX);
    }
    
    static int gridToScreenY(int gridY) {
        return 20 + gridY * (TileHeight + PaddingY);
    }
    
    static int gridToScreenW(int gridW) {
        return gridW * TileWidth + (gridW - 1) * PaddingX;
    }
    
    static int gridToScreenH(int gridH) {
        return gridH * TileHeight + (gridH - 1) * PaddingY;
    }
};

// Color palette for tiles
struct Colors {
    static constexpr uint32_t TileBackground = 0x2C2C2CFF;
    static constexpr uint32_t TileBorder = 0x404040FF;
    static constexpr uint32_t TileHover = 0x383838FF;
    static constexpr uint32_t TileActive = 0x4A4A4AFF;
    static constexpr uint32_t TextPrimary = 0xFFFFFFFF;
    static constexpr uint32_t TextSecondary = 0xB0B0B0FF;
    static constexpr uint32_t WidgetClock = 0x1E90FFFF;  // Dodger blue
    static constexpr uint32_t WidgetBattery = 0x32CD32FF; // Lime green
    static constexpr uint32_t WidgetRecent = 0xFF8C00FF;  // Dark orange
    static constexpr uint32_t BackgroundDark = 0x1A1A1AFF;
};

class GridRenderer {
public:
    virtual ~GridRenderer() = default;
    
    /// Render the entire grid to screen.
    virtual void render(const model::Workspace& workspace) = 0;
    
    /// Set the tile currently under the cursor (for hover effects).
    virtual void setHoveredTile(int tileIndex) = 0;
    
    /// Set the tile currently being dragged.
    virtual void setDraggedTile(int tileIndex, int screenX, int screenY) = 0;
    
    /// Convert screen coordinates to grid position. Returns -1 if out of bounds.
    virtual int screenToGridX(int screenX) const = 0;
    virtual int screenToGridY(int screenY) const = 0;
    
    /// Get the screen bounding box of a tile in grid coordinates.
    virtual void getTileBounds(int gridX, int gridY, int gridW, int gridH,
                               int& outX, int& outY, int& outW, int& outH) const = 0;
};

// Factory: create platform-specific renderer (SDL2 or nxui)
GridRenderer* createGridRenderer(SDL_Renderer* renderer);

} // namespace desknx::menu::renderer

{"content":"#pragma once\n\n#include <desknx/Model.hpp>\n#include <cstdint>\n#include <string>\n\nstruct SDL_Renderer;\n\nnamespace desknx::menu::renderer {\n\n// Tile rendering constants for 1280×720 display (6×3 grid)\nstruct GridLayout {\n    static constexpr int WindowWidth = 1280;\n    static constexpr int WindowHeight = 720;\n    static constexpr int GridColumns = 6;\n    static constexpr int GridRows = 3;\n    \n    // Tile sizing: each 1×1 tile is ~200px wide and ~180px tall on a 6×3 grid\n    static constexpr int TileWidth = 200;\n    static constexpr int TileHeight = 180;\n    static constexpr int PaddingX = 8;\n    static constexpr int PaddingY = 8;\n    static constexpr int CornerRadius = 12;\n    \n    static int gridToScreenX(int gridX) {\n        return 20 + gridX * (TileWidth + PaddingX);\n    }\n    \n    static int gridToScreenY(int gridY) {\n        return 20 + gridY * (TileHeight + PaddingY);\n    }\n    \n    static int gridToScreenW(int gridW) {\n        return gridW * TileWidth + (gridW - 1) * PaddingX;\n    }\n    \n    static int gridToScreenH(int gridH) {\n        return gridH * TileHeight + (gridH - 1) * PaddingY;\n    }\n};\n\n// Color palette for tiles\nstruct Colors {\n    static constexpr uint32_t TileBackground = 0x2C2C2CFF;\n    static constexpr uint32_t TileBorder = 0x404040FF;\n    static constexpr uint32_t TileHover = 0x383838FF;\n    static constexpr uint32_t TileActive = 0x4A4A4AFF;\n    static constexpr uint32_t TextPrimary = 0xFFFFFFFF;\n    static constexpr uint32_t TextSecondary = 0xB0B0B0FF;\n    static constexpr uint32_t WidgetClock = 0x1E90FFFF;  // Dodger blue\n    static constexpr uint32_t WidgetBattery = 0x32CD32FF; // Lime green\n    static constexpr uint32_t WidgetRecent = 0xFF8C00FF;  // Dark orange\n    static constexpr uint32_t BackgroundDark = 0x1A1A1AFF;\n};\n\nclass GridRenderer {\npublic:\n    virtual ~GridRenderer() = default;\n    \n    /// Render the entire grid to screen.\n    virtual void render(const model::Workspace& workspace) = 0;\n    \n    /// Set the tile currently under the cursor (for hover effects).\n    virtual void setHoveredTile(int tileIndex) = 0;\n    \n    /// Set the tile currently being dragged.\n    virtual void setDraggedTile(int tileIndex, int screenX, int screenY) = 0;\n    \n    /// Convert screen coordinates to grid position. Returns -1 if out of bounds.\n    virtual int screenToGridX(int screenX) const = 0;\n    virtual int screenToGridY(int screenY) const = 0;\n    \n    /// Get the screen bounding box of a tile in grid coordinates.\n    virtual void getTileBounds(int gridX, int gridY, int gridW, int gridH,\n                               int& outX, int& outY, int& outW, int& outH) const = 0;\n};\n\n// Factory: create platform-specific renderer (SDL2 or nxui)\nGridRenderer* createGridRenderer(SDL_Renderer* renderer);\n\n} // namespace desknx::menu::renderer\n"}