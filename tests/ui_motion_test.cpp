#include <desknx/UiMotion.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

using desknx::ui::motion::easeInOutCubic;
using desknx::ui::motion::fadeAlpha;
using desknx::ui::motion::pingPongEase;
using desknx::ui::motion::transitionProgress;

static bool near(float a, float b) {
    return std::fabs(a-b) < 0.001f;
}

int main() {
    assert(near(easeInOutCubic(-1.0f),0.0f));
    assert(near(easeInOutCubic(0.0f),0.0f));
    assert(near(easeInOutCubic(0.5f),0.5f));
    assert(near(easeInOutCubic(1.0f),1.0f));
    assert(near(easeInOutCubic(2.0f),1.0f));

    assert(near(transitionProgress(100,100,150),0.0f));
    assert(near(transitionProgress(175,100,150),0.5f));
    assert(near(transitionProgress(250,100,150),1.0f));
    assert(near(transitionProgress(16,0xFFFFFFF0u,64),0.5f));
    assert(near(transitionProgress(42,42,0),1.0f));

    assert(fadeAlpha(0,150,70)==70);
    assert(fadeAlpha(75,150,70)==35);
    assert(fadeAlpha(150,150,70)==0);
    assert(fadeAlpha(1,0,70)==0);

    assert(near(pingPongEase(0,1000),0.0f));
    assert(near(pingPongEase(250,1000),0.5f));
    assert(near(pingPongEase(500,1000),1.0f));
    assert(near(pingPongEase(750,1000),0.5f));
    assert(near(pingPongEase(1000,1000),0.0f));
    assert(near(pingPongEase(0,1),1.0f));

    std::cout << "UI motion tests passed\n";
    return 0;
}
