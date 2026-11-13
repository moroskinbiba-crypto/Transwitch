#include <switch.h>
#include <SDL2/SDL.h>
#include <desknx/HomeNXWorkspaceProvider.hpp>
#include <desknx/HomeNXWorkspaceFileStore.hpp>
#include "../../../source/MenuActivity.hpp"

#include <fstream>
#include <sstream>

namespace {

// Simple file loader for layout persistence
bool loadLayoutFromFile(const std::string& path,
                        desknx::homenx::WorkspaceProvider& provider) {
    std::ifstream file(path);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    return provider.loadSerialized(buffer.str());
}

// Save layout to file
bool saveLayoutToFile(const std::string& path,
                      const desknx::homenx::WorkspaceProvider& provider) {
    std::ofstream file(path);
    if (!file) return false;
    file << provider.serializeLayout();
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char* argv[]) {
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

    // Set up the workspace provider
    desknx::homenx::WorkspaceProvider provider;

    // Try to load persisted layout from SD card
    const std::string layoutPath = "sdmc:/switch/DeskNX/layout.tsv";
    if (!loadLayoutFromFile(layoutPath, provider)) {
        // Provide a default workspace if no layout exists
        provider.addWorkspace("Home");
    }

    // Create the menu activity
    desknx::nxui::MenuActivity menu;
    menu.setWorkspaceProvider(&provider);

    // Main loop
    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

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
                    // TODO: Map HID input to menu navigation
                    // For now, keyboard for testing
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            running = false;
                            break;
                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
        }

        // Update and render
        menu.update(dt);
        menu.render(renderer);

        SDL_Delay(16);  // ~60 FPS
    }

    // Save layout before exiting
    saveLayoutToFile(layoutPath, provider);

    // Cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

{"content":"#include <switch.h>\n#include <SDL2/SDL.h>\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n#include <desknx/HomeNXWorkspaceFileStore.hpp>\n#include \"../../../source/MenuActivity.hpp\"\n\n#include <fstream>\n#include <sstream>\n\nnamespace {\n\n// Simple file loader for layout persistence\nbool loadLayoutFromFile(const std::string& path,\n                        desknx::homenx::WorkspaceProvider& provider) {\n    std::ifstream file(path);\n    if (!file) return false;\n    std::stringstream buffer;\n    buffer << file.rdbuf();\n    return provider.loadSerialized(buffer.str());\n}\n\n// Save layout to file\nbool saveLayoutToFile(const std::string& path,\n                      const desknx::homenx::WorkspaceProvider& provider) {\n    std::ofstream file(path);\n    if (!file) return false;\n    file << provider.serializeLayout();\n    return static_cast<bool>(file);\n}\n\n} // namespace\n\nint main(int argc, char* argv[]) {\n    // Initialize SDL2\n    if (SDL_Init(SDL_INIT_VIDEO) < 0) {\n        return 1;\n    }\n\n    // Create window and renderer\n    SDL_Window* window = SDL_CreateWindow(\n        \"DeskNX Menu\",\n        SDL_WINDOWPOS_CENTERED,\n        SDL_WINDOWPOS_CENTERED,\n        1280, 720,\n        SDL_WINDOW_SHOWN);\n\n    if (!window) {\n        SDL_Quit();\n        return 1;\n    }\n\n    SDL_Renderer* renderer = SDL_CreateRenderer(\n        window, -1,\n        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n\n    if (!renderer) {\n        SDL_DestroyWindow(window);\n        SDL_Quit();\n        return 1;\n    }\n\n    // Set up the workspace provider\n    desknx::homenx::WorkspaceProvider provider;\n\n    // Try to load persisted layout from SD card\n    const std::string layoutPath = \"sdmc:/switch/DeskNX/layout.tsv\";\n    if (!loadLayoutFromFile(layoutPath, provider)) {\n        // Provide a default workspace if no layout exists\n        provider.addWorkspace(\"Home\");\n    }\n\n    // Create the menu activity\n    desknx::nxui::MenuActivity menu;\n    menu.setWorkspaceProvider(&provider);\n\n    // Main loop\n    bool running = true;\n    Uint32 lastTime = SDL_GetTicks();\n\n    while (running) {\n        Uint32 currentTime = SDL_GetTicks();\n        float dt = (currentTime - lastTime) / 1000.0f;\n        lastTime = currentTime;\n\n        SDL_Event event;\n        while (SDL_PollEvent(&event)) {\n            switch (event.type) {\n                case SDL_QUIT:\n                    running = false;\n                    break;\n\n                case SDL_KEYDOWN:\n                    // TODO: Map HID input to menu navigation\n                    // For now, keyboard for testing\n                    switch (event.key.keysym.sym) {\n                        case SDLK_ESCAPE:\n                            running = false;\n                            break;\n                        default:\n                            break;\n                    }\n                    break;\n\n                default:\n                    break;\n            }\n        }\n\n        // Update and render\n        menu.update(dt);\n        menu.render(renderer);\n\n        SDL_Delay(16);  // ~60 FPS\n    }\n\n    // Save layout before exiting\n    saveLayoutToFile(layoutPath, provider);\n\n    // Cleanup\n    SDL_DestroyRenderer(renderer);\n    SDL_DestroyWindow(window);\n    SDL_Quit();\n\n    return 0;\n}\n"}