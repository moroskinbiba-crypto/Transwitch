#include <desknx/HomeNXGridAdapter.hpp>

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

// Clock (0,0) 1x1 | Game 2x1 (1,0) | Game 3x2 (3,0)
// Battery (0,1) 1x1 | empty ......... | (covered by the 3x2)
// Recent 3x1 (0,2) | empty .......... | empty
Workspace sampleWorkspace() {
    Workspace desk{"Home", {}};
    desk.tiles.push_back(makeTile(Kind::Clock, "Clock", "widget://clock", 0, 0, 1, 1));
    desk.tiles.push_back(makeTile(Kind::Game, "Mario", "0100000000001000", 1, 0, 2, 1));
    desk.tiles.push_back(makeTile(Kind::Nro, "Homebrew", "sdmc:/switch/app.nro", 3, 0, 3, 2));
    desk.tiles.push_back(makeTile(Kind::Battery, "Battery", "widget://battery", 0, 1, 1, 1));
    desk.tiles.push_back(makeTile(Kind::Recent, "Recent", "widget://recent", 0, 2, 3, 1));
    return desk;
}

WorkspaceProjection project() {
    WorkspaceProjection projection;
    ProjectionError error = ProjectionError::None;
    const bool ok = projectWorkspace(sampleWorkspace(), projection, &error);
    assert(ok);
    assert(error == ProjectionError::None);
    return projection;
}

void testDisplayOrderIsReadingOrder() {
    const WorkspaceProjection p = project();
    const auto ordered = displayOrder(p);

    assert(ordered.size() == 5);
    assert(ordered[0]->title == "Clock");    // (0,0)
    assert(ordered[1]->title == "Mario");    // (1,0)
    assert(ordered[2]->title == "Homebrew"); // (3,0)
    assert(ordered[3]->title == "Battery");  // (0,1)
    assert(ordered[4]->title == "Recent");   // (0,2)
}

void testContinuationCellsResolveToPrimary() {
    const WorkspaceProjection p = project();

    // Every cell of the 3x2 tile reports the same primary cell.
    for (int y = 0; y < 2; ++y) {
        for (int x = 3; x < 6; ++x) {
            const GridCellRef primary = primaryOf(p, x, y);
            assert(primary.valid());
            assert(primary.x == 3 && primary.y == 0);

            const Entry* entry = entryAt(p, x, y);
            assert(entry != nullptr);
            assert(entry->title == "Homebrew");
            assert(entryIndexAt(p, x, y) == entryIndexAt(p, 3, 0));
        }
    }

    // Empty cells own nothing.
    assert(!primaryOf(p, 1, 1).valid());
    assert(entryAt(p, 1, 1) == nullptr);
    assert(entryIndexAt(p, 1, 1) == -1);

    // Out-of-range cells are rejected, not read out of bounds.
    assert(!primaryOf(p, -1, 0).valid());
    assert(entryAt(p, 6, 0) == nullptr);
}

void testDirectionalFocus() {
    const WorkspaceProjection p = project();

    // Right from the clock lands on the 2x1 game, not on the empty cell below.
    const GridCellRef right = focusNeighbour(p, 0, 0, Direction::Right);
    assert(sameCell(right, GridCellRef{1, 0}));

    // Right from the game skips its continuation cell and reaches the 3x2 tile.
    const GridCellRef rightAgain = focusNeighbour(p, 1, 0, Direction::Right);
    assert(sameCell(rightAgain, GridCellRef{3, 0}));

    // Focus addressed from inside a tile behaves like its primary cell: left of
    // the 3x2 tile is the tile whose right edge touches it and overlaps
    // vertically, i.e. the 2x1 game - not the clock further left.
    const GridCellRef fromInside = focusNeighbour(p, 4, 1, Direction::Left);
    assert(sameCell(fromInside, GridCellRef{1, 0}));

    // Down from the clock reaches the battery directly below it.
    const GridCellRef down = focusNeighbour(p, 0, 0, Direction::Down);
    assert(sameCell(down, GridCellRef{0, 1}));

    // Up from the battery returns to the clock.
    const GridCellRef up = focusNeighbour(p, 0, 1, Direction::Up);
    assert(sameCell(up, GridCellRef{0, 0}));

    // Nothing above the clock, nothing left of the battery: focus stays put.
    assert(!focusNeighbour(p, 0, 0, Direction::Up).valid());
    assert(!focusNeighbour(p, 0, 1, Direction::Left).valid());
}

