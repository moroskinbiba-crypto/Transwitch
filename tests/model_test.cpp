#include <desknx/Model.hpp>
#include <desknx/Layout.hpp>

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

    const std::string v1 =
        "DESKNX1\nD\tLegacy\nI\t0\tG\tExample\t0100\t0\t0\t1\t1\t0\n";
    desknx::layout::LoadResult parsedV1{};
    assert(desknx::layout::parse(v1, parsedV1));
    assert(parsedV1.needsMigration && parsedV1.activeWorkspace == 0);
    assert(parsedV1.workspaces.size() == 1);
    assert(parsedV1.workspaces[0].tiles.size() == 1);

    Workspace second{"Homebrew", {}};
    second.tiles.push_back(Tile{Kind::Nro,"Demo","sdmc:/switch/demo.nro",1,1,2,1,2});
    std::vector<Workspace> source{{"Games",{wide}},second};
    const std::string v3 = desknx::layout::serialize(source,1);
    assert(v3.rfind("DESKNX3\\n",0) == 0);
    desknx::layout::LoadResult roundTrip{};
    assert(desknx::layout::parse(v3, roundTrip));
    assert(!roundTrip.needsMigration && roundTrip.activeWorkspace == 1);
    assert(roundTrip.workspaces.size() == 2);
    assert(roundTrip.workspaces[1].tiles[0].data == "sdmc:/switch/demo.nro");

    const std::string overlap =
        "DESKNX3\nA\t0\nD\tBroken\n"
        "I\t0\tG\tOne\t0100\t0\t0\t2\t1\t0\n"
        "I\t0\tN\tTwo\tsdmc:/switch/two.nro\t1\t0\t1\t1\t1\n";
    desknx::layout::LoadResult invalid{};
    assert(!desknx::layout::parse(overlap, invalid));
    assert(!desknx::layout::parse("DESKNX999\\nD\\tbad\\n", invalid));
    return 0;
}
