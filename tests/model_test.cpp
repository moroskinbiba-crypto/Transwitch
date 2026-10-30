#include <desknx/Model.hpp>
#include <desknx/WorkspaceOps.hpp>
#include <desknx/CatalogPagination.hpp>
#include <desknx/StrictParse.hpp>
#include <desknx/SafeReplace.hpp>
#include <desknx/Layout.hpp>
#include "../source/catalog_view.hpp"

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <limits>
#include <iterator>
#include <unistd.h>
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

    // Touch hit-testing must reject empty gutters, preserve spanning tile
    // bounds, and use the same 6x3 grid geometry as the renderer.
    {
        using namespace desknx::workspace_ops;
        CellPoint cell{};
        assert(cellFromPoint(100,200,100,200,20,15,5,5,false,cell));
        assert(cell.x==0 && cell.y==0);
        assert(!cellFromPoint(122,200,100,200,20,15,5,5,false,cell));
        assert(cellFromPoint(122,217,100,200,20,15,5,5,true,cell));
        assert(cell.x==0 && cell.y==0);
        assert(cellFromPoint(150,200,100,200,20,15,5,5,false,cell));
        assert(cell.x==2 && cell.y==0);
        assert(!cellFromPoint(245,200,100,200,20,15,5,5,true,cell));

        Workspace touchDesk{"touch",{}};
        touchDesk.tiles.push_back(Tile{Kind::Game,"Wide","0100000000000001",0,0,2,2,0});
        assert(tileAtPoint(touchDesk,122,217,100,200,20,15,5,5)==0);
        assert(tileAtPoint(touchDesk,146,234,100,200,20,15,5,5)==0);
        assert(tileAtPoint(touchDesk,150,200,100,200,20,15,5,5)==-1);
    }

    // Moving a tile from a selected interior cell always keeps selection
    // anchored to the tile's new top-left coordinate.
    {
        using namespace desknx::workspace_ops;
        Workspace moveDesk{"move",{}};
        moveDesk.tiles.push_back(Tile{Kind::Game,"Wide","0100000000000002",0,0,2,2,0});
        int sx=1,sy=1;
        assert(moveSelectedTile(moveDesk,sx,sy,1,0));
        assert(moveDesk.tiles[0].x==1 && moveDesk.tiles[0].y==0);
        assert(sx==1 && sy==0);
        assert(!moveSelectedTile(moveDesk,sx,sy,0,0));
        assert(!moveSelectedTile(moveDesk,sx,sy,10,0));
    }

    // Shrinking a large tile must move keyboard focus back to its top-left.
    // Otherwise the next resize/remove command would target an empty cell.
    {
        using namespace desknx::workspace_ops;
        Workspace resizeDesk{"resize",{}};
        resizeDesk.tiles.push_back(Tile{Kind::Game,"Large","0100000000000003",0,0,3,2,0});
        int sx=2,sy=1;
        assert(resizeSelectedTile(resizeDesk,sx,sy,false));
        assert(resizeDesk.tiles[0].w==2 && resizeDesk.tiles[0].h==2);
        assert(sx==0 && sy==0 && itemAt(resizeDesk,sx,sy)==0);
        assert(resizeSelectedTile(resizeDesk,sx,sy,false));
        assert(resizeDesk.tiles[0].w==2 && resizeDesk.tiles[0].h==1);
        assert(sx==0 && sy==0 && itemAt(resizeDesk,sx,sy)==0);
    }

    // Cross-workspace moves preserve the tile and choose the first free
    // placement when the original position is occupied.
    {
        using namespace desknx::workspace_ops;
        std::vector<Workspace> spaces{{"Games",{}},{"Homebrew",{}}};
        spaces[0].tiles.push_back(Tile{Kind::Game,"Game","0100000000000004",0,0,2,1,0});
        spaces[1].tiles.push_back(Tile{Kind::Nro,"Existing","sdmc:/switch/existing.nro",0,0,1,1,1});
        int active=0,sx=0,sy=0;
        assert(moveSelectedTileToWorkspace(spaces,active,sx,sy,1));
        assert(active==1 && sx==1 && sy==0);
        assert(spaces[0].tiles.empty());
        assert(spaces[1].tiles.size()==2);
        assert(spaces[1].tiles[1].data=="0100000000000004");
    }

    // A full destination cannot cause a source tile to disappear.
    {
        using namespace desknx::workspace_ops;
        std::vector<Workspace> spaces{{"Source",{}},{"Full",{}}};
        spaces[0].tiles.push_back(Tile{Kind::Game,"Keep","0100000000000005",0,0,1,1,0});
        for(int yy=0;yy<Rows;++yy)
            for(int xx=0;xx<Columns;++xx)
                spaces[1].tiles.push_back(Tile{Kind::Nro,"Filler",
                    "sdmc:/switch/filler.nro",xx,yy,1,1,0});
        int active=0,sx=0,sy=0;
        assert(!moveSelectedTileToWorkspace(spaces,active,sx,sy,1));
        assert(active==0 && spaces[0].tiles.size()==1);
        assert(spaces[0].tiles[0].title=="Keep");
    }
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

    // Safe file replacement preserves the last good destination if publishing
    // a fully-written temp file fails.
    {
        char directoryTemplate[] = "/tmp/desknx-safe-replace-XXXXXX";
        char* directory = mkdtemp(directoryTemplate);
        assert(directory != nullptr);
        const std::string root(directory);
        const std::string destination = root + "/state.dat";
        const std::string temporary = root + "/state.tmp";
        const std::string backup = root + "/state.bak";
        auto writeText = [](const std::string& path, const std::string& value) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << value;
            out.close();
            assert(out.good());
        };
        auto readText = [](const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>());
        };

        writeText(destination, "old-state");
        writeText(temporary, "new-state");
        assert(desknx::safeio::replaceFilePreservingOnFailure(
                   temporary.c_str(), destination.c_str(), backup.c_str()) ==
               desknx::safeio::ReplaceStatus::Success);
        assert(readText(destination) == "new-state");
        assert(readText(backup).empty());
        assert(readText(temporary).empty());

        // Layout saves intentionally keep the old valid version as a backup.
        writeText(temporary, "next-state");
        assert(desknx::safeio::replaceFilePreservingOnFailure(
                   temporary.c_str(), destination.c_str(), backup.c_str(), true) ==
               desknx::safeio::ReplaceStatus::Success);
        assert(readText(destination) == "next-state");
        assert(readText(backup) == "new-state");

        // A missing temp simulates publication failure after the old file has
        // been moved aside; the helper must roll the old file back.
        assert(desknx::safeio::replaceFilePreservingOnFailure(
                   (root + "/missing.tmp").c_str(),
                   destination.c_str(), backup.c_str()) ==
               desknx::safeio::ReplaceStatus::Failed);
        assert(readText(destination) == "next-state");
        assert(readText(backup).empty());

        // If a prior failed rollback left only the backup, a later failed
        // publish must first restore and then preserve that recovery copy.
        std::remove(destination.c_str());
        writeText(backup, "recovery-state");
        assert(desknx::safeio::replaceFilePreservingOnFailure(
                   (root + "/still-missing.tmp").c_str(),
                   destination.c_str(), backup.c_str()) ==
               desknx::safeio::ReplaceStatus::Failed);
        assert(readText(destination) == "recovery-state");
        assert(readText(backup).empty());

        const std::string first = root + "/first.dat";
        const std::string firstTemp = root + "/first.tmp";
        const std::string firstBackup = root + "/first.bak";
        writeText(firstTemp, "first-state");
        assert(desknx::safeio::replaceFilePreservingOnFailure(
                   firstTemp.c_str(), first.c_str(), firstBackup.c_str()) ==
               desknx::safeio::ReplaceStatus::Success);
        assert(readText(first) == "first-state");

        std::remove(destination.c_str());
        std::remove(temporary.c_str());
        std::remove(backup.c_str());
        std::remove(first.c_str());
        std::remove(firstTemp.c_str());
        std::remove(firstBackup.c_str());
        rmdir(root.c_str());
    }

    // NS application-record pagination: count is an in/out parameter and
    // must start at the output buffer capacity, not zero.
    {
        using namespace desknx::catalog;
        const auto firstPage = MakeApplicationPageRequest(0);
        assert(firstPage.offset == 0);
        assert(firstPage.capacity == 128);
        assert(IsValidApplicationPageCount(firstPage.capacity, 128));
        assert(NextApplicationPageOffset(firstPage.offset, 128) == 128);
        const auto secondPage = MakeApplicationPageRequest(128);
        assert(secondPage.capacity == 128);
        assert(IsValidApplicationPageCount(secondPage.capacity, 37));
        assert(NextApplicationPageOffset(secondPage.offset, 37) == 165);
        assert(!IsValidApplicationPageCount(secondPage.capacity, 129));
        assert(!IsValidApplicationPageCount(secondPage.capacity, -1));
    }

    return 0;
}
