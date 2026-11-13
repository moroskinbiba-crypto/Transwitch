// tests/focus_navigation_test.cpp
// Host-only unit tests for D-pad focus navigation logic.
// Build: zig c++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/focus_navigation_test.cpp -o /tmp/desknx-focus-test
// Run:   /tmp/desknx-focus-test

#include <desknx/Model.hpp>
#include <desknx/TouchGesture.hpp>
#include <cassert>
#include <cstdio>
#include <utility>

// ── Replicate focus helpers (same code as GridRenderer.cpp) ────────────────

static int tileAt(const desknx::model::Workspace& ws, int cx, int cy) {
    for (int i = 0; i < static_cast<int>(ws.tiles.size()); ++i) {
        const auto& t = ws.tiles[i];
        if (cx >= t.x && cx < t.x + t.w && cy >= t.y && cy < t.y + t.h)
            return i;
    }
    return -1;
}

static bool cellOccupied(const desknx::model::Workspace& ws, int cx, int cy) {
    return tileAt(ws, cx, cy) >= 0;
}

static std::pair<int,int> firstOccupied(const desknx::model::Workspace& ws) {
    for (int row = 0; row < desknx::model::Rows; ++row)
        for (int col = 0; col < desknx::model::Columns; ++col)
            if (cellOccupied(ws, col, row)) return {col, row};
    return {-1, -1};
}

static std::pair<int,int> nextOccupied(const desknx::model::Workspace& ws,
                                        int cx, int cy,
                                        desknx::touch::Direction dir) {
    if (ws.tiles.empty()) return {cx, cy};

    const int total = desknx::model::Columns * desknx::model::Rows;
    auto rowMajor = [](int c, int r){ return r * desknx::model::Columns + c; };
    auto colMajor = [](int c, int r){ return c * desknx::model::Rows + r; };
    auto fromRM   = [](int i) -> std::pair<int,int> {
        return {i % desknx::model::Columns, i / desknx::model::Columns}; };
    auto fromCM   = [](int i) -> std::pair<int,int> {
        return {i / desknx::model::Rows, i % desknx::model::Rows}; };

    const int curRM = rowMajor(cx, cy);
    const int curCM = colMajor(cx, cy);

    for (int step = 1; step <= total; ++step) {
        std::pair<int,int> cell;
        switch (dir) {
            case desknx::touch::Direction::Right:
                cell = fromRM((curRM + step) % total); break;
            case desknx::touch::Direction::Left:
                cell = fromRM((curRM - step + total) % total); break;
            case desknx::touch::Direction::Down:
                cell = fromCM((curCM + step) % total); break;
            case desknx::touch::Direction::Up:
                cell = fromCM((curCM - step + total) % total); break;
        }
        if (cellOccupied(ws, cell.first, cell.second)) return cell;
    }
    return {cx, cy};
}

// ── Test helpers ──────────────────────────────────────────────────────────

static desknx::model::Tile makeTile(int x, int y, int w = 1, int h = 1) {
    desknx::model::Tile t;
    t.x = x; t.y = y; t.w = w; t.h = h;
    return t;
}

static int passed = 0;
static int failed = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failed; \
    } else { ++passed; } \
} while (0)

// ── Tests ─────────────────────────────────────────────────────────────────

void test_firstOccupied_empty() {
    desknx::model::Workspace ws;
    auto [x, y] = firstOccupied(ws);
    CHECK(x == -1 && y == -1);
}

void test_firstOccupied_one_tile() {
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(3, 2));
    auto [x, y] = firstOccupied(ws);
    // Scan is row-major top-left so col 3 row 2 is the only tile
    CHECK(x == 3 && y == 2);
}

void test_firstOccupied_prefers_topleft() {
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(2, 1));
    ws.tiles.push_back(makeTile(0, 0));
    auto [x, y] = firstOccupied(ws);
    CHECK(x == 0 && y == 0);
}

void test_navigate_right_basic() {
    // Two tiles: (0,0) and (3,0)
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0));
    ws.tiles.push_back(makeTile(3, 0));
    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);
    CHECK(nx == 3 && ny == 0);
}

