// Host test for the CardsNX-style home view geometry and deck ordering.
// Pure layout/selection logic: no SDL, no filesystem, no Horizon services.
#include "../include/desknx/CardsLayout.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using desknx::model::Kind;
using desknx::model::Tile;
using desknx::model::Workspace;

Workspace sample() {
    Workspace workspace;
    workspace.name = "Games";
    // Deliberately stored out of visual order: the deck must sort by position.
    workspace.tiles.push_back(Tile{Kind::Game, "Third", "0100000000003000", 4, 0, 2, 1, 2});
    workspace.tiles.push_back(Tile{Kind::Nro, "Fourth", "sdmc:/switch/a.nro", 0, 1, 1, 1, 3});
    workspace.tiles.push_back(Tile{Kind::Game, "First", "0100000000001000", 0, 0, 2, 2, 0});
    workspace.tiles.push_back(Tile{Kind::Clock, "Clock", "", 2, 0, 2, 1, 1});
    return workspace;
}

void testDeckOrdersByGridPosition() {
    const Workspace workspace = sample();
    const auto deck = desknx::cards::makeDeck(workspace);
    check(deck.size == 4, "every tile appears exactly once in the deck");
    check(workspace.tiles[static_cast<std::size_t>(deck.indices[0])].title == "First",
          "row-major order starts at the top-left tile");
    check(workspace.tiles[static_cast<std::size_t>(deck.indices[1])].title == "Clock",
          "the second card is the next tile on the first row");
    check(workspace.tiles[static_cast<std::size_t>(deck.indices[2])].title == "Third",
          "wide tiles keep their single origin slot");
    check(workspace.tiles[static_cast<std::size_t>(deck.indices[3])].title == "Fourth",
          "the second grid row follows the first");

    std::set<int> unique;
    for (int i = 0; i < deck.size; ++i) unique.insert(deck.indices[static_cast<std::size_t>(i)]);
    check(unique.size() == 4, "a multi-cell tile is never duplicated as several cards");
}

void testDeckIgnoresCorruptCoordinates() {
    Workspace workspace;
    workspace.tiles.push_back(Tile{Kind::Game, "Bad", "0100000000009000", -1, 0, 1, 1, 0});
    workspace.tiles.push_back(Tile{Kind::Game, "Off", "010000000000A000", 0, 9, 1, 1, 0});
    workspace.tiles.push_back(Tile{Kind::Game, "Good", "010000000000B000", 1, 1, 1, 1, 0});
    const auto deck = desknx::cards::makeDeck(workspace);
    check(deck.size == 1, "out-of-grid tiles are skipped instead of indexing out of bounds");
    check(workspace.tiles[static_cast<std::size_t>(deck.indices[0])].title == "Good",
          "the valid tile survives");
}

void testSelectionMapsFromGridCursorAndClamps() {
    const Workspace workspace = sample();
    const auto deck = desknx::cards::makeDeck(workspace);
    const int thirdTile = 0; // "Third" is tiles[0]
    check(deck.positionOf(thirdTile) == 2, "the grid cursor maps onto the matching card");
    check(deck.positionOf(99) == 0, "an empty cell falls back to the first card");
    check(deck.step(0, -1) == 0, "the rail stops at the first card instead of wrapping");
    check(deck.step(3, 1) == 3, "the rail stops at the last card instead of wrapping");
    check(deck.step(1, 1) == 2, "stepping right advances exactly one card");
    check(deck.step(7, -1) == 2, "an out-of-range selection is clamped before stepping");

    const desknx::cards::Deck empty;
    check(empty.step(0, 1) == -1, "an empty desktop reports no selectable card");
}

void testHasApp() {
    Workspace widgets;
    widgets.tiles.push_back(Tile{Kind::Clock, "Clock", "", 0, 0, 2, 1, 0});
    check(!desknx::cards::hasApp(widgets), "a widgets-only desktop has no launchable app");
    check(desknx::cards::hasApp(sample()), "a desktop with games reports a launchable app");
}

void testCardGeometry() {
    const desknx::cards::Box focused = desknx::cards::card(2, 2.f);
    const desknx::cards::Box right = desknx::cards::card(3, 2.f);
    const desknx::cards::Box left = desknx::cards::card(1, 2.f);
    check(focused.x + focused.w / 2 == desknx::cards::Width / 2,
          "the focused card is centred on screen");
    check(focused.w > right.w && focused.h > right.h, "the focused card is the largest");
    check(right.w == left.w && right.h == left.h, "neighbours are symmetric");
    check(left.x + left.w < right.x + right.w, "cards advance left to right");
    check(focused.y >= desknx::cards::RailTop && focused.y + focused.h <= desknx::cards::RailBottom,
          "cards stay inside the rail band, above the detail panel");
    check(right.x > focused.x + focused.w / 2, "neighbouring cards do not cover the focused title");

    const desknx::cards::Box half = desknx::cards::card(2, 2.5f);
    check(half.w < focused.w && half.w > right.w, "mid-animation sizes interpolate smoothly");
    check(!desknx::cards::visible(desknx::cards::card(9, 0.f)), "far cards are skipped when off-screen");
    check(desknx::cards::visible(desknx::cards::card(0, 0.f)), "the focused card is always drawn");
}

