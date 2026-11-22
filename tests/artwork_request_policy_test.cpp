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
    return 0;
}
