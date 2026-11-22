#include "../include/desknx/RendererStatePolicy.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using namespace desknx::ui;
    constexpr int blendMode=1;
    constexpr RendererStateSnapshot clean{false,false,blendMode};
    static_assert(RendererStateIsBaseline(clean,blendMode),
        "clean renderer state must be accepted");

    constexpr RendererStateSnapshot targetLeak{true,false,blendMode};
    static_assert(RendererStateViolations(targetLeak,blendMode)==
        RendererStateRenderTargetActive);
    constexpr RendererStateSnapshot clipLeak{false,true,blendMode};
    static_assert(RendererStateViolations(clipLeak,blendMode)==
        RendererStateClipEnabled);
    constexpr RendererStateSnapshot blendLeak{false,false,0};
    static_assert(RendererStateViolations(blendLeak,blendMode)==
        RendererStateBlendMismatch);

    constexpr RendererStateSnapshot combined{true,true,0};
    static_assert(RendererStateViolations(combined,blendMode)==
        (RendererStateRenderTargetActive|RendererStateClipEnabled|
         RendererStateBlendMismatch));
    assert(!RendererStateIsBaseline(combined,blendMode));
    assert(RendererStateIsBaseline({false,false,blendMode},blendMode));
    return 0;
}