void testHitTesting() {
    constexpr float anchor = 2.f;
    const desknx::cards::Box focused = desknx::cards::card(2, anchor);
    check(desknx::cards::hitCard(5, anchor, focused.x + 4, focused.y + 4) == 2,
          "a tap inside the focused card selects it");
    check(desknx::cards::hitCard(5, anchor, desknx::cards::Width / 2, desknx::cards::RailBottom + 10) == -1,
          "a tap on the detail panel does not select a card");
    check(desknx::cards::hitCard(5, anchor, desknx::cards::Width / 2, desknx::cards::RailTop - 12) == -1,
          "a tap on the header does not select a card");
    const desknx::cards::Box neighbour = desknx::cards::card(3, anchor);
    const int hit = desknx::cards::hitCard(5, anchor, neighbour.x + neighbour.w - 3, neighbour.y + 30);
    check(hit == 3, "a tap on a neighbour selects that neighbour");
    check(desknx::cards::hitCard(0, anchor, desknx::cards::Width / 2, desknx::cards::RailTop + 30) == -1,
          "an empty desktop has no hit targets");
}

void testSwipe() {
    check(desknx::cards::swipeStep(-200, 10) > 0, "swiping left advances to later cards");
    check(desknx::cards::swipeStep(200, 10) < 0, "swiping right goes back");
    check(desknx::cards::swipeStep(-20, 2) == 0, "a tap is not a swipe");
    check(desknx::cards::swipeStep(-120, 240) == 0, "a vertical drag is not a page swipe");
    check(desknx::cards::swipeStep(-900, 10) == 3, "a long flick is capped at three cards");
}

void testControlsDoNotOverlap() {
    const desknx::cards::Box boxes[] = {
        desknx::cards::Primary, desknx::cards::Actions,
        desknx::cards::AddDesk, desknx::cards::Workspaces,
        desknx::cards::WorkspaceCount, desknx::cards::Arrange,
        desknx::cards::tab(0), desknx::cards::tab(desknx::cards::VisibleTabs - 1)
    };
    for (const auto& box : boxes) {
        check(box.x >= 0 && box.x + box.w <= desknx::cards::Width, "a control stays on screen");
        check(box.y >= 0 && box.y + box.h <= desknx::cards::Height, "a control stays on screen");
        check(box.w >= 44 && box.h >= 44, "every touch target is at least 44 px");
    }
    check(desknx::cards::tab(1).x >= desknx::cards::tab(0).x + desknx::cards::tab(0).w,
          "workspace tabs do not overlap");
    check(desknx::cards::tab(desknx::cards::VisibleTabs - 1).x +
          desknx::cards::tab(desknx::cards::VisibleTabs - 1).w <= desknx::cards::AddDesk.x,
          "the last visible tab does not collide with the add-desktop button");
    check(desknx::cards::Primary.x + desknx::cards::Primary.w <= desknx::cards::Actions.x,
          "Play and the overflow button do not overlap");
    check(desknx::cards::Actions.x + desknx::cards::Actions.w <=
          desknx::cards::Detail.x + desknx::cards::Detail.w,
          "detail controls stay inside the detail panel");
    check(desknx::cards::Detail.y + desknx::cards::Detail.h <= desknx::cards::Footer.y,
          "the detail panel does not cover the workspace switcher");
    check(desknx::cards::RailBottom <= desknx::cards::Detail.y, "the card rail clears the detail panel");
    check(desknx::cards::Profile.x + desknx::cards::Profile.w <=
          desknx::cards::tool(desknx::cards::Tool::Library).x,
          "the status pill does not overlap the toolbar");
    const auto last = desknx::cards::tool(static_cast<desknx::cards::Tool>(
        static_cast<int>(desknx::cards::Tool::Count) - 1));
    check(last.x + last.w <= desknx::cards::Width, "the toolbar fits on screen");
}

} // namespace

int main() {
    testDeckOrdersByGridPosition();
    testDeckIgnoresCorruptCoordinates();
    testSelectionMapsFromGridCursorAndClamps();
    testHasApp();
    testCardGeometry();
    testHitTesting();
    testSwipe();
    testControlsDoNotOverlap();
    if (failures != 0) {
        std::printf("cards layout test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("cards layout test: all checks passed\n");
    return EXIT_SUCCESS;
}
