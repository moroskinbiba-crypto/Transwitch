#include "CardsHomeRenderer.hpp"

#include <cmath>

namespace desknx::cards {
namespace {

constexpr SDL_Color Ink{248, 251, 255, 255};
constexpr SDL_Color Soft{190, 208, 227, 255};
constexpr SDL_Color Pale{232, 245, 255, 255};
constexpr SDL_Color Night{11, 23, 42, 255};
constexpr SDL_Color Glass{11, 26, 48, 224};
constexpr SDL_Color Edge{216, 238, 255, 67};
constexpr SDL_Color Shadow{1, 5, 15, 74};
constexpr float HalfPi = 1.5707963268f;

SDL_Color alpha(SDL_Color c, Uint8 a) { c.a = a; return c; }
SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    auto channel = [t](Uint8 low, Uint8 high) {
        return static_cast<Uint8>(low + (static_cast<float>(high) - low) * t);
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}
bool same(SDL_Color a, SDL_Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
SDL_Rect rect(Box b) { return {b.x, b.y, b.w, b.h}; }
Box inset(Box b, int amount) { return {b.x + amount, b.y + amount, b.w - 2*amount, b.h - 2*amount}; }
void color(SDL_Renderer* r, SDL_Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }
void fill(SDL_Renderer* r, Box b, SDL_Color c) {
    color(r, c);
    const auto dst = rect(b);
    SDL_RenderFillRect(r, &dst);
}

void rounded(SDL_Renderer* r, Box b, int radius, SDL_Color c) {
    const int rad = std::clamp(radius, 0, std::min({32, b.w / 2, b.h / 2}));
    if (rad == 0) { fill(r, b, c); return; }
    std::array<SDL_Rect, 65> spans{};
    int count = 0;
    if (b.h > 2*rad) spans[count++] = {b.x, b.y + rad, b.w, b.h - 2*rad};
    for (int y = 0; y < rad; ++y) {
        const float dy = rad - y - 0.5f;
        const int cut = rad - static_cast<int>(std::sqrt(rad*rad - dy*dy));
        if (b.w <= 2*cut) continue;
        spans[count++] = {b.x + cut, b.y + y, b.w - 2*cut, 1};
        spans[count++] = {b.x + cut, b.y + b.h - y - 1, b.w - 2*cut, 1};
    }
    color(r, c);
    SDL_RenderFillRects(r, spans.data(), count);
}

void panel(SDL_Renderer* r, Box b, int rad, SDL_Color background = Glass, SDL_Color edge = Edge) {
    rounded(r, b, rad, edge);
    rounded(r, inset(b, 1), rad - 1, background);
}

// The renderer's clip rect can clip off-screen cards. A rounded UV mesh clips
// the artwork itself without repainting its corners with the wallpaper color.
bool art(SDL_Renderer* r, SDL_Texture* tex, Box b, int radius) {
    int w = 0, h = 0;
    if (!tex || SDL_QueryTexture(tex, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0)
        return false;
    const float texAspect = static_cast<float>(w) / h;
    const float boxAspect = static_cast<float>(b.w) / b.h;
    const float u = std::min(1.f, boxAspect / texAspect);
    const float v = std::min(1.f, texAspect / boxAspect);
#if SDL_VERSION_ATLEAST(2, 0, 18)
    constexpr int Samples = 8;
    constexpr int Points = 4 * (Samples + 1);
    const float rad = static_cast<float>(std::min({radius, b.w/2, b.h/2}));
    const float left = static_cast<float>(b.x), top = static_cast<float>(b.y);
    const float right = left + b.w, bottom = top + b.h;
    const SDL_FPoint centers[] = {{right-rad, top+rad}, {right-rad, bottom-rad},
                                   {left+rad, bottom-rad}, {left+rad, top+rad}};
    std::array<SDL_Vertex, Points + 1> vertices{};
    std::array<int, Points * 3> triangles{};
    constexpr SDL_Color NoTint{255, 255, 255, 255};
    vertices[0] = {{left + b.w / 2.f, top + b.h / 2.f}, NoTint, {0.5f, 0.5f}};
    int index = 1;
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= Samples; ++step, ++index) {
            const float angle = HalfPi * (corner - 1 + static_cast<float>(step) / Samples);
            const float x = centers[corner].x + rad * std::cos(angle);
            const float y = centers[corner].y + rad * std::sin(angle);
            vertices[index] = {{x, y}, NoTint,
                {std::clamp(0.5f + ((x - left) / b.w - 0.5f) * u, 0.f, 1.f),
                 std::clamp(0.5f + ((y - top) / b.h - 0.5f) * v, 0.f, 1.f)}};
            const int at = (index - 1) * 3;
            triangles[at] = 0;
            triangles[at + 1] = index;
            triangles[at + 2] = index == Points ? 1 : index + 1;
        }
    }
    if (SDL_RenderGeometry(r, tex, vertices.data(), static_cast<int>(vertices.size()),
                           triangles.data(), static_cast<int>(triangles.size())) == 0)
        return true;
#else
    (void)radius;
#endif
    const int sw = std::clamp(static_cast<int>(w * u), 1, w);
    const int sh = std::clamp(static_cast<int>(h * v), 1, h);
    const SDL_Rect src{(w - sw) / 2, (h - sh) / 2, sw, sh};
    const auto dst = rect(b);
    return SDL_RenderCopy(r, tex, &src, &dst) == 0;
}

void icon(SDL_Renderer* r, Box b, Tool which, SDL_Color fg) {
    const int x = b.x + b.w / 2, y = b.y + b.h / 2;
    auto line = [&](int x1, int y1, int x2, int y2) {
        color(r, fg);
        SDL_RenderDrawLine(r, x + x1, y + y1, x + x2, y + y2);
        SDL_RenderDrawLine(r, x + x1, y + y1 + 1, x + x2, y + y2 + 1);
    };
    auto square = [&](int x1, int y1, int w, int h) {
        const auto dst = rect({x + x1, y + y1, w, h});
        color(r, fg);
        SDL_RenderDrawRect(r, &dst);
    };
    auto ring = [&](int radius, int begin, int end) {
        std::array<SDL_Point, 25> points{};
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const float a = (begin + (end - begin) * (i / 24.f)) * 0.01745329252f;
            points[i] = {x + static_cast<int>(std::round(radius * std::cos(a))),
                         y + static_cast<int>(std::round(radius * std::sin(a)))};
        }
        color(r, fg);
        SDL_RenderDrawLines(r, points.data(), static_cast<int>(points.size()));
        for (auto& p : points) ++p.y;
        SDL_RenderDrawLines(r, points.data(), static_cast<int>(points.size()));
    };
    switch (which) {
        case Tool::Library:
            for (int i = -10; i <= 4; i += 7) square(i, i == -3 ? -10 : -8, 6, 17);
            break;
        case Tool::Gallery:
            square(-11, -9, 23, 18);
            line(-9, 6, -3, 0); line(-3, 0, 0, 3);
            line(0, 3, 6, -4); line(6, -4, 10, 0);
            fill(r, {x - 6, y - 6, 3, 3}, fg);
            break;
        case Tool::Controllers:
            line(-8, -6, 8, -6); line(-8, -6, -12, 5); line(8, -6, 12, 5);
            line(-12, 5, -8, 7); line(12, 5, 8, 7); line(-8, 7, -3, 3);
            line(8, 7, 3, 3); line(-3, 3, 3, 3); line(-8, -1, -3, -1);
            line(-6, -3, -6, 2); fill(r, {x + 5, y - 1, 3, 3}, fg);
            break;
        case Tool::Tools:
            for (int i = 0; i < 3; ++i)
                rounded(r, {x - 10 + i*8, y - 2, 4, 4}, 2, fg);
            break;
        case Tool::Power:
            ring(9, -58, 238); line(0, -11, 0, -1);
            break;
        case Tool::Settings:
            ring(7, 0, 360); ring(2, 0, 360);
            for (int i = 0; i < 8; ++i) {
                const float a = i * HalfPi / 2;
                line(static_cast<int>(std::round(std::cos(a) * 7)),
                     static_cast<int>(std::round(std::sin(a) * 7)),
                     static_cast<int>(std::round(std::cos(a) * 11)),
                     static_cast<int>(std::round(std::sin(a) * 11)));
            }
            break;
        default: break;
    }
}

const char* kindLabel(model::Kind kind) {
    switch (kind) {
        case model::Kind::Game: return "GAME";
        case model::Kind::Nro: return "HOMEBREW";
        case model::Kind::Unsupported: return "UNSUPPORTED";
        default: return "WIDGETS";
    }
}

bool isApp(const model::Tile& tile) {
    return tile.kind == model::Kind::Game || tile.kind == model::Kind::Nro;
}

void poster(SDL_Renderer* r, const DrawHooks& hooks, const model::Tile& tile,
            SDL_Texture* image, Box b, SDL_Color accent, bool selected, unsigned online) {
    const int radius = 23;
    SDL_Color ink = selected ? Ink : Pale;
    if (selected) rounded(r, inset(b, -6), radius + 6, alpha(accent, 108));
    rounded(r, {b.x, b.y + 8, b.w, b.h}, radius, Shadow);
    rounded(r, b, radius, selected ? Ink : alpha(Pale, 124));
    const int frame = selected ? 3 : 1;
    const Box face = inset(b, frame);
    const SDL_Color tint = hooks.tileAccent(tile);
    const SDL_Color base = mix(Night, tint, 0.43f);
    rounded(r, face, radius - frame, base);

    int width = 0, height = 0;
    const bool hasArt = image && SDL_QueryTexture(image, nullptr, nullptr, &width, &height) == 0 &&
                        width > 0 && height > 0;
    const bool portrait = hasArt && width * 100 <= height * 86;
    if (portrait) {
        art(r, image, face, radius - frame);
    } else {
        // The built-in NACP image is square. Frame it rather than cutting off
        // its title to fill a portrait slot; actual portrait art fills the card.
        const int tileSize = face.w - 34;
        const Box insetArt{face.x + 17, face.y + (face.h - tileSize) / 2 - 13, tileSize, tileSize};
        const SDL_Color tone = mix(base, tint, 0.31f);
        rounded(r, {face.x + 10, face.y + 10, face.w - 20, face.h - 84}, 16, tone);
        const int eyebrowX = face.x + 17;
        fill(r, {eyebrowX, face.y + 31, face.w - 34, 1}, alpha(Ink, 48));
        hooks.text(kindLabel(tile.kind), eyebrowX, face.y + 17, 1, ink, face.x + face.w - 17, true);
        if (hasArt) {
            rounded(r, inset(insetArt, -1), 15, alpha(Pale, 98));
            art(r, image, insetArt, 14);
        } else {
            panel(r, insetArt, 16, mix(base, tint, 0.56f), alpha(Pale, 52));
            icon(r, insetArt, isApp(tile) ? Tool::Library : Tool::Tools, Pale);
        }
        hooks.text(tile.title, face.x + 17, face.y + face.h - 55, 2, ink,
                   face.x + face.w - 17, false);
        const int rule = selected ? 38 : 21;
        rounded(r, {face.x + 17, face.y + face.h - 24, rule, 3}, 2, selected ? accent : alpha(Pale, 109));
    }
    if (online > 0) {
        const Box badge{b.x + b.w - 76, b.y + 12, 61, 24};
        panel(r, badge, 12, {8, 30, 28, 243}, {135, 238, 186, 97});
        rounded(r, {badge.x + 8, badge.y + 8, 8, 8}, 4, {111, 245, 173, 255});
        hooks.text(std::to_string(online), badge.x + 22, badge.y + 5, 1, Ink, badge.x + badge.w - 4, false);
    }
}

void primaryButton(SDL_Renderer* r, const DrawHooks& hooks, const char* label, bool playable) {
    rounded(r, {Primary.x, Primary.y + 4, Primary.w, Primary.h}, 28, Shadow);
    rounded(r, Primary, 28, Ink);
    if (playable) {
        color(r, Night);
        for (int i = 0; i < 10; ++i) {
            const int h = 7 - (i * 7 / 10);
            SDL_RenderDrawLine(r, Primary.x + 26 + i, Primary.y + 27 - h,
                              Primary.x + 26 + i, Primary.y + 27 + h);
        }
    } else {
        fill(r, {Primary.x + 23, Primary.y + 26, 14, 2}, Night);
        fill(r, {Primary.x + 29, Primary.y + 20, 2, 14}, Night);
    }
    hooks.text(label, Primary.x + 50, Primary.y + 16, 2, Night, Primary.x + Primary.w - 14, true);
}

} // namespace

