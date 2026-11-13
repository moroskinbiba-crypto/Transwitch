#include <switch.h>
#include <SDL2/SDL.h>
#include <desknx/Model.hpp>
#include <desknx/HomeNXWorkspaceProvider.hpp>
#include <desknx/HomeNXWorkspaceFileStore.hpp>

#include "GridRenderer.hpp"
#include "TileInputHandler.hpp"
#include "WidgetFactory.hpp"
#include "LayoutStore.hpp"

#include <memory>
#include <cstring>

using namespace desknx;

namespace {

// Global renderer and input handler
std::unique_ptr<menu::renderer::GridRenderer> g_renderer;
std::unique_ptr<menu::input::TileInputHandler> g_inputHandler;
std::unique_ptr<homenx::WorkspaceProvider> g_provider;

const char* kLayoutPath = "sdmc:/switch/DeskNX/layout.tsv";

} // namespace

int main(int argc, char* argv[]) {
    // Initialize Switch services
    socketInitializeDefault();
    
    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return 1;
    }

    // Create window and renderer
    SDL_Window* window = SDL_CreateWindow(
        "DeskNX Menu",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN);

    if (!window) {
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize workspace provider
    g_provider = std::make_unique<homenx::WorkspaceProvider>();
    
    // Load layout from SD card
    if (!menu::storage::LayoutStore::load(*g_provider)) {
        // No layout file; create a default workspace
        g_provider->addWorkspace("Home");
    }

    // Create renderer and input handler
    g_renderer = std::unique_ptr<menu::renderer::GridRenderer>(
        menu::renderer::createGridRenderer(renderer));
    g_inputHandler = std::unique_ptr<menu::input::TileInputHandler>(
        menu::input::createTileInputHandler());

    // Add some default tiles if workspace is empty
    if (auto* active = g_provider->activeWorkspaceForEditing()) {
        if (active->tiles.empty()) {
            // Add a clock widget
            model::Tile clock = menu::widgets::WidgetFactory::createWidgetTile(
                model::Kind::Clock, 2, 1);
            clock.x = 0;
            clock.y = 0;
            if (model::placeable(*active, clock, 0, 0)) {
                active->tiles.push_back(std::move(clock));
            }
            
            // Add a battery widget
            model::Tile battery = menu::widgets::WidgetFactory::createWidgetTile(
                model::Kind::Battery, 2, 1);
            battery.x = 2;
            battery.y = 0;
            if (model::placeable(*active, battery, 2, 0)) {
                active->tiles.push_back(std::move(battery));
            }
            
            // Add a recent games widget
            model::Tile recent = menu::widgets::WidgetFactory::createWidgetTile(
                model::Kind::Recent, 3, 2);
            recent.x = 0;
            recent.y = 1;
            if (model::placeable(*active, recent, 0, 1)) {
                active->tiles.push_back(std::move(recent));
            }
        }
    }

    // Main loop
    bool running = true;
    Uint32 lastTime = SDL_GetTicks();
    Uint32 lastWidgetUpdate = lastTime;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float dt = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN:
                    // Map keyboard to button IDs for testing
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        // TODO: Map other keys to HID buttons (D-pad, A, B, etc.)
                        default:
                            break;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN: {
                    menu::input::TileInputHandler::State state = g_inputHandler->getCurrentState();
                    g_inputHandler->onTouchEvent(
                        event.button.x, event.button.y, true, {});
                    break;
                }

                case SDL_MOUSEBUTTONUP: {
                    g_inputHandler->onTouchEvent(
                        event.button.x, event.button.y, false, {});
                    break;
                }

                case SDL_MOUSEMOTION: {
                    // Update hover state
                    if (g_renderer) {
                        int gridX = g_renderer->screenToGridX(event.motion.x);
                        int gridY = g_renderer->screenToGridY(event.motion.y);
                        
                        auto* workspace = g_provider->activeWorkspace();
                        if (workspace) {
                            int tileIdx = model::itemAt(*workspace, gridX, gridY);
                            g_renderer->setHoveredTile(tileIdx);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Update input and workspace
        if (auto* workspace = g_provider->activeWorkspaceForEditing()) {
            g_inputHandler->update(*workspace, dt);
        }

        // Update widgets every second
        if (currentTime - lastWidgetUpdate >= 1000) {
            lastWidgetUpdate = currentTime;
            // TODO: Update widget data (clock, battery, recent games)
        }

        // Render
        if (auto* workspace = g_provider->activeWorkspace()) {
            g_renderer->render(*workspace);
        }

        SDL_Delay(16);  // ~60 FPS
    }

    // Save layout before exiting
    menu::storage::LayoutStore::save(*g_provider);

    // Cleanup
    g_inputHandler.reset();
    g_renderer.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    socketExit();

    return 0;
}

{"content":"#include <switch.h>\n#include <SDL2/SDL.h>\n#include <desknx/Model.hpp>\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n#include <desknx/HomeNXWorkspaceFileStore.hpp>\n\n#include \"GridRenderer.hpp\"\n#include \"TileInputHandler.hpp\"\n#include \"WidgetFactory.hpp\"\n#include \"LayoutStore.hpp\"\n\n#include <memory>\n#include <cstring>\n\nusing namespace desknx;\n\nnamespace {\n\n// Global renderer and input handler\nstd::unique_ptr<menu::renderer::GridRenderer> g_renderer;\nstd::unique_ptr<menu::input::TileInputHandler> g_inputHandler;\nstd::unique_ptr<homenx::WorkspaceProvider> g_provider;\n\nconst char* kLayoutPath = \"sdmc:/switch/DeskNX/layout.tsv\";\n\n} // namespace\n\nint main(int argc, char* argv[]) {\n    // Initialize Switch services\n    socketInitializeDefault();\n    \n    // Initialize SDL2\n    if (SDL_Init(SDL_INIT_VIDEO) < 0) {\n        return 1;\n    }\n\n    // Create window and renderer\n    SDL_Window* window = SDL_CreateWindow(\n        \"DeskNX Menu\",\n        SDL_WINDOWPOS_CENTERED,\n        SDL_WINDOWPOS_CENTERED,\n        1280, 720,\n        SDL_WINDOW_SHOWN);\n\n    if (!window) {\n        SDL_Quit();\n        return 1;\n    }\n\n    SDL_Renderer* renderer = SDL_CreateRenderer(\n        window, -1,\n        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n\n    if (!renderer) {\n        SDL_DestroyWindow(window);\n        SDL_Quit();\n        return 1;\n    }\n\n    // Initialize workspace provider\n    g_provider = std::make_unique<homenx::WorkspaceProvider>();\n    \n    // Load layout from SD card\n    if (!menu::storage::LayoutStore::load(*g_provider)) {\n        // No layout file; create a default workspace\n        g_provider->addWorkspace(\"Home\");\n    }\n\n    // Create renderer and input handler\n    g_renderer = std::unique_ptr<menu::renderer::GridRenderer>(\n        menu::renderer::createGridRenderer(renderer));\n    g_inputHandler = std::unique_ptr<menu::input::TileInputHandler>(\n        menu::input::createTileInputHandler());\n\n    // Add some default tiles if workspace is empty\n    if (auto* active = g_provider->activeWorkspaceForEditing()) {\n        if (active->tiles.empty()) {\n            // Add a clock widget\n            model::Tile clock = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Clock, 2, 1);\n            clock.x = 0;\n            clock.y = 0;\n            if (model::placeable(*active, clock, 0, 0)) {\n                active->tiles.push_back(std::move(clock));\n            }\n            \n            // Add a battery widget\n            model::Tile battery = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Battery, 2, 1);\n            battery.x = 2;\n            battery.y = 0;\n            if (model::placeable(*active, battery, 2, 0)) {\n                active->tiles.push_back(std::move(battery));\n            }\n            \n            // Add a recent games widget\n            model::Tile recent = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Recent, 3, 2);\n            recent.x = 0;\n            recent.y = 1;\n            if (model::placeable(*active, recent, 0, 1)) {\n                active->tiles.push_back(std::move(recent));\n            }\n        }\n    }\n\n    // Main loop\n    bool running = true;\n    Uint32 lastTime = SDL_GetTicks();\n    Uint32 lastWidgetUpdate = lastTime;\n\n    while (running) {\n        Uint32 currentTime = SDL_GetTicks();\n        float dt = (currentTime - lastTime) / 1000.0f;\n        lastTime = currentTime;\n\n        SDL_Event event;\n        while (SDL_PollEvent(&event)) {\n            switch (event.type) {\n                case SDL_QUIT:\n                    running = false;\n                    break;\n\n                case SDL_KEYDOWN:\n                    // Map keyboard to button IDs for testing\n                    switch (event.key.keysym.sym) {\n                        case SDLK_ESCAPE:\n                            running = false;\n                            break;\n                        // TODO: Map other keys to HID buttons (D-pad, A, B, etc.)\n                        default:\n                            break;\n                    }\n                    break;\n\n                case SDL_MOUSEBUTTONDOWN: {\n                    menu::input::TileInputHandler::State state = g_inputHandler->getCurrentState();\n                    g_inputHandler->onTouchEvent(\n                        event.button.x, event.button.y, true, {});\n                    break;\n                }\n\n                case SDL_MOUSEBUTTONUP: {\n                    g_inputHandler->onTouchEvent(\n                        event.button.x, event.button.y, false, {});\n                    break;\n                }\n\n                case SDL_MOUSEMOTION: {\n                    // Update hover state\n                    if (g_renderer) {\n                        int gridX = g_renderer->screenToGridX(event.motion.x);\n                        int gridY = g_renderer->screenToGridY(event.motion.y);\n                        \n                        auto* workspace = g_provider->activeWorkspace();\n                        if (workspace) {\n                            int tileIdx = model::itemAt(*workspace, gridX, gridY);\n                            g_renderer->setHoveredTile(tileIdx);\n                        }\n                    }\n                    break;\n                }\n\n                default:\n                    break;\n            }\n        }\n\n        // Update input and workspace\n        if (auto* workspace = g_provider->activeWorkspaceForEditing()) {\n            g_inputHandler->update(*workspace, dt);\n        }\n\n        // Update widgets every second\n        if (currentTime - lastWidgetUpdate >= 1000) {\n            lastWidgetUpdate = currentTime;\n            // TODO: Update widget data (clock, battery, recent games)\n        }\n\n        // Render\n        if (auto* workspace = g_provider->activeWorkspace()) {\n            g_renderer->render(*workspace);\n        }\n\n        SDL_Delay(16);  // ~60 FPS\n    }\n\n    // Save layout before exiting\n    menu::storage::LayoutStore::save(*g_provider);\n\n    // Cleanup\n    g_inputHandler.reset();\n    g_renderer.reset();\n    SDL_DestroyRenderer(renderer);\n    SDL_DestroyWindow(window);\n    SDL_Quit();\n\n    socketExit();\n\n    return 0;\n}\n"}