void test_navigate_right_wraps() {
    // Tile at (0,0) only; moving right wraps all the way around
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0));
    // Should return same cell (the only occupied one)
    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);
    CHECK(nx == 0 && ny == 0);
}

void test_navigate_left_wraps_to_last_column() {
    // Tiles at (0,0) and (5,2)
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0));
    ws.tiles.push_back(makeTile(5, 2));
    // From (0,0) moving left wraps → (5,2)
    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Left);
    CHECK(nx == 5 && ny == 2);
}

void test_navigate_down_basic() {
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(2, 0));
    ws.tiles.push_back(makeTile(2, 2));
    auto [nx, ny] = nextOccupied(ws, 2, 0, desknx::touch::Direction::Down);
    CHECK(nx == 2 && ny == 2);
}

void test_navigate_up_wraps() {
    // Only tile at (1,2); moving up from row 0 wraps to row 2
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(1, 2));
    auto [nx, ny] = nextOccupied(ws, 1, 0, desknx::touch::Direction::Up);
    CHECK(nx == 1 && ny == 2);
}

void test_navigate_skips_empty_rows() {
    // Tiles at (0,0) and (0,2) — row 1 column 0 is empty
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0));
    ws.tiles.push_back(makeTile(0, 2));
    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Down);
    CHECK(nx == 0 && ny == 2);
}

void test_wide_tile_occupies_multiple_cells() {
    // 2×1 tile at (0,0) — cells (0,0) and (1,0) both occupied
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0, /*w=*/2, /*h=*/1));
    CHECK(cellOccupied(ws, 0, 0));
    CHECK(cellOccupied(ws, 1, 0));
    CHECK(!cellOccupied(ws, 2, 0));
    CHECK(!cellOccupied(ws, 0, 1));
}

void test_navigate_lands_on_wide_tile() {
    // 2×1 tile at (2,1); single tile at (0,0)
    desknx::model::Workspace ws;
    ws.tiles.push_back(makeTile(0, 0));
    ws.tiles.push_back(makeTile(2, 1, 2, 1)); // occupies (2,1) and (3,1)
    // Moving right from (0,0): scan right→wraps through row 0 (empty cols 1-5),
    // then row 1 col 0 (empty), col 1 (empty), col 2 → occupied
    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);
    CHECK(nx == 2 && ny == 1);
}

void test_empty_workspace_no_panic() {
    desknx::model::Workspace ws;
    // All directions must return the starting position, not crash
    for (auto dir : {desknx::touch::Direction::Left,
                     desknx::touch::Direction::Right,
                     desknx::touch::Direction::Up,
                     desknx::touch::Direction::Down}) {
        auto [nx, ny] = nextOccupied(ws, 0, 0, dir);
        CHECK(nx == 0 && ny == 0);
    }
}

// ── Entry point ───────────────────────────────────────────────────────────

int main() {
    test_firstOccupied_empty();
    test_firstOccupied_one_tile();
    test_firstOccupied_prefers_topleft();
    test_navigate_right_basic();
    test_navigate_right_wraps();
    test_navigate_left_wraps_to_last_column();
    test_navigate_down_basic();
    test_navigate_up_wraps();
    test_navigate_skips_empty_rows();
    test_wide_tile_occupies_multiple_cells();
    test_navigate_lands_on_wide_tile();
    test_empty_workspace_no_panic();

    std::printf("\nFocus navigation tests: %d passed, %d failed\n", passed, failed);
    return (failed > 0) ? 1 : 0;
}

