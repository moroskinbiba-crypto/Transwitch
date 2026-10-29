#include <desknx/Model.hpp>

#include <cassert>
#include <string>

using namespace desknx::model;

int main() {
    Workspace desk{"test", {}};
    Tile wide{Kind::Game, "wide", "0100", 0, 0, 2, 1, 0};
    Tile small{Kind::Nro, "small", "sdmc:/switch/demo.nro", 2, 0, 1, 1, 1};
    assert(placeable(desk, wide, 0, 0));
    desk.tiles.push_back(wide);
    assert(!placeable(desk, small, 1, 0));
    assert(placeable(desk, small, 2, 0));
    assert(itemAt(desk, 1, 0) == 0);
    assert(itemAt(desk, 5, 2) == -1);
    assert(placeable(desk, wide, 1, 1));
    assert(!placeable(desk, Tile{Kind::Game, "bad", "", 0, 0, 4, 1, 0}, 0, 0));
    int x = -1, y = -1;
    assert(firstFree(desk, small, x, y));
    assert(x >= 0 && y >= 0 && placeable(desk, small, x, y));
    assert(typeCode(parseKind("N")) == "N");
    assert(typeCode(parseKind("C")) == "C");
    assert(parseKind("unknown") == Kind::Game);
    const auto fields = splitTabs("A\tb\tc");
    assert(fields.size() == 3 && fields[1] == "b");
    assert(safeField("a\tb\nc") == "a b c");
    assert(iconCachePath(Kind::Game, "0100") == "sdmc:/switch/DeskNX/cache/icons/game-0100.jpg");
    assert(iconCachePath(Kind::Nro, "sdmc:/switch/demo.nro") !=
           iconCachePath(Kind::Nro, "sdmc:/switch/other.nro"));
    return 0;
}
