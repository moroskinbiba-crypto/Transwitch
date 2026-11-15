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

    // Populate workspace with real discovered apps if empty
    if (auto* active = g_provider->activeWorkspaceForEditing()) {
        if (active->tiles.empty() && g_catalog) {
            const auto& games = g_catalog->games();
            int x = 0, y = 0;
            
            debugPrintf("Populating grid with %d discovered games\n", g_catalog->gameCount());
            
            // Add discovered games (up to 18 tiles: 6×3 grid)
            for (size_t i = 0; i < games.size() && y < model::Rows; ++i) {
                const auto& entry = games[i];
                
                model::Tile tile;
                tile.kind = entry.isHomebrew ? model::Kind::Nro : model::Kind::Game;
                tile.title = entry.title.empty() ? "Unknown" : entry.title;
                
                // Store titleId as hex string in data field
                if (!entry.isHomebrew && entry.titleId != 0) {
                    char buf[17];
                    snprintf(buf, sizeof(buf), "%016lX", entry.titleId);
                    tile.data = buf;
                } else if (entry.isHomebrew) {
                    tile.data = entry.nroPath;
                }
                
                tile.w = 1;
                tile.h = 1;
                tile.x = x;
                tile.y = y;
                tile.color = static_cast<int>(entry.titleId % 8);  // color variety
                
                if (model::placeable(*active, tile, x, y)) {
                    active->tiles.push_back(std::move(tile));
                    debugPrintf("  [%d,%d] %s\n", x, y, tile.title.c_str());
                    x++;
                    if (x >= model::Columns) {
                        x = 0;
                        y++;
                    }
                }
            }
            
            debugPrintf("Populated %zu tiles from catalog\n", active->tiles.size());
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
                                } else if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Resize) {
                                    // Cycle to next size
                                    if (auto* ws = g_provider->activeWorkspace()) {
                                        g_inputHandler->cycleResizeSize(1, *ws);
                                        auto sz = g_inputHandler->getResizeTargetSize();
                                        g_renderer->updateResizePreview(sz.first, sz.second,
                                                                       g_inputHandler->isResizeValid());
                                    }
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
                                } else if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Resize) {
                                    // Cycle to previous size
                                    if (auto* ws = g_provider->activeWorkspace()) {
                                        g_inputHandler->cycleResizeSize(-1, *ws);
                                        auto sz = g_inputHandler->getResizeTargetSize();
                                        g_renderer->updateResizePreview(sz.first, sz.second,
                                                                       g_inputHandler->isResizeValid());
                                    }
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

                        // Y — enter resize mode on focused tile
                        case SDLK_y:
                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {
                                const int focusedIdx = g_renderer->focusedTileIndex();
                                if (focusedIdx >= 0) {
                                    if (auto* ws = g_provider->activeWorkspace()) {
                                        g_inputHandler->enterResize(focusedIdx, *ws);
                                        const auto& tile = ws->tiles[static_cast<size_t>(focusedIdx)];
                                        g_renderer->beginResizePreview(focusedIdx, tile.w, tile.h,
                                                                      g_inputHandler->isResizeValid());
                                    }
                                }
                            }
                            break;

                        // X — mark/delete focused tile (single press = immediate delete)
                        case SDLK_x:
                            if (!g_settings->isActive() && g_renderer && g_inputHandler) {
                                const int focusedIdx = g_renderer->focusedTileIndex();
                                if (focusedIdx >= 0) {
                                    if (auto* ws = g_provider->activeWorkspaceForEditing()) {
                                        // Immediate delete (no double-press, faster)
                                        ws->tiles.erase(ws->tiles.begin() + focusedIdx);
                                        if (g_store) g_store->save();
                                        debugPrintf("[Remove] deleted tile %d\n", focusedIdx);
                                        g_renderer->initFocus(*ws); // Re-init focus after delete
                                    }
                                }
                            }
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
                        // If we were in Resize mode, A-release commits the resize.
                        else if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Resize) {
                            if (auto* ws = g_provider->activeWorkspaceForEditing()) {
                                if (g_inputHandler->commitResize(*ws)) {
                                    if (g_store) g_store->save();
                                    debugPrintf("[Resize] committed & saved\n");
                                }
                                g_renderer->endResizePreview();
                            }
                        }
                    }
                    if (event.key.keysym.sym == SDLK_BACKSPACE && g_inputHandler) {
                        g_inputHandler->setBPressed(false);
                        // Cancel resize if in Resize mode
                        if (g_inputHandler->getEditMode() == desknx::menu::input::EditMode::Resize) {
                            g_inputHandler->cancelResize();
                            g_renderer->endResizePreview();
                        }
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