void testFocusOnEmptyCellRecovers() {
    WorkspaceProjection p = project();
    // An empty cell must not swallow input; it recovers to reading order.
    const GridCellRef recovered = focusNeighbour(p, 2, 1, Direction::Right);
    assert(sameCell(recovered, GridCellRef{0, 0}));
}

void testEmptyWorkspaceHasNoFocus() {
    WorkspaceProjection empty;
    assert(displayOrder(empty).empty());
    assert(!focusNeighbour(empty, 0, 0, Direction::Right).valid());
}

void testSingleTileCannotMove() {
    Workspace desk{"Solo", {}};
    desk.tiles.push_back(makeTile(Kind::Game, "Only", "0100000000001000", 0, 0, 2, 2));
    WorkspaceProjection p;
    assert(projectWorkspace(desk, p, nullptr));

    for (const Direction direction :
         {Direction::Up, Direction::Down, Direction::Left, Direction::Right}) {
        assert(!focusNeighbour(p, 0, 0, direction).valid());
    }
    assert(displayOrder(p).size() == 1);
}

} // namespace

int main() {
    testDisplayOrderIsReadingOrder();
    testContinuationCellsResolveToPrimary();
    testDirectionalFocus();
    testFocusOnEmptyCellRecovers();
    testEmptyWorkspaceHasNoFocus();
    testSingleTileCannotMove();
    std::puts("homenx_grid_adapter_test: ok");
    return 0;
}

