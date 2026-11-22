#include "../include/desknx/ArtworkRequestPolicy.hpp"
#include <cassert>
int main() {
    using desknx::artwork::workspacePrefetchPriority;
    using desknx::artwork::tileRenderPriority;
    using desknx::model::Kind;
    using desknx::model::Tile;
    const Tile focus{Kind::Game,"Focus","0100000000001000",2,1,2,1,0};
    const Tile adjacent{Kind::Game,"Adjacent","0100000000002000",4,1,1,1,0};
    const Tile far{Kind::Nro,"Far","sdmc:/switch/far.nro",0,3,1,1,0};
    const int pFocus=workspacePrefetchPriority(focus,2,1);
    const int pAdjacent=workspacePrefetchPriority(adjacent,2,1);
    const int pFar=workspacePrefetchPriority(far,2,1);
    assert(pFocus>pAdjacent);
    assert(pAdjacent>pFar);
    assert(tileRenderPriority(true)>tileRenderPriority(false));
    assert(tileRenderPriority(false)>pFocus);
    assert(pFar>=24);
    assert(workspacePrefetchPriority(focus,3,1)==pFocus);

    using desknx::artwork::PrefetchContext;
    using desknx::artwork::shouldRebuildPrefetchContext;
    const PrefetchContext initial{};
    const PrefetchContext desktop{true,false,0,10,20};
    assert(shouldRebuildPrefetchContext(initial,desktop));
    assert(!shouldRebuildPrefetchContext(desktop,desktop));

    PrefetchContext switched=desktop;
    switched.workspace=1;
    assert(shouldRebuildPrefetchContext(desktop,switched));

    PrefetchContext library=desktop;
    library.listOpen=true;
    assert(shouldRebuildPrefetchContext(desktop,library));

    PrefetchContext catalogueChanged=desktop;
    ++catalogueChanged.catalogueRevision;
    assert(shouldRebuildPrefetchContext(desktop,catalogueChanged));

    PrefetchContext layoutChanged=desktop;
    ++layoutChanged.layoutRevision;
    assert(shouldRebuildPrefetchContext(desktop,layoutChanged));

    // Focus coordinates are deliberately absent from PrefetchContext:
    // moving selection does not restart/cancel the background decode queue.
    return 0;
}
