#pragma once

#include <SDL2/SDL.h>
#include <desknx/CardsLayout.hpp>

namespace desknx::cards {

struct Theme {
    SDL_Color top, bottom, accent;
};

struct HomeState {
    const std::vector<model::Workspace>& workspaces;
    const Deck& deck;
    int workspace, firstTab, selected;
    float anchor;
    std::string player, time, battery, storage, network, status;
    SDL_Color accent;
};

struct DrawHooks {
    SDL_Texture* (*artwork)(const model::Tile& tile);
    SDL_Color (*tileAccent)(const model::Tile& tile);
    unsigned (*online)(const std::string& titleId);
    void (*text)(const std::string& value, int x, int y, int scale,
                 SDL_Color color, int right, bool localize);
};

// Runs on the existing SDL render thread. It owns one small cached backdrop;
// icon textures belong to IconCache and callbacks borrow them for the frame.
class HomeRenderer {
public:
    void backdrop(SDL_Renderer* renderer, Theme theme);
    void draw(SDL_Renderer* renderer, const HomeState& view, const DrawHooks& hooks) const;
    void release(); // before SDL_DestroyRenderer
private:
    SDL_Texture* backdrop_ = nullptr;
    Theme lastTheme_{};
};

} // namespace desknx::cards

{"content":"#pragma once\n\n#include <SDL2/SDL.h>\n#include <desknx/CardsLayout.hpp>\n\nnamespace desknx::cards {\n\nstruct Theme {\n    SDL_Color top, bottom, accent;\n};\n\nstruct HomeState {\n    const std::vector<model::Workspace>& workspaces;\n    const Deck& deck;\n    int workspace, firstTab, selected;\n    float anchor;\n    std::string player, time, battery, storage, network, status;\n    SDL_Color accent;\n};\n\nstruct DrawHooks {\n    SDL_Texture* (*artwork)(const model::Tile& tile);\n    SDL_Color (*tileAccent)(const model::Tile& tile);\n    unsigned (*online)(const std::string& titleId);\n    void (*text)(const std::string& value, int x, int y, int scale,\n                 SDL_Color color, int right, bool localize);\n};\n\n// Runs on the existing SDL render thread. It owns one small cached backdrop;\n// icon textures belong to IconCache and callbacks borrow them for the frame.\nclass HomeRenderer {\npublic:\n    void backdrop(SDL_Renderer* renderer, Theme theme);\n    void draw(SDL_Renderer* renderer, const HomeState& view, const DrawHooks& hooks) const;\n    void release(); // before SDL_DestroyRenderer\nprivate:\n    SDL_Texture* backdrop_ = nullptr;\n    Theme lastTheme_{};\n};\n\n} // namespace desknx::cards\n"}