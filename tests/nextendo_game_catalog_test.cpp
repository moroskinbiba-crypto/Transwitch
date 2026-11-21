#include <desknx/NextendoGameCatalog.hpp>

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace desknx::nextendo;

    assert(sameGameVersion("4.0.1", "4.0.1.0"));
    assert(sameGameVersion("1.2.0", "1.2"));
    assert(sameGameVersion("02.10", "2.10"));
    assert(!sameGameVersion("1.2.3", "1.2.4"));
    assert(!sameGameVersion("1.2-beta", "1.2"));
    assert(!sameGameVersion("1..2", "1.2"));

    GameCatalog catalog;
    catalog.revision = 7;
    catalog.expiresAt = 2000000000;
    GameVersions exact;
    exact.enabled = true;
    exact.policy = "exact";
    exact.accepted = {"3.2.1", "3.2.2"};
    exact.recommended = "3.2.2";
    catalog.games.emplace(0x0100000000001000ull, exact);

    GameVersions any;
    any.enabled = true;
    any.policy = "any";
    catalog.games.emplace(0x0100000000002000ull, any);

    GameVersions disabled;
    disabled.enabled = false;
    disabled.policy = "exact";
    disabled.accepted = {"1.0.0"};
    catalog.games.emplace(0x0100000000003000ull, disabled);

    assert(catalog.check(0x0100000000001000ull, "3.2.1").state ==
        CompatibilityState::Accepted);
    const auto update = catalog.check(0x0100000000001000ull, "3.2.0");
    assert(update.state == CompatibilityState::UpdateRequired);
    assert(update.recommended == "3.2.2");
    assert(catalog.check(0x0100000000002000ull, "99.0").state ==
        CompatibilityState::Accepted);
    assert(catalog.check(0x0100000000003000ull, "1.0.0").state ==
        CompatibilityState::Disabled);
    assert(catalog.check(0x0100000000004000ull, "1.0.0").state ==
        CompatibilityState::NotListed);
    assert(catalog.check(0x0100000000001000ull, "").state ==
        CompatibilityState::Unknown);
    assert(catalog.fresh(1999999999));
    assert(!catalog.fresh(2000000000));

    GameCatalog parsed;
    std::string error;
    assert(!parseSignedGameCatalog(
        R"({"key_id":"catalog-key-1","payload":"Zm9v","signature":"YmFy"})",
        1900000000, parsed, error));
    assert(!error.empty());
    assert(parsed.games.empty());

    std::puts("nextendo_game_catalog_test: ok");
    return 0;
}
