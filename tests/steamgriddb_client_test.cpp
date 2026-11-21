#include <desknx/SteamGridDbClient.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    using desknx::steamgriddb::titleMatchScore;
    assert(std::fabs(titleMatchScore("Super Mario Odyssey",
                                     "Super Mario Odyssey") - 1.0f) < 0.001f);
    assert(titleMatchScore("The Legend of Zelda Breath of the Wild",
                           "The Legend of Zelda: Breath of the Wild") > 0.90f);
    assert(titleMatchScore("Mario Kart 8",
                           "Mario Kart 8 Deluxe") > 0.90f);
    assert(titleMatchScore("Metroid Dread",
                           "Animal Crossing New Horizons") < 0.35f);
    assert(titleMatchScore("", "Game") == 0.0f);
    std::puts("steamgriddb_client_test: ok");
    return 0;
}