{"content":"#include <desknx/HomeNXGridAdapter.hpp>\n\n#include <cassert>\n#include <cstdio>\n#include <string>\n\nusing namespace desknx::model;\nusing namespace desknx::homenx;\n\nnamespace {\n\nTile makeTile(Kind kind, const std::string& title, const std::string& data, int x, int y, int w,\n              int h) {\n    Tile tile{};\n    tile.kind = kind;\n    tile.title = title;\n    tile.data = data;\n    tile.x = x;\n    tile.y = y;\n    tile.w = w;\n    tile.h = h;\n    return tile;\n}\n\n// Clock (0,0) 1x1 | Game 2x1 (1,0) | Game 3x2 (3,0)\n// Battery (0,1) 1x1 | empty ......... | (covered by the 3x2)\n// Recent 3x1 (0,2) | empty .......... | empty\nWorkspace sampleWorkspace() {\n    Workspace desk{\"Home\", {}};\n    desk.tiles.push_back(makeTile(Kind::Clock, \"Clock\", \"widget://clock\", 0, 0, 1, 1));\n    desk.tiles.push_back(makeTile(Kind::Game, \"Mario\", \"0100000000001000\", 1, 0, 2, 1));\n    desk.tiles.push_back(makeTile(Kind::Nro, \"Homebrew\", \"sdmc:/switch/app.nro\", 3, 0, 3, 2));\n    desk.tiles.push_back(makeTile(Kind::Battery, \"Battery\", \"widget://battery\", 0, 1, 1, 1));\n    desk.tiles.push_back(makeTile(Kind::Recent, \"Recent\", \"widget://recent\", 0, 2, 3, 1));\n    return desk;\n}\n\nWorkspaceProjection project() {\n    WorkspaceProjection projection;\n    ProjectionError error = ProjectionError::None;\n    const bool ok = projectWorkspace(sampleWorkspace(), projection, &error);\n    assert(ok);\n    assert(error == ProjectionError::None);\n    return projection;\n}\n\nvoid testDisplayOrderIsReadingOrder() {\n    const WorkspaceProjection p = project();\n    const auto ordered = displayOrder(p);\n\n    assert(ordered.size() == 5);\n    assert(ordered[0]->title == \"Clock\");    // (0,0)\n    assert(ordered[1]->title == \"Mario\");    // (1,0)\n    assert(ordered[2]->title == \"Homebrew\"); // (3,0)\n    assert(ordered[3]->title == \"Battery\");  // (0,1)\n    assert(ordered[4]->title == \"Recent\");   // (0,2)\n}\n\nvoid testContinuationCellsResolveToPrimary() {\n    const WorkspaceProjection p = project();\n\n    // Every cell of the 3x2 tile reports the same primary cell.\n    for (int y = 0; y < 2; ++y) {\n        for (int x = 3; x < 6; ++x) {\n            const GridCellRef primary = primaryOf(p, x, y);\n            assert(primary.valid());\n            assert(primary.x == 3 && primary.y == 0);\n\n            const Entry* entry = entryAt(p, x, y);\n            assert(entry != nullptr);\n            assert(entry->title == \"Homebrew\");\n            assert(entryIndexAt(p, x, y) == entryIndexAt(p, 3, 0));\n        }\n    }\n\n    // Empty cells own nothing.\n    assert(!primaryOf(p, 1, 1).valid());\n    assert(entryAt(p, 1, 1) == nullptr);\n    assert(entryIndexAt(p, 1, 1) == -1);\n\n    // Out-of-range cells are rejected, not read out of bounds.\n    assert(!primaryOf(p, -1, 0).valid());\n    assert(entryAt(p, 6, 0) == nullptr);\n}\n\nvoid testDirectionalFocus() {\n    const WorkspaceProjection p = project();\n\n    // Right from the clock lands on the 2x1 game, not on the empty cell below.\n    const GridCellRef right = focusNeighbour(p, 0, 0, Direction::Right);\n    assert(sameCell(right, GridCellRef{1, 0}));\n\n    // Right from the game skips its continuation cell and reaches the 3x2 tile.\n    const GridCellRef rightAgain = focusNeighbour(p, 1, 0, Direction::Right);\n    assert(sameCell(rightAgain, GridCellRef{3, 0}));\n\n    // Focus addressed from inside a tile behaves like its primary cell: left of\n    // the 3x2 tile is the tile whose right edge touches it and overlaps\n    // vertically, i.e. the 2x1 game - not the clock further left.\n    const GridCellRef fromInside = focusNeighbour(p, 4, 1, Direction::Left);\n    assert(sameCell(fromInside, GridCellRef{1, 0}));\n\n    // Down from the clock reaches the battery directly below it.\n    const GridCellRef down = focusNeighbour(p, 0, 0, Direction::Down);\n    assert(sameCell(down, GridCellRef{0, 1}));\n\n    // Up from the battery returns to the clock.\n    const GridCellRef up = focusNeighbour(p, 0, 1, Direction::Up);\n    assert(sameCell(up, GridCellRef{0, 0}));\n\n    // Nothing above the clock, nothing left of the battery: focus stays put.\n    assert(!focusNeighbour(p, 0, 0, Direction::Up).valid());\n    assert(!focusNeighbour(p, 0, 1, Direction::Left).valid());\n}\n\nvoid testFocusOnEmptyCellRecovers() {\n    WorkspaceProjection p = project();\n    // An empty cell must not swallow input; it recovers to reading order.\n    const GridCellRef recovered = focusNeighbour(p, 2, 1, Direction::Right);\n    assert(sameCell(recovered, GridCellRef{0, 0}));\n}\n\nvoid testEmptyWorkspaceHasNoFocus() {\n    WorkspaceProjection empty;\n    assert(displayOrder(empty).empty());\n    assert(!focusNeighbour(empty, 0, 0, Direction::Right).valid());\n}\n\nvoid testSingleTileCannotMove() {\n    Workspace desk{\"Solo\", {}};\n    desk.tiles.push_back(makeTile(Kind::Game, \"Only\", \"0100000000001000\", 0, 0, 2, 2));\n    WorkspaceProjection p;\n    assert(projectWorkspace(desk, p, nullptr));\n\n    for (const Direction direction :\n         {Direction::Up, Direction::Down, Direction::Left, Direction::Right}) {\n        assert(!focusNeighbour(p, 0, 0, direction).valid());\n    }\n    assert(displayOrder(p).size() == 1);\n}\n\n} // namespace\n\nint main() {\n    testDisplayOrderIsReadingOrder();\n    testContinuationCellsResolveToPrimary();\n    testDirectionalFocus();\n    testFocusOnEmptyCellRecovers();\n    testEmptyWorkspaceHasNoFocus();\n    testSingleTileCannotMove();\n    std::puts(\"homenx_grid_adapter_test: ok\");\n    return 0;\n}\n"}