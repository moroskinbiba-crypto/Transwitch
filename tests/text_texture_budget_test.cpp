#include "../include/desknx/TextTextureBudget.hpp"

#include <cassert>

int main() {
    using namespace desknx::ui;
    static_assert(MaxTextTextureBuildsPerFrame==12);

    TextTextureFrameBudget budget;
    assert(budget.limit()==12);
    assert(budget.used()==0&&budget.remaining()==12);
    for(int i=0;i<12;++i) {
        assert(budget.tryConsume());
        assert(budget.used()==i+1);
    }
    assert(budget.remaining()==0);
    assert(!budget.tryConsume());
    assert(budget.used()==12);

    budget.beginFrame();
    assert(budget.used()==0&&budget.remaining()==12);
    assert(budget.tryConsume());

    TextTextureFrameBudget smallBudget(2);
    assert(smallBudget.tryConsume());
    assert(smallBudget.tryConsume());
    assert(!smallBudget.tryConsume());
    smallBudget.beginFrame();
    assert(smallBudget.tryConsume());

    return 0;
}