void HomeRenderer::backdrop(SDL_Renderer* r, Theme theme) {
    if (!backdrop_ || !same(theme.top, lastTheme_.top) || !same(theme.bottom, lastTheme_.bottom) ||
        !same(theme.accent, lastTheme_.accent)) {
        release();
        constexpr int w = 320, h = 180;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            for (int y = 0; y < h; ++y) {
                auto* row = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;
                const float fy = y / static_cast<float>(h - 1);
                for (int x = 0; x < w; ++x) {
                    const float fx = x / static_cast<float>(w - 1);
                    const float left = (fx - .19f) * (fx - .19f) * 7.4f + (fy - .28f)*(fy - .28f)*7.4f;
                    const float right = (fx - .89f)*(fx - .89f)*8.f + (fy - .58f)*(fy - .58f)*10.f;
                    const float wave = fy - (.21f + .43f * fx + .09f * std::sin(fx * 5.f));
                    SDL_Color c = mix(theme.top, theme.bottom, fy);
                    c = mix(c, theme.accent, .42f * std::exp(-left) + .19f * std::exp(-wave * wave * 70.f));
                    c = mix(c, mix(theme.accent, {69, 79, 193, 255}, .55f), .36f * std::exp(-right));
                    // A small amount of dither breaks up banding on the
                    // console's 8-bit panel without showing as texture.
                    const int grain = ((x * 7 + y * 13) % 3) - 1;
                    row[4*x] = static_cast<Uint8>(std::clamp(c.r + grain, 0, 255));
                    row[4*x+1] = static_cast<Uint8>(std::clamp(c.g + grain, 0, 255));
                    row[4*x+2] = static_cast<Uint8>(std::clamp(c.b + grain, 0, 255));
                    row[4*x+3] = 255;
                }
            }
            backdrop_ = SDL_CreateTextureFromSurface(r, surface);
            SDL_FreeSurface(surface);
        }
        lastTheme_ = theme;
    }
    const auto dst = rect({0, 0, Width, Height});
    if (!backdrop_ || SDL_RenderCopy(r, backdrop_, nullptr, &dst) != 0)
        fill(r, {0, 0, Width, Height}, theme.bottom);
}

