// Copyright contributors to the DeskNX project.
// SPDX-License-Identifier: MIT
//
// Host-side drag/drop unit tests (no SDL, no libnx).
// Compile: zig c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -Iprojects/DeskNXMenu/source
//          tests/drag_drop_test.cpp projects/DeskNXMenu/source/TileInputHandler.cpp -o /tmp/dd-test
// Run:     /tmp/dd-test

#include <desknx/Model.hpp>
#include <cassert>
#include <cstdio>

// Minimal stubs so TileInputHandler.cpp compiles without SDL/libnx
#ifndef debugPrintf
#  define debugPrintf(...) (void)0
#endif

#include "TileInputHandler.hpp"

using namespace desknx;
using namespace desknx::menu::input;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static model::Workspace makeWorkspace() {
    model::Workspace ws;
    ws.name = "Test";
    return ws;
}

static model::Tile makeTile(int x, int y, int w = 1, int h = 1) {
    model::Tile t;
    t.x = x; t.y = y; t.w = w; t.h = h;
    t.kind = model::Kind::Game;
    t.title = "Game";
    return t;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static int passed = 0;
static int failed = 0;

#define CHECK(cond) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while(0)

void test_valid_move() {
    // Tile at (0,0) can move to (3,2) if empty
    auto ws = makeWorkspace();
    ws.tiles.push_back(makeTile(0, 0));  // index 0 — the tile we drag

    auto* handler = createTileInputHandler();
    handler->enterDrag(0, 0, 0);
    CHECK(handler->getEditMode() == EditMode::Drag);
    CHECK(handler->getDraggedTileIndex() == 0);
    CHECK(handler->getDragOriginX() == 0 && handler->getDragOriginY() == 0);

    handler->moveDragDest(3, 2, ws);     // move dest to (3,2)
    CHECK(handler->getDragDestX() == 3 && handler->getDragDestY() == 2);
    CHECK(handler->isDragDestValid());

    auto result = handler->commitDrop(ws);
    CHECK(result == DropResult::Committed);
    CHECK(ws.tiles[0].x == 3 && ws.tiles[0].y == 2);
    CHECK(handler->getEditMode() == EditMode::Browse);

    delete handler;
    std::printf("test_valid_move: OK\n");
}

void test_invalid_move_occupied() {
    // Tile at (0,0) cannot move to (1,0) because another tile is there
    auto ws = makeWorkspace();
    ws.tiles.push_back(makeTile(0, 0));  // index 0 — dragged
    ws.tiles.push_back(makeTile(1, 0));  // index 1 — blocker

    auto* handler = createTileInputHandler();
    handler->enterDrag(0, 0, 0);
    handler->moveDragDest(1, 0, ws);     // dest = (1,0) — occupied by tile 1
    CHECK(!handler->isDragDestValid());

    auto result = handler->commitDrop(ws);
    CHECK(result == DropResult::Invalid);
    // Tile must stay at origin
    CHECK(ws.tiles[0].x == 0 && ws.tiles[0].y == 0);
    CHECK(handler->getEditMode() == EditMode::Browse);

    delete handler;
    std::printf("test_invalid_move_occupied: OK\n");
}

void test_bounds_wide_tile() {
    // 3×1 tile at (4, 0) would overflow column 5 (needs cols 4,5,6)
    auto ws = makeWorkspace();
    model::Tile wide = makeTile(0, 0, 3, 1);  // 3×1 tile starting at (0,0)
    ws.tiles.push_back(wide);

    auto* handler = createTileInputHandler();
    handler->enterDrag(0, 0, 0);

    // Dest = (4,0): tile would occupy (4,0),(5,0),(6,0) → col 6 out of bounds
    handler->moveDragDest(4, 0, ws);
    CHECK(!handler->isDragDestValid());

    // Dest = (3,0): tile occupies (3,0),(4,0),(5,0) → valid
    // But we need moveDragDest to do absolute placement, not delta.
    // Reset and set to (3,0)
    handler->enterDrag(0, 0, 0);
    handler->moveDragDest(3, 0, ws);
    CHECK(handler->isDragDestValid());

    delete handler;
    std::printf("test_bounds_wide_tile: OK\n");
}

void test_cancel_keeps_origin() {
    auto ws = makeWorkspace();
    ws.tiles.push_back(makeTile(2, 1));

    auto* handler = createTileInputHandler();
    handler->enterDrag(0, 2, 1);
    handler->moveDragDest(5, 2, ws);    // move dest somewhere
    CHECK(handler->getDragDestX() == 5);

    auto result = handler->cancelDrop();
    CHECK(result == DropResult::Cancelled);
    CHECK(handler->getEditMode() == EditMode::Browse);
    // Tile must still be at original position
    CHECK(ws.tiles[0].x == 2 && ws.tiles[0].y == 1);

    delete handler;
    std::printf("test_cancel_keeps_origin: OK\n");
}

void test_a_hold_threshold() {
    auto* handler = createTileInputHandler();
    CHECK(handler->getEditMode() == EditMode::Browse);
    CHECK(handler->getAHoldFrames() == 0);

    // Simulate 14 frames of A held — should NOT trigger drag
    for (int i = 0; i < 14; ++i) {
        handler->setAHeld(true);
        CHECK(handler->getAHoldFrames() == i + 1);
    }
    CHECK(handler->getAHoldFrames() == 14);

    // Frame 15 → at threshold
    handler->setAHeld(true);
    CHECK(handler->getAHoldFrames() == 15);
    CHECK(handler->getAHoldFrames() >= TileInputHandler::kAHoldThreshold);

    // Release A — counter resets
    handler->setAHeld(false);
    CHECK(handler->getAHoldFrames() == 0);

    delete handler;
    std::printf("test_a_hold_threshold: OK\n");
}

void test_no_drag_without_tile() {
    // enterDrag on index -1 should still transition but be inert on commit
    auto ws = makeWorkspace(); // empty workspace
    auto* handler = createTileInputHandler();

    handler->enterDrag(-1, 0, 0);
    CHECK(handler->getEditMode() == EditMode::Drag);

    auto result = handler->commitDrop(ws);
    CHECK(result == DropResult::NothingDragged);

    delete handler;
    std::printf("test_no_drag_without_tile: OK\n");
}

void test_reset_clears_state() {
    auto ws = makeWorkspace();
    ws.tiles.push_back(makeTile(0, 0));

    auto* handler = createTileInputHandler();
    handler->enterDrag(0, 0, 0);
    CHECK(handler->getEditMode() == EditMode::Drag);

    handler->reset();
    CHECK(handler->getEditMode() == EditMode::Browse);
    CHECK(handler->getDraggedTileIndex() == -1);
    CHECK(handler->getAHoldFrames() == 0);

    delete handler;
    std::printf("test_reset_clears_state: OK\n");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== drag_drop_test ===\n");
    test_valid_move();
    test_invalid_move_occupied();
    test_bounds_wide_tile();
    test_cancel_keeps_origin();
    test_a_hold_threshold();
    test_no_drag_without_tile();
    test_reset_clears_state();
    std::printf("=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

{"content":"// Copyright contributors to the DeskNX project.\n// SPDX-License-Identifier: MIT\n//\n// Host-side drag/drop unit tests (no SDL, no libnx).\n// Compile: zig c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -Iprojects/DeskNXMenu/source\n//          tests/drag_drop_test.cpp projects/DeskNXMenu/source/TileInputHandler.cpp -o /tmp/dd-test\n// Run:     /tmp/dd-test\n\n#include <desknx/Model.hpp>\n#include <cassert>\n#include <cstdio>\n\n// Minimal stubs so TileInputHandler.cpp compiles without SDL/libnx\n#ifndef debugPrintf\n#  define debugPrintf(...) (void)0\n#endif\n\n#include \"TileInputHandler.hpp\"\n\nusing namespace desknx;\nusing namespace desknx::menu::input;\n\n// ---------------------------------------------------------------------------\n// Helpers\n// ---------------------------------------------------------------------------\n\nstatic model::Workspace makeWorkspace() {\n    model::Workspace ws;\n    ws.name = \"Test\";\n    return ws;\n}\n\nstatic model::Tile makeTile(int x, int y, int w = 1, int h = 1) {\n    model::Tile t;\n    t.x = x; t.y = y; t.w = w; t.h = h;\n    t.kind = model::Kind::Game;\n    t.title = \"Game\";\n    return t;\n}\n\n// ---------------------------------------------------------------------------\n// Tests\n// ---------------------------------------------------------------------------\n\nstatic int passed = 0;\nstatic int failed = 0;\n\n#define CHECK(cond) do { \\\n    if (cond) { ++passed; } \\\n    else { ++failed; std::printf(\"FAIL %s:%d: %s\\n\", __FILE__, __LINE__, #cond); } \\\n} while(0)\n\nvoid test_valid_move() {\n    // Tile at (0,0) can move to (3,2) if empty\n    auto ws = makeWorkspace();\n    ws.tiles.push_back(makeTile(0, 0));  // index 0 — the tile we drag\n\n    auto* handler = createTileInputHandler();\n    handler->enterDrag(0, 0, 0);\n    CHECK(handler->getEditMode() == EditMode::Drag);\n    CHECK(handler->getDraggedTileIndex() == 0);\n    CHECK(handler->getDragOriginX() == 0 && handler->getDragOriginY() == 0);\n\n    handler->moveDragDest(3, 2, ws);     // move dest to (3,2)\n    CHECK(handler->getDragDestX() == 3 && handler->getDragDestY() == 2);\n    CHECK(handler->isDragDestValid());\n\n    auto result = handler->commitDrop(ws);\n    CHECK(result == DropResult::Committed);\n    CHECK(ws.tiles[0].x == 3 && ws.tiles[0].y == 2);\n    CHECK(handler->getEditMode() == EditMode::Browse);\n\n    delete handler;\n    std::printf(\"test_valid_move: OK\\n\");\n}\n\nvoid test_invalid_move_occupied() {\n    // Tile at (0,0) cannot move to (1,0) because another tile is there\n    auto ws = makeWorkspace();\n    ws.tiles.push_back(makeTile(0, 0));  // index 0 — dragged\n    ws.tiles.push_back(makeTile(1, 0));  // index 1 — blocker\n\n    auto* handler = createTileInputHandler();\n    handler->enterDrag(0, 0, 0);\n    handler->moveDragDest(1, 0, ws);     // dest = (1,0) — occupied by tile 1\n    CHECK(!handler->isDragDestValid());\n\n    auto result = handler->commitDrop(ws);\n    CHECK(result == DropResult::Invalid);\n    // Tile must stay at origin\n    CHECK(ws.tiles[0].x == 0 && ws.tiles[0].y == 0);\n    CHECK(handler->getEditMode() == EditMode::Browse);\n\n    delete handler;\n    std::printf(\"test_invalid_move_occupied: OK\\n\");\n}\n\nvoid test_bounds_wide_tile() {\n    // 3×1 tile at (4, 0) would overflow column 5 (needs cols 4,5,6)\n    auto ws = makeWorkspace();\n    model::Tile wide = makeTile(0, 0, 3, 1);  // 3×1 tile starting at (0,0)\n    ws.tiles.push_back(wide);\n\n    auto* handler = createTileInputHandler();\n    handler->enterDrag(0, 0, 0);\n\n    // Dest = (4,0): tile would occupy (4,0),(5,0),(6,0) → col 6 out of bounds\n    handler->moveDragDest(4, 0, ws);\n    CHECK(!handler->isDragDestValid());\n\n    // Dest = (3,0): tile occupies (3,0),(4,0),(5,0) → valid\n    // But we need moveDragDest to do absolute placement, not delta.\n    // Reset and set to (3,0)\n    handler->enterDrag(0, 0, 0);\n    handler->moveDragDest(3, 0, ws);\n    CHECK(handler->isDragDestValid());\n\n    delete handler;\n    std::printf(\"test_bounds_wide_tile: OK\\n\");\n}\n\nvoid test_cancel_keeps_origin() {\n    auto ws = makeWorkspace();\n    ws.tiles.push_back(makeTile(2, 1));\n\n    auto* handler = createTileInputHandler();\n    handler->enterDrag(0, 2, 1);\n    handler->moveDragDest(5, 2, ws);    // move dest somewhere\n    CHECK(handler->getDragDestX() == 5);\n\n    auto result = handler->cancelDrop();\n    CHECK(result == DropResult::Cancelled);\n    CHECK(handler->getEditMode() == EditMode::Browse);\n    // Tile must still be at original position\n    CHECK(ws.tiles[0].x == 2 && ws.tiles[0].y == 1);\n\n    delete handler;\n    std::printf(\"test_cancel_keeps_origin: OK\\n\");\n}\n\nvoid test_a_hold_threshold() {\n    auto* handler = createTileInputHandler();\n    CHECK(handler->getEditMode() == EditMode::Browse);\n    CHECK(handler->getAHoldFrames() == 0);\n\n    // Simulate 14 frames of A held — should NOT trigger drag\n    for (int i = 0; i < 14; ++i) {\n        handler->setAHeld(true);\n        CHECK(handler->getAHoldFrames() == i + 1);\n    }\n    CHECK(handler->getAHoldFrames() == 14);\n\n    // Frame 15 → at threshold\n    handler->setAHeld(true);\n    CHECK(handler->getAHoldFrames() == 15);\n    CHECK(handler->getAHoldFrames() >= TileInputHandler::kAHoldThreshold);\n\n    // Release A — counter resets\n    handler->setAHeld(false);\n    CHECK(handler->getAHoldFrames() == 0);\n\n    delete handler;\n    std::printf(\"test_a_hold_threshold: OK\\n\");\n}\n\nvoid test_no_drag_without_tile() {\n    // enterDrag on index -1 should still transition but be inert on commit\n    auto ws = makeWorkspace(); // empty workspace\n    auto* handler = createTileInputHandler();\n\n    handler->enterDrag(-1, 0, 0);\n    CHECK(handler->getEditMode() == EditMode::Drag);\n\n    auto result = handler->commitDrop(ws);\n    CHECK(result == DropResult::NothingDragged);\n\n    delete handler;\n    std::printf(\"test_no_drag_without_tile: OK\\n\");\n}\n\nvoid test_reset_clears_state() {\n    auto ws = makeWorkspace();\n    ws.tiles.push_back(makeTile(0, 0));\n\n    auto* handler = createTileInputHandler();\n    handler->enterDrag(0, 0, 0);\n    CHECK(handler->getEditMode() == EditMode::Drag);\n\n    handler->reset();\n    CHECK(handler->getEditMode() == EditMode::Browse);\n    CHECK(handler->getDraggedTileIndex() == -1);\n    CHECK(handler->getAHoldFrames() == 0);\n\n    delete handler;\n    std::printf(\"test_reset_clears_state: OK\\n\");\n}\n\n// ---------------------------------------------------------------------------\n// Main\n// ---------------------------------------------------------------------------\n\nint main() {\n    std::printf(\"=== drag_drop_test ===\\n\");\n    test_valid_move();\n    test_invalid_move_occupied();\n    test_bounds_wide_tile();\n    test_cancel_keeps_origin();\n    test_a_hold_threshold();\n    test_no_drag_without_tile();\n    test_reset_clears_state();\n    std::printf(\"=== %d passed, %d failed ===\\n\", passed, failed);\n    return failed == 0 ? 0 : 1;\n}\n"}