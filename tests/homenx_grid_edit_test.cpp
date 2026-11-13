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

void testResizeToUnsupportedShapeIsRefused() {
    WorkspaceProvider provider = makeProvider();
    const Workspace before = *provider.activeWorkspace();

    // 4x1 and 2x3 are not part of the native shape set (1x1, 2x1, 3x1, 2x2, 3x2).
    assert(!provider.resizeTile(0, 0, 0, 4, 1));
    assert(!provider.resizeTile(0, 0, 0, 2, 3));

    assert(provider.activeWorkspace()->tiles[0].w == before.tiles[0].w);
    assert(provider.activeWorkspace()->tiles[0].h == before.tiles[0].h);
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

} // namespace

int main() {
    testMoveIntoEmptyCell();
    testMoveOntoOccupiedCellIsRefusedAndChangesNothing();
    testMoveOffGridIsRefused();
    testMoveToSameCellIsNotAnEdit();
    testMoveOfUnknownTileIsRefused();
    testResizeInPlace();
    testResizeToUnsupportedShapeIsRefused();
    testResizeWithoutRoomIsRefused();
    testResizeThenMoveKeepsGeometryValid();
    std::puts("homenx_grid_edit_test: ok");
    return 0;
}

{"content":"// Android-style direct manipulation on the active desktop: dragging a tile to a\n// new cell and resizing it from a corner handle. Both must either apply fully or\n// leave the workspace exactly as it was - a half-applied drag corrupts a layout\n// that is then persisted to the SD card.\n#include <desknx/HomeNXWorkspaceProvider.hpp>\n\n#include <cassert>\n#include <cstdio>\n#include <string>\n\nusing namespace desknx::model;\nusing namespace desknx::homenx;\n\nnamespace {\n\nTile makeTile(Kind kind, const std::string& title, const std::string& data, int x, int y, int w,\n              int h) {\n    Tile tile{};\n    tile.kind = kind;\n    tile.title = title;\n    tile.data = data;\n    tile.x = x;\n    tile.y = y;\n    tile.w = w;\n    tile.h = h;\n    return tile;\n}\n\nWorkspaceProvider makeProvider() {\n    Workspace desk{\"Home\", {}};\n    desk.tiles.push_back(makeTile(Kind::Clock, \"Clock\", \"widget://clock\", 0, 0, 1, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"Game\", \"0100000000001000\", 1, 0, 2, 1));\n\n    WorkspaceProvider provider;\n    assert(provider.load(std::vector<Workspace>{desk}, 0));\n    return provider;\n}\n\nvoid testMoveIntoEmptyCell() {\n    WorkspaceProvider provider = makeProvider();\n    const Workspace* desk = provider.activeWorkspace();\n    assert(desk != nullptr);\n    assert(desk->tiles.size() == 2);\n\n    // (0,0) -> (0,1): empty, so the drag is accepted.\n    assert(provider.moveTile(0, 0, 1));\n    assert(provider.activeWorkspace()->tiles[0].x == 0);\n    assert(provider.activeWorkspace()->tiles[0].y == 1);\n\n    // Identity and size survive the move.\n    assert(provider.activeWorkspace()->tiles[0].kind == Kind::Clock);\n    assert(provider.activeWorkspace()->tiles[0].w == 1);\n}\n\nvoid testMoveOntoOccupiedCellIsRefusedAndChangesNothing() {\n    WorkspaceProvider provider = makeProvider();\n    const Workspace before = *provider.activeWorkspace();\n\n    // The 2x1 game occupies (1,0)-(2,0); dropping the clock there must fail.\n    assert(!provider.moveTile(0, 1, 0));\n\n    const Workspace& after = *provider.activeWorkspace();\n    assert(after.tiles.size() == before.tiles.size());\n    assert(after.tiles[0].x == before.tiles[0].x);\n    assert(after.tiles[0].y == before.tiles[0].y);\n}\n\nvoid testMoveOffGridIsRefused() {\n    WorkspaceProvider provider = makeProvider();\n    assert(!provider.moveTile(1, 5, 0));  // 2-wide tile would end at column 7\n    assert(!provider.moveTile(1, -1, 0));\n    assert(!provider.moveTile(1, 0, 3));  // below the third row\n    assert(provider.activeWorkspace()->tiles[1].x == 1);\n    assert(provider.activeWorkspace()->tiles[1].y == 0);\n}\n\nvoid testMoveToSameCellIsNotAnEdit() {\n    WorkspaceProvider provider = makeProvider();\n    assert(!provider.moveTile(0, 0, 0));\n}\n\nvoid testMoveOfUnknownTileIsRefused() {\n    WorkspaceProvider provider = makeProvider();\n    assert(!provider.moveTile(99, 0, 1));\n}\n\nvoid testResizeInPlace() {\n    WorkspaceProvider provider = makeProvider();\n    // The clock at (0,0) can grow to 2x2 only if (1,0) were free; the game is\n    // there, so this must relocate instead of overwriting it.\n    assert(provider.resizeTile(0, 0, 0, 2, 2));\n\n    const Workspace& desk = *provider.activeWorkspace();\n    const Tile& clock = desk.tiles[0];\n    assert(clock.w == 2 && clock.h == 2);\n    assert(!overlap(clock, desk.tiles[1]));\n}\n\nvoid testResizeToUnsupportedShapeIsRefused() {\n    WorkspaceProvider provider = makeProvider();\n    const Workspace before = *provider.activeWorkspace();\n\n    // 4x1 and 2x3 are not part of the native shape set (1x1, 2x1, 3x1, 2x2, 3x2).\n    assert(!provider.resizeTile(0, 0, 0, 4, 1));\n    assert(!provider.resizeTile(0, 0, 0, 2, 3));\n\n    assert(provider.activeWorkspace()->tiles[0].w == before.tiles[0].w);\n    assert(provider.activeWorkspace()->tiles[0].h == before.tiles[0].h);\n}\n\nvoid testResizeWithoutRoomIsRefused() {\n    Workspace desk{\"Full\", {}};\n    // Fill two full rows with 3x1 tiles, leaving only the last row.\n    desk.tiles.push_back(makeTile(Kind::Game, \"a\", \"0100000000001000\", 0, 0, 3, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"b\", \"0100000000001001\", 3, 0, 3, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"c\", \"0100000000001002\", 0, 1, 3, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"d\", \"0100000000001003\", 3, 1, 3, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"e\", \"0100000000001004\", 0, 2, 3, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"f\", \"0100000000001005\", 3, 2, 3, 1));\n\n    WorkspaceProvider provider;\n    assert(provider.load(std::vector<Workspace>{desk}, 0));\n\n    // Growing any tile to 3x2 needs a second row it does not have.\n    assert(!provider.resizeTile(0, 0, 0, 3, 2));\n    assert(provider.activeWorkspace()->tiles[0].w == 3);\n    assert(provider.activeWorkspace()->tiles[0].h == 1);\n}\n\nvoid testResizeThenMoveKeepsGeometryValid() {\n    WorkspaceProvider provider = makeProvider();\n    assert(provider.resizeTile(1, 1, 0, 3, 1));  // game becomes 3 wide at (1,0)\n    assert(provider.activeWorkspace()->tiles[1].w == 3);\n\n    // A 3-wide tile starting at column 4 would end past the sixth column.\n    assert(!provider.moveTile(1, 4, 0));\n    // Starting at column 3 ends exactly on the last column.\n    assert(provider.moveTile(1, 3, 0));\n    assert(provider.activeWorkspace()->tiles[1].x == 3);\n\n    // Down to row 1 is free, and the tile keeps its 3-wide size.\n    assert(provider.moveTile(1, 3, 1));\n    assert(provider.activeWorkspace()->tiles[1].y == 1);\n    assert(provider.activeWorkspace()->tiles[1].w == 3);\n}\n\n} // namespace\n\nint main() {\n    testMoveIntoEmptyCell();\n    testMoveOntoOccupiedCellIsRefusedAndChangesNothing();\n    testMoveOffGridIsRefused();\n    testMoveToSameCellIsNotAnEdit();\n    testMoveOfUnknownTileIsRefused();\n    testResizeInPlace();\n    testResizeToUnsupportedShapeIsRefused();\n    testResizeWithoutRoomIsRefused();\n    testResizeThenMoveKeepsGeometryValid();\n    std::puts(\"homenx_grid_edit_test: ok\");\n    return 0;\n}\n"}