void HomeRenderer::draw(SDL_Renderer* r, const HomeState& view, const DrawHooks& hooks) const {
    const auto& workspace = view.workspaces[static_cast<std::size_t>(view.workspace)];
    const auto& deck = view.deck;
    const model::Tile* selectedTile = deck.size > 0
        ? &workspace.tiles[static_cast<std::size_t>(deck.indices[static_cast<std::size_t>(view.selected)])] : nullptr;
    SDL_Texture* selectedArt = selectedTile ? hooks.artwork(*selectedTile) : nullptr;

    panel(r, {41, 24, 39, 39}, 12, {24, 53, 77, 246});
    fill(r, {50, 35, 5, 17}, view.accent);
    fill(r, {58, 31, 5, 21}, Ink);
    fill(r, {66, 38, 5, 14}, view.accent);
    hooks.text("DeskNX", 93, 23, 4, Ink, 296, false);
    hooks.text("HOME LAUNCHER", 95, 61, 1, Soft, 294, true);

    panel(r, Profile, 26);
    rounded(r, {326, 31, 36, 36}, 18, {48, 80, 107, 255});
    rounded(r, {341, 39, 7, 7}, 4, Ink);
    rounded(r, {336, 49, 17, 10}, 5, Ink);
    hooks.text(view.player, 373, 37, 2, Ink, 555, false);
    fill(r, {567, 36, 1, 26}, Edge);
    hooks.text(view.time, 585, 37, 2, Ink, 651, false);
    fill(r, {657, 36, 1, 26}, Edge);
    hooks.text(view.battery, 672, 38, 2, Ink, 735, false);
    fill(r, {743, 36, 1, 26}, Edge);
    hooks.text(view.storage, 757, 38, 2, Ink, 850, false);
    for (int i = 0; i < static_cast<int>(Tool::Count); ++i) {
        const auto which = static_cast<Tool>(i);
        Box bounds = tool(which);
        panel(r, bounds, 25);
        icon(r, bounds, which, Ink);
    }

    fill(r, {43, 111, 5, 18}, view.accent);
    hooks.text(workspace.name, 61, 99, 3, Ink, 710, true);
    if (!view.network.empty()) {
        panel(r, {1022, 100, 215, 33}, 17, Glass, alpha(view.accent, 100));
        hooks.text(view.network, 1036, 109, 1, Pale, 1227, true);
    }

    if (!selectedTile) {
        panel(r, {364, 235, 552, 140}, 23);
        hooks.text("THIS DESKTOP IS EMPTY", 394, 267, 3, Ink, 885, true);
        hooks.text("Add games, homebrew and widgets to get started.", 396, 313, 2, Soft, 883, true);
    } else {
        // Focused art is queued first; only visible neighbors borrow textures.
        // The focused card is painted last so its bright edge owns the overlap.
        auto drawAt = [&](int i) {
            const Box bounds = card(i, view.anchor);
            if (!visible(bounds)) return;
            const auto& tile = workspace.tiles[static_cast<std::size_t>(deck.indices[static_cast<std::size_t>(i)])];
            SDL_Texture* image = i == view.selected ? selectedArt : hooks.artwork(tile);
            const unsigned online = tile.kind == model::Kind::Game ? hooks.online(tile.data) : 0;
            poster(r, hooks, tile, image, bounds, view.accent, i == view.selected, online);
        };
        for (int i = 0; i < deck.size; ++i) if (i != view.selected) drawAt(i);
        drawAt(view.selected);
    }

    rounded(r, {Detail.x, Detail.y + 6, Detail.w, Detail.h}, 27, Shadow);
    panel(r, Detail, 27, Glass, alpha(Pale, 112));
    const Box thumbnail{197, 513, 67, 67};
    panel(r, thumbnail, 14, {34, 60, 84, 255});
    if (!art(r, selectedArt, inset(thumbnail, 2), 12))
        icon(r, thumbnail, selectedTile && !isApp(*selectedTile) ? Tool::Tools : Tool::Library, Pale);
    hooks.text(selectedTile ? selectedTile->title : "APP LIBRARY", 281, 508, 4, Ink, 789, !selectedTile);
    if (selectedTile) {
        hooks.text(kindLabel(selectedTile->kind), 284, 555, 1, view.accent, 433, true);
        const std::string index = std::to_string(view.selected + 1) + " / " + std::to_string(deck.size);
        hooks.text(index, 439, 552, 2, Soft, 560, false);
    } else {
        hooks.text("Y  ADD APPS", 284, 555, 1, view.accent, 520, true);
    }
    hooks.text(view.status.empty() ? "L/R  SPACES     X  EDIT     ZL  VIEW" : view.status,
               201, 594, 1, Soft, 1080, true);
    primaryButton(r, hooks, selectedTile ? (selectedTile->kind == model::Kind::Game ? "PLAY" : "OPEN")
                                        : "ADD APPS", selectedTile && isApp(*selectedTile));
    panel(r, Actions, 28, {28, 51, 75, 230}, Edge);
    icon(r, Actions, Tool::Tools, Pale);
    if (selectedTile) {
        const int bar = 160 / deck.size;
        for (int i = 0; i < deck.size; ++i)
            rounded(r, {831 + i * bar, 588, bar - 3, 4}, 2,
                    i == view.selected ? view.accent : alpha(Pale, 78));
    }

    panel(r, Footer, 24, Glass);
    for (int slot = 0; slot < VisibleTabs; ++slot) {
        const int index = view.firstTab + slot;
        if (index >= static_cast<int>(view.workspaces.size())) break;
        const Box bounds = tab(slot);
        const bool active = index == view.workspace;
        panel(r, bounds, 17, active ? SDL_Color{37, 72, 104, 247} : SDL_Color{21, 42, 64, 224},
              active ? alpha(view.accent, 164) : alpha(Pale, 45));
        hooks.text(view.workspaces[static_cast<std::size_t>(index)].name,
                   bounds.x + 12, bounds.y + 15, 2, active ? Ink : Soft, bounds.x + bounds.w - 10, true);
        if (active) rounded(r, {bounds.x + 12, bounds.y + 39, 22, 2}, 1, view.accent);
    }
    panel(r, AddDesk, 17, {23, 49, 75, 240});
    const SDL_Color add =
        view.workspaces.size() < static_cast<std::size_t>(model::MaxDesks) ? Ink : Soft;
    fill(r, {AddDesk.x + 19, AddDesk.y + 23, 14, 2}, add);
    fill(r, {AddDesk.x + 25, AddDesk.y + 17, 2, 14}, add);
    panel(r, Workspaces, 17);
    icon(r, Workspaces, Tool::Tools, Ink);
    panel(r, WorkspaceCount, 17);
    hooks.text("L / R", WorkspaceCount.x + 12, WorkspaceCount.y + 16, 1, view.accent, WorkspaceCount.x + 60, false);
    hooks.text(std::to_string(view.workspace + 1) + " / " + std::to_string(view.workspaces.size()),
               WorkspaceCount.x + 68, WorkspaceCount.y + 12, 2, Ink, WorkspaceCount.x + 141, false);
    panel(r, Arrange, 17, {28, 52, 77, 240});
    rounded(r, {Arrange.x + 10, Arrange.y + 12, 25, 25}, 12, {53, 84, 112, 255});
    hooks.text("X", Arrange.x + 18, Arrange.y + 15, 2, Ink, Arrange.x + 35, false);
    hooks.text("EDIT", Arrange.x + 44, Arrange.y + 15, 2, Ink, Arrange.x + Arrange.w - 8, true);
}