{"content":"// tests/focus_navigation_test.cpp\n// Host-only unit tests for D-pad focus navigation logic.\n// Build: zig c++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/focus_navigation_test.cpp -o /tmp/desknx-focus-test\n// Run:   /tmp/desknx-focus-test\n\n#include <desknx/Model.hpp>\n#include <desknx/TouchGesture.hpp>\n#include <cassert>\n#include <cstdio>\n#include <utility>\n\n// ── Replicate focus helpers (same code as GridRenderer.cpp) ────────────────\n\nstatic int tileAt(const desknx::model::Workspace& ws, int cx, int cy) {\n    for (int i = 0; i < static_cast<int>(ws.tiles.size()); ++i) {\n        const auto& t = ws.tiles[i];\n        if (cx >= t.x && cx < t.x + t.w && cy >= t.y && cy < t.y + t.h)\n            return i;\n    }\n    return -1;\n}\n\nstatic bool cellOccupied(const desknx::model::Workspace& ws, int cx, int cy) {\n    return tileAt(ws, cx, cy) >= 0;\n}\n\nstatic std::pair<int,int> firstOccupied(const desknx::model::Workspace& ws) {\n    for (int row = 0; row < desknx::model::Rows; ++row)\n        for (int col = 0; col < desknx::model::Columns; ++col)\n            if (cellOccupied(ws, col, row)) return {col, row};\n    return {-1, -1};\n}\n\nstatic std::pair<int,int> nextOccupied(const desknx::model::Workspace& ws,\n                                        int cx, int cy,\n                                        desknx::touch::Direction dir) {\n    if (ws.tiles.empty()) return {cx, cy};\n\n    const int total = desknx::model::Columns * desknx::model::Rows;\n    auto rowMajor = [](int c, int r){ return r * desknx::model::Columns + c; };\n    auto colMajor = [](int c, int r){ return c * desknx::model::Rows + r; };\n    auto fromRM   = [](int i) -> std::pair<int,int> {\n        return {i % desknx::model::Columns, i / desknx::model::Columns}; };\n    auto fromCM   = [](int i) -> std::pair<int,int> {\n        return {i / desknx::model::Rows, i % desknx::model::Rows}; };\n\n    const int curRM = rowMajor(cx, cy);\n    const int curCM = colMajor(cx, cy);\n\n    for (int step = 1; step <= total; ++step) {\n        std::pair<int,int> cell;\n        switch (dir) {\n            case desknx::touch::Direction::Right:\n                cell = fromRM((curRM + step) % total); break;\n            case desknx::touch::Direction::Left:\n                cell = fromRM((curRM - step + total) % total); break;\n            case desknx::touch::Direction::Down:\n                cell = fromCM((curCM + step) % total); break;\n            case desknx::touch::Direction::Up:\n                cell = fromCM((curCM - step + total) % total); break;\n        }\n        if (cellOccupied(ws, cell.first, cell.second)) return cell;\n    }\n    return {cx, cy};\n}\n\n// ── Test helpers ──────────────────────────────────────────────────────────\n\nstatic desknx::model::Tile makeTile(int x, int y, int w = 1, int h = 1) {\n    desknx::model::Tile t;\n    t.x = x; t.y = y; t.w = w; t.h = h;\n    return t;\n}\n\nstatic int passed = 0;\nstatic int failed = 0;\n\n#define CHECK(expr) do { \\\n    if (!(expr)) { \\\n        std::printf(\"FAIL %s:%d: %s\\n\", __FILE__, __LINE__, #expr); \\\n        ++failed; \\\n    } else { ++passed; } \\\n} while (0)\n\n// ── Tests ─────────────────────────────────────────────────────────────────\n\nvoid test_firstOccupied_empty() {\n    desknx::model::Workspace ws;\n    auto [x, y] = firstOccupied(ws);\n    CHECK(x == -1 && y == -1);\n}\n\nvoid test_firstOccupied_one_tile() {\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(3, 2));\n    auto [x, y] = firstOccupied(ws);\n    // Scan is row-major top-left so col 3 row 2 is the only tile\n    CHECK(x == 3 && y == 2);\n}\n\nvoid test_firstOccupied_prefers_topleft() {\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(2, 1));\n    ws.tiles.push_back(makeTile(0, 0));\n    auto [x, y] = firstOccupied(ws);\n    CHECK(x == 0 && y == 0);\n}\n\nvoid test_navigate_right_basic() {\n    // Two tiles: (0,0) and (3,0)\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0));\n    ws.tiles.push_back(makeTile(3, 0));\n    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);\n    CHECK(nx == 3 && ny == 0);\n}\n\nvoid test_navigate_right_wraps() {\n    // Tile at (0,0) only; moving right wraps all the way around\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0));\n    // Should return same cell (the only occupied one)\n    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);\n    CHECK(nx == 0 && ny == 0);\n}\n\nvoid test_navigate_left_wraps_to_last_column() {\n    // Tiles at (0,0) and (5,2)\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0));\n    ws.tiles.push_back(makeTile(5, 2));\n    // From (0,0) moving left wraps → (5,2)\n    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Left);\n    CHECK(nx == 5 && ny == 2);\n}\n\nvoid test_navigate_down_basic() {\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(2, 0));\n    ws.tiles.push_back(makeTile(2, 2));\n    auto [nx, ny] = nextOccupied(ws, 2, 0, desknx::touch::Direction::Down);\n    CHECK(nx == 2 && ny == 2);\n}\n\nvoid test_navigate_up_wraps() {\n    // Only tile at (1,2); moving up from row 0 wraps to row 2\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(1, 2));\n    auto [nx, ny] = nextOccupied(ws, 1, 0, desknx::touch::Direction::Up);\n    CHECK(nx == 1 && ny == 2);\n}\n\nvoid test_navigate_skips_empty_rows() {\n    // Tiles at (0,0) and (0,2) — row 1 column 0 is empty\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0));\n    ws.tiles.push_back(makeTile(0, 2));\n    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Down);\n    CHECK(nx == 0 && ny == 2);\n}\n\nvoid test_wide_tile_occupies_multiple_cells() {\n    // 2×1 tile at (0,0) — cells (0,0) and (1,0) both occupied\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0, /*w=*/2, /*h=*/1));\n    CHECK(cellOccupied(ws, 0, 0));\n    CHECK(cellOccupied(ws, 1, 0));\n    CHECK(!cellOccupied(ws, 2, 0));\n    CHECK(!cellOccupied(ws, 0, 1));\n}\n\nvoid test_navigate_lands_on_wide_tile() {\n    // 2×1 tile at (2,1); single tile at (0,0)\n    desknx::model::Workspace ws;\n    ws.tiles.push_back(makeTile(0, 0));\n    ws.tiles.push_back(makeTile(2, 1, 2, 1)); // occupies (2,1) and (3,1)\n    // Moving right from (0,0): scan right→wraps through row 0 (empty cols 1-5),\n    // then row 1 col 0 (empty), col 1 (empty), col 2 → occupied\n    auto [nx, ny] = nextOccupied(ws, 0, 0, desknx::touch::Direction::Right);\n    CHECK(nx == 2 && ny == 1);\n}\n\nvoid test_empty_workspace_no_panic() {\n    desknx::model::Workspace ws;\n    // All directions must return the starting position, not crash\n    for (auto dir : {desknx::touch::Direction::Left,\n                     desknx::touch::Direction::Right,\n                     desknx::touch::Direction::Up,\n                     desknx::touch::Direction::Down}) {\n        auto [nx, ny] = nextOccupied(ws, 0, 0, dir);\n        CHECK(nx == 0 && ny == 0);\n    }\n}\n\n// ── Entry point ───────────────────────────────────────────────────────────\n\nint main() {\n    test_firstOccupied_empty();\n    test_firstOccupied_one_tile();\n    test_firstOccupied_prefers_topleft();\n    test_navigate_right_basic();\n    test_navigate_right_wraps();\n    test_navigate_left_wraps_to_last_column();\n    test_navigate_down_basic();\n    test_navigate_up_wraps();\n    test_navigate_skips_empty_rows();\n    test_wide_tile_occupies_multiple_cells();\n    test_navigate_lands_on_wide_tile();\n    test_empty_workspace_no_panic();\n\n    std::printf(\"\\nFocus navigation tests: %d passed, %d failed\\n\", passed, failed);\n    return (failed > 0) ? 1 : 0;\n}\n"}