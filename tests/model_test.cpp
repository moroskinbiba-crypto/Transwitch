#include <desknx/Model.hpp>
#include <desknx/StrictParse.hpp>
#include <desknx/Layout.hpp>
#include "../source/catalog_view.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>

using namespace desknx::model;

int main() {
    int parsedInt = 0;
    assert(desknx::strict::parseInt32("0", parsedInt) && parsedInt == 0);
    assert(desknx::strict::parseInt32("-2147483648", parsedInt) && parsedInt == (-2147483647 - 1));
    assert(desknx::strict::parseInt32("2147483647", parsedInt) && parsedInt == 2147483647);
    assert(!desknx::strict::parseInt32("", parsedInt));
    assert(!desknx::strict::parseInt32("+", parsedInt));
    assert(!desknx::strict::parseInt32("123tail", parsedInt));
    assert(!desknx::strict::parseInt32(" 123", parsedInt));
    assert(!desknx::strict::parseInt32("123 ", parsedInt));
    assert(!desknx::strict::parseInt32("2147483648", parsedInt));
    assert(!desknx::strict::parseInt32("-2147483649", parsedInt));

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
    assert(!placeable(desk, small, std::numeric_limits<int>::max(), 0));
    assert(!placeable(desk, small, 0, std::numeric_limits<int>::max()));
    int x = -1, y = -1;
    assert(firstFree(desk, small, x, y));
    assert(x >= 0 && y >= 0 && placeable(desk, small, x, y));
    assert(typeCode(parseKind("N")) == "N");
    assert(typeCode(parseKind("C")) == "C");
    assert(parseKind("unknown") == Kind::Unsupported);
    const auto fields = splitTabs("A\tb\tc");
    assert(fields.size() == 3 && fields[1] == "b");
    assert(safeField("a\tb\nc") == "a b c");
    assert(isTitleId("0100000000001000"));
    assert(!isTitleId("../../escape"));
    assert(iconCachePath(Kind::Game, "0100000000001000") ==
           "sdmc:/switch/DeskNX/cache/icons/game-0100000000001000.jpg");
    assert(iconCachePath(Kind::Game, "010000000000100a") ==
           "sdmc:/switch/DeskNX/cache/icons/game-010000000000100A.jpg");
    assert(iconCachePath(Kind::Game, "0100").empty());
    assert(iconCachePath(Kind::Game, "../../escape").empty());
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
    assert(v3.rfind("DESKNX3\n",0) == 0);
    desknx::layout::LoadResult roundTrip{};
    assert(desknx::layout::parse(v3, roundTrip));
    assert(!roundTrip.needsMigration && roundTrip.activeWorkspace == 1);
    assert(roundTrip.workspaces.size() == 2);
    assert(roundTrip.workspaces[1].tiles[0].data == "sdmc:/switch/demo.nro");

    const std::string overlap =
        "DESKNX3\nA\t0\nD\tBroken\n"
        "I\t0\tG\tOne\t0100\t0\t0\t2\t1\t0\n"
        "I\t0\tN\tTwo\tsdmc:/switch/two.nro\t1\t0\t1\t1\t1\n";
    desknx::layout::LoadResult recovered{};
    assert(desknx::layout::parse(overlap, recovered));
    assert(recovered.recoveredTiles == 1);
    assert(recovered.workspaces[0].tiles.size() == 2);
    assert(recovered.workspaces[0].tiles[1].x != 1);

    const std::string unknown =
        "DESKNX3\nA\t0\nD\tLegacy\n"
        "I\t0\tQX\tOlder plug-in\topaque-data\t0\t0\t1\t1\t2\n";
    desknx::layout::LoadResult parsedUnknown{};
    assert(desknx::layout::parse(unknown, parsedUnknown));
    assert(parsedUnknown.workspaces[0].tiles[0].kind == Kind::Unsupported);
    assert(parsedUnknown.workspaces[0].tiles[0].unknownTypeCode == "QX");
    const std::string unknownRoundTrip = desknx::layout::serialize(
        parsedUnknown.workspaces, parsedUnknown.activeWorkspace);
    assert(unknownRoundTrip.find("\tQX\t") != std::string::npos);

    assert(!desknx::layout::parse("DESKNX999\nD\tbad\n", recovered));
    assert(!desknx::layout::parse(std::string(1024 * 1024 + 1, 'x'), recovered));

    using namespace desknx::catalog_view;
    std::vector<Candidate> catalog{
        {Kind::Game,"Zelda Adventure","0100",1,1,0},
        {Kind::Nro,"Retro emulator","/tmp/desknx-small.nro",1,1,1},
        {Kind::Nro,"Русский перевод","/tmp/desknx-large.nro",1,1,2},
        {Kind::Clock,"Clock widget","",2,1,1}
    };
    { std::ofstream smallFile("/tmp/desknx-small.nro",std::ios::binary); smallFile<<"1234"; }
    { std::ofstream largeFile("/tmp/desknx-large.nro",std::ios::binary); largeFile<<"1234567890"; }
    auto filtered=FilterAndSort(catalog,"zelda",FilterMode::Games,SortMode::Name,{});
    assert(filtered.size()==1 && filtered[0].title=="Zelda Adventure");
    filtered=FilterAndSort(catalog,"Perevod",FilterMode::All,SortMode::Name,{});
    assert(filtered.size()==1 && filtered[0].title=="Русский перевод");
    filtered=FilterAndSort(catalog,"",FilterMode::Homebrew,SortMode::Size,{});
    assert(filtered.size()==2 && filtered[0].title=="Русский перевод");
    std::vector<RecentKey> recent{{Kind::Game,"0100"}};
    filtered=FilterAndSort(catalog,"",FilterMode::All,SortMode::Recent,recent);
    assert(!filtered.empty() && filtered[0].kind==Kind::Game);
    std::remove("/tmp/desknx-small.nro");std::remove("/tmp/desknx-large.nro");
    assert(std::string(FilterName(FilterMode::Homebrew))=="HOMEBREW");
    assert(std::string(SortName(SortMode::Recent))=="RECENT");
    return 0;
}