void HomeRenderer::release() {
    if (backdrop_) SDL_DestroyTexture(backdrop_);
    backdrop_ = nullptr;
}

} // namespace desknx::cards

{"content":"#include \"CardsHomeRenderer.hpp\"\n\n#include <cmath>\n\nnamespace desknx::cards {\nnamespace {\n\nconstexpr SDL_Color Ink{248, 251, 255, 255};\nconstexpr SDL_Color Soft{190, 208, 227, 255};\nconstexpr SDL_Color Pale{232, 245, 255, 255};\nconstexpr SDL_Color Night{11, 23, 42, 255};\nconstexpr SDL_Color Glass{11, 26, 48, 224};\nconstexpr SDL_Color Edge{216, 238, 255, 67};\nconstexpr SDL_Color Shadow{1, 5, 15, 74};\nconstexpr float HalfPi = 1.5707963268f;\n\nSDL_Color alpha(SDL_Color c, Uint8 a) { c.a = a; return c; }\nSDL_Color mix(SDL_Color a, SDL_Color b, float t) {\n    auto channel = [t](Uint8 low, Uint8 high) {\n        return static_cast<Uint8>(low + (static_cast<float>(high) - low) * t);\n    };\n    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};\n}\nbool same(SDL_Color a, SDL_Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }\nSDL_Rect rect(Box b) { return {b.x, b.y, b.w, b.h}; }\nBox inset(Box b, int amount) { return {b.x + amount, b.y + amount, b.w - 2*amount, b.h - 2*amount}; }\nvoid color(SDL_Renderer* r, SDL_Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }\nvoid fill(SDL_Renderer* r, Box b, SDL_Color c) {\n    color(r, c);\n    const auto dst = rect(b);\n    SDL_RenderFillRect(r, &dst);\n}\n\nvoid rounded(SDL_Renderer* r, Box b, int radius, SDL_Color c) {\n    const int rad = std::clamp(radius, 0, std::min({32, b.w / 2, b.h / 2}));\n    if (rad == 0) { fill(r, b, c); return; }\n    std::array<SDL_Rect, 65> spans{};\n    int count = 0;\n    if (b.h > 2*rad) spans[count++] = {b.x, b.y + rad, b.w, b.h - 2*rad};\n    for (int y = 0; y < rad; ++y) {\n        const float dy = rad - y - 0.5f;\n        const int cut = rad - static_cast<int>(std::sqrt(rad*rad - dy*dy));\n        if (b.w <= 2*cut) continue;\n        spans[count++] = {b.x + cut, b.y + y, b.w - 2*cut, 1};\n        spans[count++] = {b.x + cut, b.y + b.h - y - 1, b.w - 2*cut, 1};\n    }\n    color(r, c);\n    SDL_RenderFillRects(r, spans.data(), count);\n}\n\nvoid panel(SDL_Renderer* r, Box b, int rad, SDL_Color background = Glass, SDL_Color edge = Edge) {\n    rounded(r, b, rad, edge);\n    rounded(r, inset(b, 1), rad - 1, background);\n}\n\n// The renderer's clip rect can clip off-screen cards. A rounded UV mesh clips\n// the artwork itself without repainting its corners with the wallpaper color.\nbool art(SDL_Renderer* r, SDL_Texture* tex, Box b, int radius) {\n    int w = 0, h = 0;\n    if (!tex || SDL_QueryTexture(tex, nullptr, nullptr, &w, &h) != 0 || w <= 0 || h <= 0)\n        return false;\n    const float texAspect = static_cast<float>(w) / h;\n    const float boxAspect = static_cast<float>(b.w) / b.h;\n    const float u = std::min(1.f, boxAspect / texAspect);\n    const float v = std::min(1.f, texAspect / boxAspect);\n#if SDL_VERSION_ATLEAST(2, 0, 18)\n    constexpr int Samples = 8;\n    constexpr int Points = 4 * (Samples + 1);\n    const float rad = static_cast<float>(std::min({radius, b.w/2, b.h/2}));\n    const float left = static_cast<float>(b.x), top = static_cast<float>(b.y);\n    const float right = left + b.w, bottom = top + b.h;\n    const SDL_FPoint centers[] = {{right-rad, top+rad}, {right-rad, bottom-rad},\n                                   {left+rad, bottom-rad}, {left+rad, top+rad}};\n    std::array<SDL_Vertex, Points + 1> vertices{};\n    std::array<int, Points * 3> triangles{};\n    constexpr SDL_Color NoTint{255, 255, 255, 255};\n    vertices[0] = {{left + b.w / 2.f, top + b.h / 2.f}, NoTint, {0.5f, 0.5f}};\n    int index = 1;\n    for (int corner = 0; corner < 4; ++corner) {\n        for (int step = 0; step <= Samples; ++step, ++index) {\n            const float angle = HalfPi * (corner - 1 + static_cast<float>(step) / Samples);\n            const float x = centers[corner].x + rad * std::cos(angle);\n            const float y = centers[corner].y + rad * std::sin(angle);\n            vertices[index] = {{x, y}, NoTint,\n                {std::clamp(0.5f + ((x - left) / b.w - 0.5f) * u, 0.f, 1.f),\n                 std::clamp(0.5f + ((y - top) / b.h - 0.5f) * v, 0.f, 1.f)}};\n            const int at = (index - 1) * 3;\n            triangles[at] = 0;\n            triangles[at + 1] = index;\n            triangles[at + 2] = index == Points ? 1 : index + 1;\n        }\n    }\n    if (SDL_RenderGeometry(r, tex, vertices.data(), static_cast<int>(vertices.size()),\n                           triangles.data(), static_cast<int>(triangles.size())) == 0)\n        return true;\n#else\n    (void)radius;\n#endif\n    const int sw = std::clamp(static_cast<int>(w * u), 1, w);\n    const int sh = std::clamp(static_cast<int>(h * v), 1, h);\n    const SDL_Rect src{(w - sw) / 2, (h - sh) / 2, sw, sh};\n    const auto dst = rect(b);\n    return SDL_RenderCopy(r, tex, &src, &dst) == 0;\n}\n\nvoid icon(SDL_Renderer* r, Box b, Tool which, SDL_Color fg) {\n    const int x = b.x + b.w / 2, y = b.y + b.h / 2;\n    auto line = [&](int x1, int y1, int x2, int y2) {\n        color(r, fg);\n        SDL_RenderDrawLine(r, x + x1, y + y1, x + x2, y + y2);\n        SDL_RenderDrawLine(r, x + x1, y + y1 + 1, x + x2, y + y2 + 1);\n    };\n    auto square = [&](int x1, int y1, int w, int h) {\n        const auto dst = rect({x + x1, y + y1, w, h});\n        color(r, fg);\n        SDL_RenderDrawRect(r, &dst);\n    };\n    auto ring = [&](int radius, int begin, int end) {\n        std::array<SDL_Point, 25> points{};\n        for (int i = 0; i < static_cast<int>(points.size()); ++i) {\n            const float a = (begin + (end - begin) * (i / 24.f)) * 0.01745329252f;\n            points[i] = {x + static_cast<int>(std::round(radius * std::cos(a))),\n                         y + static_cast<int>(std::round(radius * std::sin(a)))};\n        }\n        color(r, fg);\n        SDL_RenderDrawLines(r, points.data(), static_cast<int>(points.size()));\n        for (auto& p : points) ++p.y;\n        SDL_RenderDrawLines(r, points.data(), static_cast<int>(points.size()));\n    };\n    switch (which) {\n        case Tool::Library:\n            for (int i = -10; i <= 4; i += 7) square(i, i == -3 ? -10 : -8, 6, 17);\n            break;\n        case Tool::Gallery:\n            square(-11, -9, 23, 18);\n            line(-9, 6, -3, 0); line(-3, 0, 0, 3);\n            line(0, 3, 6, -4); line(6, -4, 10, 0);\n            fill(r, {x - 6, y - 6, 3, 3}, fg);\n            break;\n        case Tool::Controllers:\n            line(-8, -6, 8, -6); line(-8, -6, -12, 5); line(8, -6, 12, 5);\n            line(-12, 5, -8, 7); line(12, 5, 8, 7); line(-8, 7, -3, 3);\n            line(8, 7, 3, 3); line(-3, 3, 3, 3); line(-8, -1, -3, -1);\n            line(-6, -3, -6, 2); fill(r, {x + 5, y - 1, 3, 3}, fg);\n            break;\n        case Tool::Tools:\n            for (int i = 0; i < 3; ++i)\n                rounded(r, {x - 10 + i*8, y - 2, 4, 4}, 2, fg);\n            break;\n        case Tool::Power:\n            ring(9, -58, 238); line(0, -11, 0, -1);\n            break;\n        case Tool::Settings:\n            ring(7, 0, 360); ring(2, 0, 360);\n            for (int i = 0; i < 8; ++i) {\n                const float a = i * HalfPi / 2;\n                line(static_cast<int>(std::round(std::cos(a) * 7)),\n                     static_cast<int>(std::round(std::sin(a) * 7)),\n                     static_cast<int>(std::round(std::cos(a) * 11)),\n                     static_cast<int>(std::round(std::sin(a) * 11)));\n            }\n            break;\n        default: break;\n    }\n}\n\nconst char* kindLabel(model::Kind kind) {\n    switch (kind) {\n        case model::Kind::Game: return \"GAME\";\n        case model::Kind::Nro: return \"HOMEBREW\";\n        case model::Kind::Unsupported: return \"UNSUPPORTED\";\n        default: return \"WIDGETS\";\n    }\n}\n\nbool isApp(const model::Tile& tile) {\n    return tile.kind == model::Kind::Game || tile.kind == model::Kind::Nro;\n}\n\nvoid poster(SDL_Renderer* r, const DrawHooks& hooks, const model::Tile& tile,\n            SDL_Texture* image, Box b, SDL_Color accent, bool selected, unsigned online) {\n    const int radius = 23;\n    SDL_Color ink = selected ? Ink : Pale;\n    if (selected) rounded(r, inset(b, -6), radius + 6, alpha(accent, 108));\n    rounded(r, {b.x, b.y + 8, b.w, b.h}, radius, Shadow);\n    rounded(r, b, radius, selected ? Ink : alpha(Pale, 124));\n    const int frame = selected ? 3 : 1;\n    const Box face = inset(b, frame);\n    const SDL_Color tint = hooks.tileAccent(tile);\n    const SDL_Color base = mix(Night, tint, 0.43f);\n    rounded(r, face, radius - frame, base);\n\n    int width = 0, height = 0;\n    const bool hasArt = image && SDL_QueryTexture(image, nullptr, nullptr, &width, &height) == 0 &&\n                        width > 0 && height > 0;\n    const bool portrait = hasArt && width * 100 <= height * 86;\n    if (portrait) {\n        art(r, image, face, radius - frame);\n    } else {\n        // The built-in NACP image is square. Frame it rather than cutting off\n        // its title to fill a portrait slot; actual portrait art fills the card.\n        const int tileSize = face.w - 34;\n        const Box insetArt{face.x + 17, face.y + (face.h - tileSize) / 2 - 13, tileSize, tileSize};\n        const SDL_Color tone = mix(base, tint, 0.31f);\n        rounded(r, {face.x + 10, face.y + 10, face.w - 20, face.h - 84}, 16, tone);\n        const int eyebrowX = face.x + 17;\n        fill(r, {eyebrowX, face.y + 31, face.w - 34, 1}, alpha(Ink, 48));\n        hooks.text(kindLabel(tile.kind), eyebrowX, face.y + 17, 1, ink, face.x + face.w - 17, true);\n        if (hasArt) {\n            rounded(r, inset(insetArt, -1), 15, alpha(Pale, 98));\n            art(r, image, insetArt, 14);\n        } else {\n            panel(r, insetArt, 16, mix(base, tint, 0.56f), alpha(Pale, 52));\n            icon(r, insetArt, isApp(tile) ? Tool::Library : Tool::Tools, Pale);\n        }\n        hooks.text(tile.title, face.x + 17, face.y + face.h - 55, 2, ink,\n                   face.x + face.w - 17, false);\n        const int rule = selected ? 38 : 21;\n        rounded(r, {face.x + 17, face.y + face.h - 24, rule, 3}, 2, selected ? accent : alpha(Pale, 109));\n    }\n    if (online > 0) {\n        const Box badge{b.x + b.w - 76, b.y + 12, 61, 24};\n        panel(r, badge, 12, {8, 30, 28, 243}, {135, 238, 186, 97});\n        rounded(r, {badge.x + 8, badge.y + 8, 8, 8}, 4, {111, 245, 173, 255});\n        hooks.text(std::to_string(online), badge.x + 22, badge.y + 5, 1, Ink, badge.x + badge.w - 4, false);\n    }\n}\n\nvoid primaryButton(SDL_Renderer* r, const DrawHooks& hooks, const char* label, bool playable) {\n    rounded(r, {Primary.x, Primary.y + 4, Primary.w, Primary.h}, 28, Shadow);\n    rounded(r, Primary, 28, Ink);\n    if (playable) {\n        color(r, Night);\n        for (int i = 0; i < 10; ++i) {\n            const int h = 7 - (i * 7 / 10);\n            SDL_RenderDrawLine(r, Primary.x + 26 + i, Primary.y + 27 - h,\n                              Primary.x + 26 + i, Primary.y + 27 + h);\n        }\n    } else {\n        fill(r, {Primary.x + 23, Primary.y + 26, 14, 2}, Night);\n        fill(r, {Primary.x + 29, Primary.y + 20, 2, 14}, Night);\n    }\n    hooks.text(label, Primary.x + 50, Primary.y + 16, 2, Night, Primary.x + Primary.w - 14, true);\n}\n\n} // namespace\n\nvoid HomeRenderer::backdrop(SDL_Renderer* r, Theme theme) {\n    if (!backdrop_ || !same(theme.top, lastTheme_.top) || !same(theme.bottom, lastTheme_.bottom) ||\n        !same(theme.accent, lastTheme_.accent)) {\n        release();\n        constexpr int w = 320, h = 180;\n        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);\n        if (surface) {\n            for (int y = 0; y < h; ++y) {\n                auto* row = static_cast<Uint8*>(surface->pixels) + y * surface->pitch;\n                const float fy = y / static_cast<float>(h - 1);\n                for (int x = 0; x < w; ++x) {\n                    const float fx = x / static_cast<float>(w - 1);\n                    const float left = (fx - .19f) * (fx - .19f) * 7.4f + (fy - .28f)*(fy - .28f)*7.4f;\n                    const float right = (fx - .89f)*(fx - .89f)*8.f + (fy - .58f)*(fy - .58f)*10.f;\n                    const float wave = fy - (.21f + .43f * fx + .09f * std::sin(fx * 5.f));\n                    SDL_Color c = mix(theme.top, theme.bottom, fy);\n                    c = mix(c, theme.accent, .42f * std::exp(-left) + .19f * std::exp(-wave * wave * 70.f));\n                    c = mix(c, mix(theme.accent, {69, 79, 193, 255}, .55f), .36f * std::exp(-right));\n                    // A small amount of dither breaks up banding on the\n                    // console's 8-bit panel without showing as texture.\n                    const int grain = ((x * 7 + y * 13) % 3) - 1;\n                    row[4*x] = static_cast<Uint8>(std::clamp(c.r + grain, 0, 255));\n                    row[4*x+1] = static_cast<Uint8>(std::clamp(c.g + grain, 0, 255));\n                    row[4*x+2] = static_cast<Uint8>(std::clamp(c.b + grain, 0, 255));\n                    row[4*x+3] = 255;\n                }\n            }\n            backdrop_ = SDL_CreateTextureFromSurface(r, surface);\n            SDL_FreeSurface(surface);\n        }\n        lastTheme_ = theme;\n    }\n    const auto dst = rect({0, 0, Width, Height});\n    if (!backdrop_ || SDL_RenderCopy(r, backdrop_, nullptr, &dst) != 0)\n        fill(r, {0, 0, Width, Height}, theme.bottom);\n}\n\nvoid HomeRenderer::draw(SDL_Renderer* r, const HomeState& view, const DrawHooks& hooks) const {\n    const auto& workspace = view.workspaces[static_cast<std::size_t>(view.workspace)];\n    const auto& deck = view.deck;\n    const model::Tile* selectedTile = deck.size > 0\n        ? &workspace.tiles[static_cast<std::size_t>(deck.indices[static_cast<std::size_t>(view.selected)])] : nullptr;\n    SDL_Texture* selectedArt = selectedTile ? hooks.artwork(*selectedTile) : nullptr;\n\n    panel(r, {41, 24, 39, 39}, 12, {24, 53, 77, 246});\n    fill(r, {50, 35, 5, 17}, view.accent);\n    fill(r, {58, 31, 5, 21}, Ink);\n    fill(r, {66, 38, 5, 14}, view.accent);\n    hooks.text(\"DeskNX\", 93, 23, 4, Ink, 296, false);\n    hooks.text(\"HOME LAUNCHER\", 95, 61, 1, Soft, 294, true);\n\n    panel(r, Profile, 26);\n    rounded(r, {326, 31, 36, 36}, 18, {48, 80, 107, 255});\n    rounded(r, {341, 39, 7, 7}, 4, Ink);\n    rounded(r, {336, 49, 17, 10}, 5, Ink);\n    hooks.text(view.player, 373, 37, 2, Ink, 555, false);\n    fill(r, {567, 36, 1, 26}, Edge);\n    hooks.text(view.time, 585, 37, 2, Ink, 651, false);\n    fill(r, {657, 36, 1, 26}, Edge);\n    hooks.text(view.battery, 672, 38, 2, Ink, 735, false);\n    fill(r, {743, 36, 1, 26}, Edge);\n    hooks.text(view.storage, 757, 38, 2, Ink, 850, false);\n    for (int i = 0; i < static_cast<int>(Tool::Count); ++i) {\n        const auto which = static_cast<Tool>(i);\n        Box bounds = tool(which);\n        panel(r, bounds, 25);\n        icon(r, bounds, which, Ink);\n    }\n\n    fill(r, {43, 111, 5, 18}, view.accent);\n    hooks.text(workspace.name, 61, 99, 3, Ink, 710, true);\n    if (!view.network.empty()) {\n        panel(r, {1022, 100, 215, 33}, 17, Glass, alpha(view.accent, 100));\n        hooks.text(view.network, 1036, 109, 1, Pale, 1227, true);\n    }\n\n    if (!selectedTile) {\n        panel(r, {364, 235, 552, 140}, 23);\n        hooks.text(\"THIS DESKTOP IS EMPTY\", 394, 267, 3, Ink, 885, true);\n        hooks.text(\"Add games, homebrew and widgets to get started.\", 396, 313, 2, Soft, 883, true);\n    } else {\n        // Focused art is queued first; only visible neighbors borrow textures.\n        // The focused card is painted last so its bright edge owns the overlap.\n        auto drawAt = [&](int i) {\n            const Box bounds = card(i, view.anchor);\n            if (!visible(bounds)) return;\n            const auto& tile = workspace.tiles[static_cast<std::size_t>(deck.indices[static_cast<std::size_t>(i)])];\n            SDL_Texture* image = i == view.selected ? selectedArt : hooks.artwork(tile);\n            const unsigned online = tile.kind == model::Kind::Game ? hooks.online(tile.data) : 0;\n            poster(r, hooks, tile, image, bounds, view.accent, i == view.selected, online);\n        };\n        for (int i = 0; i < deck.size; ++i) if (i != view.selected) drawAt(i);\n        drawAt(view.selected);\n    }\n\n    rounded(r, {Detail.x, Detail.y + 6, Detail.w, Detail.h}, 27, Shadow);\n    panel(r, Detail, 27, Glass, alpha(Pale, 112));\n    const Box thumbnail{197, 513, 67, 67};\n    panel(r, thumbnail, 14, {34, 60, 84, 255});\n    if (!art(r, selectedArt, inset(thumbnail, 2), 12))\n        icon(r, thumbnail, selectedTile && !isApp(*selectedTile) ? Tool::Tools : Tool::Library, Pale);\n    hooks.text(selectedTile ? selectedTile->title : \"APP LIBRARY\", 281, 508, 4, Ink, 789, !selectedTile);\n    if (selectedTile) {\n        hooks.text(kindLabel(selectedTile->kind), 284, 555, 1, view.accent, 433, true);\n        const std::string index = std::to_string(view.selected + 1) + \" / \" + std::to_string(deck.size);\n        hooks.text(index, 439, 552, 2, Soft, 560, false);\n    } else {\n        hooks.text(\"Y  ADD APPS\", 284, 555, 1, view.accent, 520, true);\n    }\n    hooks.text(view.status.empty() ? \"L/R  SPACES     X  EDIT     ZL  VIEW\" : view.status,\n               201, 594, 1, Soft, 1080, true);\n    primaryButton(r, hooks, selectedTile ? (selectedTile->kind == model::Kind::Game ? \"PLAY\" : \"OPEN\")\n                                        : \"ADD APPS\", selectedTile && isApp(*selectedTile));\n    panel(r, Actions, 28, {28, 51, 75, 230}, Edge);\n    icon(r, Actions, Tool::Tools, Pale);\n    if (selectedTile) {\n        const int bar = 160 / deck.size;\n        for (int i = 0; i < deck.size; ++i)\n            rounded(r, {831 + i * bar, 588, bar - 3, 4}, 2,\n                    i == view.selected ? view.accent : alpha(Pale, 78));\n    }\n\n    panel(r, Footer, 24, Glass);\n    for (int slot = 0; slot < VisibleTabs; ++slot) {\n        const int index = view.firstTab + slot;\n        if (index >= static_cast<int>(view.workspaces.size())) break;\n        const Box bounds = tab(slot);\n        const bool active = index == view.workspace;\n        panel(r, bounds, 17, active ? SDL_Color{37, 72, 104, 247} : SDL_Color{21, 42, 64, 224},\n              active ? alpha(view.accent, 164) : alpha(Pale, 45));\n        hooks.text(view.workspaces[static_cast<std::size_t>(index)].name,\n                   bounds.x + 12, bounds.y + 15, 2, active ? Ink : Soft, bounds.x + bounds.w - 10, true);\n        if (active) rounded(r, {bounds.x + 12, bounds.y + 39, 22, 2}, 1, view.accent);\n    }\n    panel(r, AddDesk, 17, {23, 49, 75, 240});\n    const SDL_Color add =\n        view.workspaces.size() < static_cast<std::size_t>(model::MaxDesks) ? Ink : Soft;\n    fill(r, {AddDesk.x + 19, AddDesk.y + 23, 14, 2}, add);\n    fill(r, {AddDesk.x + 25, AddDesk.y + 17, 2, 14}, add);\n    panel(r, Workspaces, 17);\n    icon(r, Workspaces, Tool::Tools, Ink);\n    panel(r, WorkspaceCount, 17);\n    hooks.text(\"L / R\", WorkspaceCount.x + 12, WorkspaceCount.y + 16, 1, view.accent, WorkspaceCount.x + 60, false);\n    hooks.text(std::to_string(view.workspace + 1) + \" / \" + std::to_string(view.workspaces.size()),\n               WorkspaceCount.x + 68, WorkspaceCount.y + 12, 2, Ink, WorkspaceCount.x + 141, false);\n    panel(r, Arrange, 17, {28, 52, 77, 240});\n    rounded(r, {Arrange.x + 10, Arrange.y + 12, 25, 25}, 12, {53, 84, 112, 255});\n    hooks.text(\"X\", Arrange.x + 18, Arrange.y + 15, 2, Ink, Arrange.x + 35, false);\n    hooks.text(\"EDIT\", Arrange.x + 44, Arrange.y + 15, 2, Ink, Arrange.x + Arrange.w - 8, true);\n}\n\nvoid HomeRenderer::release() {\n    if (backdrop_) SDL_DestroyTexture(backdrop_);\n    backdrop_ = nullptr;\n}\n\n} // namespace desknx::cards\n"}