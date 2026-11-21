// Android-style direct manipulation on the active desktop: dragging a tile to a
// new cell and resizing it from a corner handle. Both must either apply fully or
// leave the workspace exactly as it was - a half-applied drag corrupts a layout
// that is then persisted to the SD card.
#include <desknx/HomeNXWorkspaceProvider.hpp>

#include <cassert>
#include <cstdio>
#include <string>

using namespace desknx::model;
using namespace desknx::homenx;

namespace {

Tile makeTile(Kind kind, const std::string& title, const std::string& data, int x, int y, int w,
              int h) {
    Tile tile{};
    tile.kind = kind;
    tile.title = title;
    tile.data = data;
    tile.x = x;
    tile.y = y;
    tile.w = w;
    tile.h = h;
    return tile;
}

WorkspaceProvider makeProvider() {
    Workspace desk{"Home", {}};
    desk.tiles.push_back(makeTile(Kind::Clock, "Clock", "widget://clock", 0, 0, 1, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "Game", "0100000000001000", 1, 0, 2, 1));

    WorkspaceProvider provider;
    assert(provider.load(std::vector<Workspace>{desk}, 0));
    return provider;
}

void testMoveIntoEmptyCell() {
    WorkspaceProvider provider = makeProvider();
    const Workspace* desk = provider.activeWorkspace();
    assert(desk != nullptr);
    assert(desk->tiles.size() == 2);

    // (0,0) -> (0,1): empty, so the drag is accepted.
    assert(provider.moveTile(0, 0, 1));
    assert(provider.activeWorkspace()->tiles[0].x == 0);
    assert(provider.activeWorkspace()->tiles[0].y == 1);

    // Identity and size survive the move.
    assert(provider.activeWorkspace()->tiles[0].kind == Kind::Clock);
    assert(provider.activeWorkspace()->tiles[0].w == 1);
}

void testMoveOntoOccupiedCellIsRefusedAndChangesNothing() {
    WorkspaceProvider provider = makeProvider();
    const Workspace before = *provider.activeWorkspace();

    // The 2x1 game occupies (1,0)-(2,0); dropping the clock there must fail.
    assert(!provider.moveTile(0, 1, 0));

    const Workspace& after = *provider.activeWorkspace();
    assert(after.tiles.size() == before.tiles.size());
    assert(after.tiles[0].x == before.tiles[0].x);
    assert(after.tiles[0].y == before.tiles[0].y);
}

void testMoveOffGridIsRefused() {
    WorkspaceProvider provider = makeProvider();
    assert(!provider.moveTile(1, 5, 0));  // 2-wide tile would end at column 7
    assert(!provider.moveTile(1, -1, 0));
    assert(!provider.moveTile(1, 0, 3));  // below the third row
    assert(provider.activeWorkspace()->tiles[1].x == 1);
    assert(provider.activeWorkspace()->tiles[1].y == 0);
}

void testMoveToSameCellIsNotAnEdit() {
    WorkspaceProvider provider = makeProvider();
    assert(!provider.moveTile(0, 0, 0));
}

void testMoveOfUnknownTileIsRefused() {
    WorkspaceProvider provider = makeProvider();
    assert(!provider.moveTile(99, 0, 1));
}

void testResizeInPlace() {
    WorkspaceProvider provider = makeProvider();
    // The clock at (0,0) can grow to 2x2 only if (1,0) were free; the game is
    // there, so this must relocate instead of overwriting it.
    assert(provider.resizeTile(0, 0, 0, 2, 2));

    const Workspace& desk = *provider.activeWorkspace();
    const Tile& clock = desk.tiles[0];
    assert(clock.w == 2 && clock.h == 2);
    assert(!overlap(clock, desk.tiles[1]));
}

void testResizeSupportsPortraitShapeButRejectsUnsupportedWidth() {
    Workspace desk{"Portrait", {}};
    desk.tiles.push_back(makeTile(Kind::Game, "Portrait", "0100000000002000", 4, 0, 2, 1));
    WorkspaceProvider provider;
    assert(provider.load(std::vector<Workspace>{desk}, 0));

    // 4x1 remains unsupported, while portrait 2x3 is now a supported game tile.
    assert(!provider.resizeTile(0, 4, 0, 4, 1));
    assert(provider.resizeTile(0, 4, 0, 2, 3));
    assert(provider.activeWorkspace()->tiles[0].w == 2);
    assert(provider.activeWorkspace()->tiles[0].h == 3);
}

void testResizeWithoutRoomIsRefused() {
    Workspace desk{"Full", {}};
    // Fill two full rows with 3x1 tiles, leaving only the last row.
    desk.tiles.push_back(makeTile(Kind::Game, "a", "0100000000001000", 0, 0, 3, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "b", "0100000000001001", 3, 0, 3, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "c", "0100000000001002", 0, 1, 3, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "d", "0100000000001003", 3, 1, 3, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "e", "0100000000001004", 0, 2, 3, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "f", "0100000000001005", 3, 2, 3, 1));

    WorkspaceProvider provider;
    assert(provider.load(std::vector<Workspace>{desk}, 0));

    // Growing any tile to 3x2 needs a second row it does not have.
    assert(!provider.resizeTile(0, 0, 0, 3, 2));
    assert(provider.activeWorkspace()->tiles[0].w == 3);
    assert(provider.activeWorkspace()->tiles[0].h == 1);
}

void testResizeThenMoveKeepsGeometryValid() {
    WorkspaceProvider provider = makeProvider();
    assert(provider.resizeTile(1, 1, 0, 3, 1));  // game becomes 3 wide at (1,0)
    assert(provider.activeWorkspace()->tiles[1].w == 3);

    // A 3-wide tile starting at column 4 would end past the sixth column.
    assert(!provider.moveTile(1, 4, 0));
    // Starting at column 3 ends exactly on the last column.
    assert(provider.moveTile(1, 3, 0));
    assert(provider.activeWorkspace()->tiles[1].x == 3);

    // Down to row 1 is free, and the tile keeps its 3-wide size.
    assert(provider.moveTile(1, 3, 1));
    assert(provider.activeWorkspace()->tiles[1].y == 1);
    assert(provider.activeWorkspace()->tiles[1].w == 3);
}

void testRemoveTile() {
    WorkspaceProvider provider = makeProvider();
    assert(provider.removeTile(0));
    assert(provider.activeWorkspace()->tiles.size() == 1);
    assert(provider.activeWorkspace()->tiles[0].title == "Game");

    // Stale index from an outdated frame is refused, not undefined behaviour.
    assert(!provider.removeTile(7));
    assert(provider.activeWorkspace()->tiles.size() == 1);
}

void testRemoveTileAtCoordinates() {
    WorkspaceProvider provider = makeProvider();

    // (2,0) is a continuation cell of the 2x1 game: removing by coordinates
    // must remove the whole tile, not just that cell.
    assert(provider.removeTileAt(2, 0));
    assert(provider.activeWorkspace()->tiles.size() == 1);
    assert(provider.activeWorkspace()->tiles[0].title == "Clock");

    // Empty cell removes nothing.
    assert(!provider.removeTileAt(5, 2));
    assert(provider.activeWorkspace()->tiles.size() == 1);
}

// The whole point of direct manipulation is that it survives a reboot: edits go
// through serializeLayout -> parse before they ever reach the SD card.
void testEditsSurviveSerializeAndReload() {
    WorkspaceProvider provider = makeProvider();
    assert(provider.resizeTile(1, 1, 0, 3, 2));   // game -> 3x2 at (1,0)
    assert(provider.moveTile(0, 5, 0));           // clock -> top-right corner
    assert(provider.addWorkspace("Second"));
    assert(!provider.removeTileAt(0, 2));         // empty cell: nothing to remove
    provider.switchTo(0);

    const std::string layout = provider.serializeLayout();
    const std::string active = provider.serializeActiveState();
    assert(!layout.empty());
    assert(!active.empty());

    WorkspaceProvider reloaded;
    assert(reloaded.loadSerialized(layout, active));
    assert(reloaded.count() == 2);
    assert(reloaded.activeIndex() == 0);

    const Workspace* desk = reloaded.activeWorkspace();
    assert(desk != nullptr);
    assert(desk->tiles.size() == 2);

    const Tile& clock = desk->tiles[0];
    assert(clock.kind == Kind::Clock);
    assert(clock.x == 5 && clock.y == 0 && clock.w == 1 && clock.h == 1);

    const Tile& game = desk->tiles[1];
    assert(game.kind == Kind::Game);
    assert(game.w == 3 && game.h == 2);
    assert(game.x == 1 && game.y == 0);
}

} // namespace

int main() {
    testMoveIntoEmptyCell();
    testMoveOntoOccupiedCellIsRefusedAndChangesNothing();
    testMoveOffGridIsRefused();
    testMoveToSameCellIsNotAnEdit();
    testMoveOfUnknownTileIsRefused();
    testResizeInPlace();
    testResizeSupportsPortraitShapeButRejectsUnsupportedWidth();
    testResizeWithoutRoomIsRefused();
    testResizeThenMoveKeepsGeometryValid();
    testRemoveTile();
    testRemoveTileAtCoordinates();
    testEditsSurviveSerializeAndReload();
    std::puts("homenx_grid_edit_test: ok");
    return 0;
}
