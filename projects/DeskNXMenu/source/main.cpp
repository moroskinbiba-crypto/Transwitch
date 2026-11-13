#include <switch.h>
#include <SDL2/SDL.h>
#include <desknx/Model.hpp>
#include <desknx/HomeNXWorkspaceProvider.hpp>
#include <desknx/HomeNXWorkspaceFileStore.hpp>

#include "GridRenderer.hpp"
#include "TileInputHandler.hpp"
#include "WidgetFactory.hpp"
#include "LayoutStore.hpp"
#include "SettingsScreen.hpp"

// Persistence layer includes
#include "../../../source/DataDirectory.hpp"
#include "../../../source/AppCatalogManager.hpp"
#include "../../../source/PersistenceStore.hpp"

#include <memory>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <cstdio>
#include <cerrno>

using namespace desknx;

namespace {

// Global renderer, input handler, persistence, and settings
std::unique_ptr<menu::renderer::GridRenderer> g_renderer;
std::unique_ptr<menu::input::TileInputHandler> g_inputHandler;
std::unique_ptr<homenx::WorkspaceProvider> g_provider;
std::unique_ptr<desknx::persistence::PersistenceStore> g_store;
std::unique_ptr<desknx::catalog::AppCatalogManager> g_catalog;
std::unique_ptr<menu::settings::SettingsScreen> g_settings;

const char* kLayoutPath = "sdmc:/switch/DeskNX/layout.tsv";
const char* kTitleID = "0100000000001000";
const char* kSaveDataPath = "sdmc:/01000000/00001000/";
const char* kDeskNXPath = "sdmc:/switch/DeskNX/";
const char* kDefaultLauncherMarker = "sdmc:/switch/DeskNX/.is_default_launcher";
const char* kConfigPath = "sdmc:/01000000/00001000/config.json";

// Create directory recursively (simple implementation)
bool mkdirRecursive(const char* path) {
    if (!path || strlen(path) == 0) return false;
    
    std::string pathStr(path);
    size_t pos = 0;
    
    while ((pos = pathStr.find('/', pos + 1)) != std::string::npos) {
        std::string dir = pathStr.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }
    
    return mkdir(pathStr.c_str(), 0755) == 0 || errno == EEXIST;
}

// Initialize directories and config
void initializeAppDirectories() {
    debugPrintf("DeskNX Title ID: %s\n", kTitleID);
    debugPrintf("Initializing DeskNX directories...\n");
    
    // Create save data directory
    if (mkdirRecursive(kSaveDataPath)) {
        debugPrintf("Created save data directory: %s\n", kSaveDataPath);
    } else {
        debugPrintf("Warning: Could not create save data directory: %s\n", kSaveDataPath);
    }
    
    // Create application data directory
    if (mkdirRecursive(kDeskNXPath)) {
        debugPrintf("Created application data directory: %s\n", kDeskNXPath);
    } else {
        debugPrintf("Warning: Could not create application data directory: %s\n", kDeskNXPath);
    }
    
    // Create initial config.json if it doesn't exist
    std::ifstream configTest(kConfigPath);
    if (!configTest.good()) {
        std::ofstream configFile(kConfigPath);
        if (configFile.is_open()) {
            configFile << "{\n";
            configFile << "  \"isDefaultLauncher\": false,\n";
            configFile << "  \"version\": \"0.2.0\",\n";
            configFile << "  \"titleId\": \"" << kTitleID << "\"\n";
            configFile << "}\n";
            configFile.close();
            debugPrintf("Created config file: %s\n", kConfigPath);
        } else {
            debugPrintf("Warning: Could not create config file: %s\n", kConfigPath);
        }
    } else {
        configTest.close();
        debugPrintf("Config file already exists: %s\n", kConfigPath);
    }
}

// Check if this applet is registered as the default launcher
bool isDefaultLauncherEnabled() {
    std::ifstream markerFile(kDefaultLauncherMarker);
    return markerFile.good();
}

// Toggle default launcher registration
void toggleDefaultLauncher() {
    if (isDefaultLauncherEnabled()) {
        // Disable: remove marker
        if (std::remove(kDefaultLauncherMarker) == 0) {
            debugPrintf("Default launcher registration disabled\n");
        }
    } else {
        // Enable: create marker
        std::ofstream markerFile(kDefaultLauncherMarker);
        if (markerFile.is_open()) {
            markerFile << "DeskNX is registered as the default launcher\n";
            markerFile.close();
            debugPrintf("Default launcher registration enabled\n");
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    // Initialize Switch services
    socketInitializeDefault();
    
    // Initialize directories and configuration
    initializeAppDirectories();
    
    // 1. Ensure data directory structure exists
    desknx::data::DataDirectory dataDir;
    if (!dataDir.ensureAll()) {
        debugPrintf("Warning: Failed to ensure data directory structure\n");
    } else {
        debugPrintf("Data directories created/verified successfully\n");
    }
    
    // 2. Discover apps from SD card
    g_catalog = std::make_unique<desknx::catalog::AppCatalogManager>();
    if (!g_catalog->scan()) {
        debugPrintf("Warning: Failed to scan app catalog\n");
    } else {
        debugPrintf("App catalog scan complete: %d games, %d homebrew\n",
                    g_catalog->gameCount(), g_catalog->homebrewCount());
    }
    
    // 3. Load persisted state
    g_store = std::make_unique<desknx::persistence::PersistenceStore>();
    if (!g_store->load()) {
        debugPrintf("Warning: Failed to load persisted state\n");
    } else {
        debugPrintf("Persisted state loaded successfully\n");
    }
    
    // Log default launcher status
    if (isDefaultLauncherEnabled()) {
        debugPrintf("DeskNX is registered as the default launcher\n");
    } else {
        debugPrintf("DeskNX is NOT the default launcher\n");
    }
    
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
    
    // 4. Populate workspaces from persisted layouts
    const auto& desktops = g_store->desktops();
    if (!desktops.empty()) {
        // Load workspaces from persistence
        for (const auto& desktop : desktops) {
            g_provider->addWorkspace(desktop.name);
        }
        debugPrintf("Loaded %zu workspaces from persistence\n", desktops.size());
    } else {
        // Load legacy layout or create default
        if (!menu::storage::LayoutStore::load(*g_provider)) {
            // No layout file; create a default workspace
            g_provider->addWorkspace("Home");
            debugPrintf("Created default workspace\n");
        }
    }

    // Create renderer, input handler, and settings screen
    g_renderer = std::unique_ptr<menu::renderer::GridRenderer>(
        menu::renderer::createGridRenderer(renderer));
    g_inputHandler = std::unique_ptr<menu::input::TileInputHandler>(
        menu::input::createTileInputHandler());
    g_settings = std::unique_ptr<menu::settings::SettingsScreen>(
        menu::settings::createSettingsScreen());

    // Initialise D-pad focus on the first occupied cell
    if (const auto* ws = g_provider->activeWorkspace()) {
        g_renderer->initFocus(*ws);
    }

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
    
    debugPrintf("Entering main loop\n");

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

                case SDL_KEYDOWN: {
                    // Feed A/B held state to the drag state machine.
                    const bool aKey = (event.key.keysym.sym == SDLK_RETURN);
                    const bool bKey = (event.key.keysym.sym == SDLK_BACKSPACE);
                    if (g_inputHandler) {
                        if (aKey) g_inputHandler->setAHeld(true);
                        if (bKey) g_inputHandler->setBPressed(true);
                    }

                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;

                        case SDLK_MINUS: // '-' opens/closes settings
                            if (!g_settings->isActive()) g_settings->show();
                            break;

                        // D-pad: settings get priority; in drag mode move destination;
                        //        otherwise move focus cursor.
                        case SDLK_UP:
                            if (g_settings->isActive()) {
                                g_settings->handleInput(0x1000);
                            } else if (g_renderer && g_inputHandler) {
                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {
                                    if (auto* ws = g_provider->activeWorkspace())
                                        g_inputHandler->moveDragDest(0, -1, *ws);
                                } else {
                                    g_renderer->moveFocus(desknx::touch::Direction::Up);
                                }
                            }
                            break;
                        case SDLK_DOWN:
                            if (g_settings->isActive()) {
                                g_settings->handleInput(0x2000);
                            } else if (g_renderer && g_inputHandler) {
                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {
                                    if (auto* ws = g_provider->activeWorkspace())
                                        g_inputHandler->moveDragDest(0, 1, *ws);
                                } else {
                                    g_renderer->moveFocus(desknx::touch::Direction::Down);
                                }
                            }
                            break;
                        case SDLK_LEFT:
                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {
                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {
                                    if (auto* ws = g_provider->activeWorkspace())
                                        g_inputHandler->moveDragDest(-1, 0, *ws);
                                } else {
                                    g_renderer->moveFocus(desknx::touch::Direction::Left);
                                }
                            }
                            break;
                        case SDLK_RIGHT:
                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {
                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {
                                    if (auto* ws = g_provider->activeWorkspace())
                                        g_inputHandler->moveDragDest(1, 0, *ws);
                                } else {
                                    g_renderer->moveFocus(desknx::touch::Direction::Right);
                                }
                            }
                            break;

                        // A — in Browse mode: start hold timer (drag entry handled
                        //     in per-frame block).  In Drag mode: handled there too.
                        case SDLK_RETURN:
                            if (g_settings->isActive()) g_settings->handleInput(0x0001);
                            break;

                        // B — cancel drag / close settings
                        case SDLK_BACKSPACE:
                            if (g_settings->isActive()) g_settings->handleInput(0x0002);
                            break;

                        default:
                            break;
                    }
                    break;
                }

                case SDL_KEYUP: {
                    if (event.key.keysym.sym == SDLK_RETURN && g_inputHandler) {
                        g_inputHandler->setAHeld(false);
                        // If we were in Drag mode, A-release commits the drop.
                        if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {
                            if (auto* ws = g_provider->activeWorkspaceForEditing()) {
                                auto result = g_inputHandler->commitDrop(*ws);
                                g_renderer->endDragPreview();
                                if (result == desknx::menu::input::DropResult::Committed) {
                                    if (g_store) g_store->save();
                                    debugPrintf("[Drop] committed & saved\n");
                                } else {
                                    debugPrintf("[Drop] invalid, bounced back\n");
                                }
                            }
                        }
                    }
                    if (event.key.keysym.sym == SDLK_BACKSPACE && g_inputHandler) {
                        g_inputHandler->setBPressed(false);
                    }
                    break;
                }

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

        // ── Per-frame drag/drop state machine ──────────────────────────────
        if (g_inputHandler && g_renderer && !g_settings->isActive()) {
            using desknx::menu::input::EditMode;
            using desknx::menu::input::DropResult;
            const Uint8* keys = SDL_GetKeyboardState(nullptr);

            if (g_inputHandler->getEditMode() == EditMode::Drag) {
                // B cancels drag
                if (keys[SDL_SCANCODE_BACKSPACE]) {
                    g_inputHandler->cancelDrop();
                    g_renderer->endDragPreview();
                    debugPrintf("[Drag] cancelled\n");
                }
                // Keep preview in sync
                if (g_renderer->isDragPreviewActive()) {
                    g_renderer->updateDragPreview(
                        g_inputHandler->getDragDestX(),
                        g_inputHandler->getDragDestY(),
                        g_inputHandler->isDragDestValid());
                }
            } else { // Browse mode
                // A held: increment counter; threshold → enter drag
                if (keys[SDL_SCANCODE_RETURN]) {
                    g_inputHandler->setAHeld(true);
                    if (g_inputHandler->getAHoldFrames() >= desknx::menu::input::TileInputHandler::kAHoldThreshold) {
                        if (auto* ws = g_provider->activeWorkspace()) {
                            int tidx = g_renderer->focusedTileIndex(*ws);
                            if (tidx >= 0) {
                                const auto& tile = ws->tiles[static_cast<size_t>(tidx)];
                                g_inputHandler->enterDrag(tidx, tile.x, tile.y);
                                g_renderer->beginDragPreview(
                                    tidx, tile.x, tile.y, tile.x, tile.y, true);
                            }
                        }
                    }
                } else {
                    g_inputHandler->setAHeld(false);
                }
            }
        }

        // Update widgets every second
        if (currentTime - lastWidgetUpdate >= 1000) {
            lastWidgetUpdate = currentTime;
            // TODO: Update widget data (clock, battery, recent games)
        }

        // Render grid
        if (auto* workspace = g_provider->activeWorkspace()) {
            // Refresh the renderer's workspace pointer (needed for moveFocus occupancy queries).
            g_renderer->setWorkspace(*workspace);
            g_renderer->render(*workspace);
        }

        // Render settings overlay on top (if active)
        if (g_settings && g_settings->isActive()) {
            g_settings->refresh();
            g_settings->render(renderer);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);  // ~60 FPS
    }

    // 5. On exit: save all persisted state atomically
    debugPrintf("Saving application state before exit\n");
    if (g_store && !g_store->save()) {
        debugPrintf("Warning: Failed to save persisted state\n");
    } else if (g_store) {
        debugPrintf("Persisted state saved successfully\n");
    }
    
    // Legacy layout save (backward compatibility)
    menu::storage::LayoutStore::save(*g_provider);

    // Cleanup
    g_settings.reset();
    g_store.reset();
    g_catalog.reset();
    g_inputHandler.reset();
    g_renderer.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    socketExit();
    debugPrintf("DeskNX Menu exited\n");

    return 0;
}

{"content":"#include <switch.h>\n#include <SDL2/SDL.h>\n#include <desknx/Model.hpp>\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n#include <desknx/HomeNXWorkspaceFileStore.hpp>\n\n#include \"GridRenderer.hpp\"\n#include \"TileInputHandler.hpp\"\n#include \"WidgetFactory.hpp\"\n#include \"LayoutStore.hpp\"\n#include \"SettingsScreen.hpp\"\n\n// Persistence layer includes\n#include \"../../../source/DataDirectory.hpp\"\n#include \"../../../source/AppCatalogManager.hpp\"\n#include \"../../../source/PersistenceStore.hpp\"\n\n#include <memory>\n#include <cstring>\n#include <fstream>\n#include <sys/stat.h>\n#include <sys/types.h>\n#include <iostream>\n#include <cstdio>\n#include <cerrno>\n\nusing namespace desknx;\n\nnamespace {\n\n// Global renderer, input handler, persistence, and settings\nstd::unique_ptr<menu::renderer::GridRenderer> g_renderer;\nstd::unique_ptr<menu::input::TileInputHandler> g_inputHandler;\nstd::unique_ptr<homenx::WorkspaceProvider> g_provider;\nstd::unique_ptr<desknx::persistence::PersistenceStore> g_store;\nstd::unique_ptr<desknx::catalog::AppCatalogManager> g_catalog;\nstd::unique_ptr<menu::settings::SettingsScreen> g_settings;\n\nconst char* kLayoutPath = \"sdmc:/switch/DeskNX/layout.tsv\";\nconst char* kTitleID = \"0100000000001000\";\nconst char* kSaveDataPath = \"sdmc:/01000000/00001000/\";\nconst char* kDeskNXPath = \"sdmc:/switch/DeskNX/\";\nconst char* kDefaultLauncherMarker = \"sdmc:/switch/DeskNX/.is_default_launcher\";\nconst char* kConfigPath = \"sdmc:/01000000/00001000/config.json\";\n\n// Create directory recursively (simple implementation)\nbool mkdirRecursive(const char* path) {\n    if (!path || strlen(path) == 0) return false;\n    \n    std::string pathStr(path);\n    size_t pos = 0;\n    \n    while ((pos = pathStr.find('/', pos + 1)) != std::string::npos) {\n        std::string dir = pathStr.substr(0, pos);\n        mkdir(dir.c_str(), 0755);\n    }\n    \n    return mkdir(pathStr.c_str(), 0755) == 0 || errno == EEXIST;\n}\n\n// Initialize directories and config\nvoid initializeAppDirectories() {\n    debugPrintf(\"DeskNX Title ID: %s\\n\", kTitleID);\n    debugPrintf(\"Initializing DeskNX directories...\\n\");\n    \n    // Create save data directory\n    if (mkdirRecursive(kSaveDataPath)) {\n        debugPrintf(\"Created save data directory: %s\\n\", kSaveDataPath);\n    } else {\n        debugPrintf(\"Warning: Could not create save data directory: %s\\n\", kSaveDataPath);\n    }\n    \n    // Create application data directory\n    if (mkdirRecursive(kDeskNXPath)) {\n        debugPrintf(\"Created application data directory: %s\\n\", kDeskNXPath);\n    } else {\n        debugPrintf(\"Warning: Could not create application data directory: %s\\n\", kDeskNXPath);\n    }\n    \n    // Create initial config.json if it doesn't exist\n    std::ifstream configTest(kConfigPath);\n    if (!configTest.good()) {\n        std::ofstream configFile(kConfigPath);\n        if (configFile.is_open()) {\n            configFile << \"{\\n\";\n            configFile << \"  \\\"isDefaultLauncher\\\": false,\\n\";\n            configFile << \"  \\\"version\\\": \\\"0.2.0\\\",\\n\";\n            configFile << \"  \\\"titleId\\\": \\\"\" << kTitleID << \"\\\"\\n\";\n            configFile << \"}\\n\";\n            configFile.close();\n            debugPrintf(\"Created config file: %s\\n\", kConfigPath);\n        } else {\n            debugPrintf(\"Warning: Could not create config file: %s\\n\", kConfigPath);\n        }\n    } else {\n        configTest.close();\n        debugPrintf(\"Config file already exists: %s\\n\", kConfigPath);\n    }\n}\n\n// Check if this applet is registered as the default launcher\nbool isDefaultLauncherEnabled() {\n    std::ifstream markerFile(kDefaultLauncherMarker);\n    return markerFile.good();\n}\n\n// Toggle default launcher registration\nvoid toggleDefaultLauncher() {\n    if (isDefaultLauncherEnabled()) {\n        // Disable: remove marker\n        if (std::remove(kDefaultLauncherMarker) == 0) {\n            debugPrintf(\"Default launcher registration disabled\\n\");\n        }\n    } else {\n        // Enable: create marker\n        std::ofstream markerFile(kDefaultLauncherMarker);\n        if (markerFile.is_open()) {\n            markerFile << \"DeskNX is registered as the default launcher\\n\";\n            markerFile.close();\n            debugPrintf(\"Default launcher registration enabled\\n\");\n        }\n    }\n}\n\n} // namespace\n\nint main(int argc, char* argv[]) {\n    // Initialize Switch services\n    socketInitializeDefault();\n    \n    // Initialize directories and configuration\n    initializeAppDirectories();\n    \n    // 1. Ensure data directory structure exists\n    desknx::data::DataDirectory dataDir;\n    if (!dataDir.ensureAll()) {\n        debugPrintf(\"Warning: Failed to ensure data directory structure\\n\");\n    } else {\n        debugPrintf(\"Data directories created/verified successfully\\n\");\n    }\n    \n    // 2. Discover apps from SD card\n    g_catalog = std::make_unique<desknx::catalog::AppCatalogManager>();\n    if (!g_catalog->scan()) {\n        debugPrintf(\"Warning: Failed to scan app catalog\\n\");\n    } else {\n        debugPrintf(\"App catalog scan complete: %d games, %d homebrew\\n\",\n                    g_catalog->gameCount(), g_catalog->homebrewCount());\n    }\n    \n    // 3. Load persisted state\n    g_store = std::make_unique<desknx::persistence::PersistenceStore>();\n    if (!g_store->load()) {\n        debugPrintf(\"Warning: Failed to load persisted state\\n\");\n    } else {\n        debugPrintf(\"Persisted state loaded successfully\\n\");\n    }\n    \n    // Log default launcher status\n    if (isDefaultLauncherEnabled()) {\n        debugPrintf(\"DeskNX is registered as the default launcher\\n\");\n    } else {\n        debugPrintf(\"DeskNX is NOT the default launcher\\n\");\n    }\n    \n    // Initialize SDL2\n    if (SDL_Init(SDL_INIT_VIDEO) < 0) {\n        return 1;\n    }\n\n    // Create window and renderer\n    SDL_Window* window = SDL_CreateWindow(\n        \"DeskNX Menu\",\n        SDL_WINDOWPOS_CENTERED,\n        SDL_WINDOWPOS_CENTERED,\n        1280, 720,\n        SDL_WINDOW_SHOWN);\n\n    if (!window) {\n        SDL_Quit();\n        return 1;\n    }\n\n    SDL_Renderer* renderer = SDL_CreateRenderer(\n        window, -1,\n        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n\n    if (!renderer) {\n        SDL_DestroyWindow(window);\n        SDL_Quit();\n        return 1;\n    }\n\n    // Initialize workspace provider\n    g_provider = std::make_unique<homenx::WorkspaceProvider>();\n    \n    // 4. Populate workspaces from persisted layouts\n    const auto& desktops = g_store->desktops();\n    if (!desktops.empty()) {\n        // Load workspaces from persistence\n        for (const auto& desktop : desktops) {\n            g_provider->addWorkspace(desktop.name);\n        }\n        debugPrintf(\"Loaded %zu workspaces from persistence\\n\", desktops.size());\n    } else {\n        // Load legacy layout or create default\n        if (!menu::storage::LayoutStore::load(*g_provider)) {\n            // No layout file; create a default workspace\n            g_provider->addWorkspace(\"Home\");\n            debugPrintf(\"Created default workspace\\n\");\n        }\n    }\n\n    // Create renderer, input handler, and settings screen\n    g_renderer = std::unique_ptr<menu::renderer::GridRenderer>(\n        menu::renderer::createGridRenderer(renderer));\n    g_inputHandler = std::unique_ptr<menu::input::TileInputHandler>(\n        menu::input::createTileInputHandler());\n    g_settings = std::unique_ptr<menu::settings::SettingsScreen>(\n        menu::settings::createSettingsScreen());\n\n    // Initialise D-pad focus on the first occupied cell\n    if (const auto* ws = g_provider->activeWorkspace()) {\n        g_renderer->initFocus(*ws);\n    }\n\n    // Add some default tiles if workspace is empty\n    if (auto* active = g_provider->activeWorkspaceForEditing()) {\n        if (active->tiles.empty()) {\n            // Add a clock widget\n            model::Tile clock = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Clock, 2, 1);\n            clock.x = 0;\n            clock.y = 0;\n            if (model::placeable(*active, clock, 0, 0)) {\n                active->tiles.push_back(std::move(clock));\n            }\n            \n            // Add a battery widget\n            model::Tile battery = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Battery, 2, 1);\n            battery.x = 2;\n            battery.y = 0;\n            if (model::placeable(*active, battery, 2, 0)) {\n                active->tiles.push_back(std::move(battery));\n            }\n            \n            // Add a recent games widget\n            model::Tile recent = menu::widgets::WidgetFactory::createWidgetTile(\n                model::Kind::Recent, 3, 2);\n            recent.x = 0;\n            recent.y = 1;\n            if (model::placeable(*active, recent, 0, 1)) {\n                active->tiles.push_back(std::move(recent));\n            }\n        }\n    }\n\n    // Main loop\n    bool running = true;\n    Uint32 lastTime = SDL_GetTicks();\n    Uint32 lastWidgetUpdate = lastTime;\n    \n    debugPrintf(\"Entering main loop\\n\");\n\n    while (running) {\n        Uint32 currentTime = SDL_GetTicks();\n        float dt = (currentTime - lastTime) / 1000.0f;\n        lastTime = currentTime;\n\n        SDL_Event event;\n        while (SDL_PollEvent(&event)) {\n            switch (event.type) {\n                case SDL_QUIT:\n                    running = false;\n                    break;\n\n                case SDL_KEYDOWN: {\n                    // Feed A/B held state to the drag state machine.\n                    const bool aKey = (event.key.keysym.sym == SDLK_RETURN);\n                    const bool bKey = (event.key.keysym.sym == SDLK_BACKSPACE);\n                    if (g_inputHandler) {\n                        if (aKey) g_inputHandler->setAHeld(true);\n                        if (bKey) g_inputHandler->setBPressed(true);\n                    }\n\n                    switch (event.key.keysym.sym) {\n                        case SDLK_ESCAPE:\n                            running = false;\n                            break;\n\n                        case SDLK_MINUS: // '-' opens/closes settings\n                            if (!g_settings->isActive()) g_settings->show();\n                            break;\n\n                        // D-pad: settings get priority; in drag mode move destination;\n                        //        otherwise move focus cursor.\n                        case SDLK_UP:\n                            if (g_settings->isActive()) {\n                                g_settings->handleInput(0x1000);\n                            } else if (g_renderer && g_inputHandler) {\n                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {\n                                    if (auto* ws = g_provider->activeWorkspace())\n                                        g_inputHandler->moveDragDest(0, -1, *ws);\n                                } else {\n                                    g_renderer->moveFocus(desknx::touch::Direction::Up);\n                                }\n                            }\n                            break;\n                        case SDLK_DOWN:\n                            if (g_settings->isActive()) {\n                                g_settings->handleInput(0x2000);\n                            } else if (g_renderer && g_inputHandler) {\n                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {\n                                    if (auto* ws = g_provider->activeWorkspace())\n                                        g_inputHandler->moveDragDest(0, 1, *ws);\n                                } else {\n                                    g_renderer->moveFocus(desknx::touch::Direction::Down);\n                                }\n                            }\n                            break;\n                        case SDLK_LEFT:\n                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {\n                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {\n                                    if (auto* ws = g_provider->activeWorkspace())\n                                        g_inputHandler->moveDragDest(-1, 0, *ws);\n                                } else {\n                                    g_renderer->moveFocus(desknx::touch::Direction::Left);\n                                }\n                            }\n                            break;\n                        case SDLK_RIGHT:\n                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {\n                                if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {\n                                    if (auto* ws = g_provider->activeWorkspace())\n                                        g_inputHandler->moveDragDest(1, 0, *ws);\n                                } else {\n                                    g_renderer->moveFocus(desknx::touch::Direction::Right);\n                                }\n                            }\n                            break;\n\n                        // A — in Browse mode: start hold timer (drag entry handled\n                        //     in per-frame block).  In Drag mode: handled there too.\n                        case SDLK_RETURN:\n                            if (g_settings->isActive()) g_settings->handleInput(0x0001);\n                            break;\n\n                        // B — cancel drag / close settings\n                        case SDLK_BACKSPACE:\n                            if (g_settings->isActive()) g_settings->handleInput(0x0002);\n                            break;\n\n                        default:\n                            break;\n                    }\n                    break;\n                }\n\n                case SDL_KEYUP: {\n                    if (event.key.keysym.sym == SDLK_RETURN && g_inputHandler) {\n                        g_inputHandler->setAHeld(false);\n                        // If we were in Drag mode, A-release commits the drop.\n                        if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Drag) {\n                            if (auto* ws = g_provider->activeWorkspaceForEditing()) {\n                                auto result = g_inputHandler->commitDrop(*ws);\n                                g_renderer->endDragPreview();\n                                if (result == desknx::menu::input::DropResult::Committed) {\n                                    if (g_store) g_store->save();\n                                    debugPrintf(\"[Drop] committed & saved\\n\");\n                                } else {\n                                    debugPrintf(\"[Drop] invalid, bounced back\\n\");\n                                }\n                            }\n                        }\n                    }\n                    if (event.key.keysym.sym == SDLK_BACKSPACE && g_inputHandler) {\n                        g_inputHandler->setBPressed(false);\n                    }\n                    break;\n                }\n\n                case SDL_MOUSEBUTTONDOWN: {\n                    menu::input::TileInputHandler::State state = g_inputHandler->getCurrentState();\n                    g_inputHandler->onTouchEvent(\n                        event.button.x, event.button.y, true, {});\n                    break;\n                }\n\n                case SDL_MOUSEBUTTONUP: {\n                    g_inputHandler->onTouchEvent(\n                        event.button.x, event.button.y, false, {});\n                    break;\n                }\n\n                case SDL_MOUSEMOTION: {\n                    // Update hover state\n                    if (g_renderer) {\n                        int gridX = g_renderer->screenToGridX(event.motion.x);\n                        int gridY = g_renderer->screenToGridY(event.motion.y);\n                        \n                        auto* workspace = g_provider->activeWorkspace();\n                        if (workspace) {\n                            int tileIdx = model::itemAt(*workspace, gridX, gridY);\n                            g_renderer->setHoveredTile(tileIdx);\n                        }\n                    }\n                    break;\n                }\n\n                default:\n                    break;\n            }\n        }\n\n        // Update input and workspace\n        if (auto* workspace = g_provider->activeWorkspaceForEditing()) {\n            g_inputHandler->update(*workspace, dt);\n        }\n\n        // ── Per-frame drag/drop state machine ──────────────────────────────\n        if (g_inputHandler && g_renderer && !g_settings->isActive()) {\n            using desknx::menu::input::EditMode;\n            using desknx::menu::input::DropResult;\n            const Uint8* keys = SDL_GetKeyboardState(nullptr);\n\n            if (g_inputHandler->getEditMode() == EditMode::Drag) {\n                // B cancels drag\n                if (keys[SDL_SCANCODE_BACKSPACE]) {\n                    g_inputHandler->cancelDrop();\n                    g_renderer->endDragPreview();\n                    debugPrintf(\"[Drag] cancelled\\n\");\n                }\n                // Keep preview in sync\n                if (g_renderer->isDragPreviewActive()) {\n                    g_renderer->updateDragPreview(\n                        g_inputHandler->getDragDestX(),\n                        g_inputHandler->getDragDestY(),\n                        g_inputHandler->isDragDestValid());\n                }\n            } else { // Browse mode\n                // A held: increment counter; threshold → enter drag\n                if (keys[SDL_SCANCODE_RETURN]) {\n                    g_inputHandler->setAHeld(true);\n                    if (g_inputHandler->getAHoldFrames() >= desknx::menu::input::TileInputHandler::kAHoldThreshold) {\n                        if (auto* ws = g_provider->activeWorkspace()) {\n                            int tidx = g_renderer->focusedTileIndex(*ws);\n                            if (tidx >= 0) {\n                                const auto& tile = ws->tiles[static_cast<size_t>(tidx)];\n                                g_inputHandler->enterDrag(tidx, tile.x, tile.y);\n                                g_renderer->beginDragPreview(\n                                    tidx, tile.x, tile.y, tile.x, tile.y, true);\n                            }\n                        }\n                    }\n                } else {\n                    g_inputHandler->setAHeld(false);\n                }\n            }\n        }\n\n        // Update widgets every second\n        if (currentTime - lastWidgetUpdate >= 1000) {\n            lastWidgetUpdate = currentTime;\n            // TODO: Update widget data (clock, battery, recent games)\n        }\n\n        // Render grid\n        if (auto* workspace = g_provider->activeWorkspace()) {\n            // Refresh the renderer's workspace pointer (needed for moveFocus occupancy queries).\n            g_renderer->setWorkspace(*workspace);\n            g_renderer->render(*workspace);\n        }\n\n        // Render settings overlay on top (if active)\n        if (g_settings && g_settings->isActive()) {\n            g_settings->refresh();\n            g_settings->render(renderer);\n        }\n\n        SDL_RenderPresent(renderer);\n        SDL_Delay(16);  // ~60 FPS\n    }\n\n    // 5. On exit: save all persisted state atomically\n    debugPrintf(\"Saving application state before exit\\n\");\n    if (g_store && !g_store->save()) {\n        debugPrintf(\"Warning: Failed to save persisted state\\n\");\n    } else if (g_store) {\n        debugPrintf(\"Persisted state saved successfully\\n\");\n    }\n    \n    // Legacy layout save (backward compatibility)\n    menu::storage::LayoutStore::save(*g_provider);\n\n    // Cleanup\n    g_settings.reset();\n    g_store.reset();\n    g_catalog.reset();\n    g_inputHandler.reset();\n    g_renderer.reset();\n    SDL_DestroyRenderer(renderer);\n    SDL_DestroyWindow(window);\n    SDL_Quit();\n\n    socketExit();\n    debugPrintf(\"DeskNX Menu exited\\n\");\n\n    return 0;\n}\n"}