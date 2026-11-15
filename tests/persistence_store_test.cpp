#include "PersistenceStore.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

int main() {
    namespace fs = std::filesystem;
    using desknx::persistence::PersistenceStore;

    std::error_code ec;
    const fs::path originalDirectory = fs::current_path(ec);
    assert(!ec);

    const fs::path testDirectory = fs::temp_directory_path(ec) / "desknx-persistence-store-test";
    assert(!ec);
    fs::remove_all(testDirectory, ec);
    ec.clear();
    fs::create_directories(testDirectory, ec);
    assert(!ec);
    fs::current_path(testDirectory, ec);
    assert(!ec);

    {
        PersistenceStore store;
        PersistenceStore::AppearanceSettings appearance;
        appearance.theme = "Neon \"Night\"";
        appearance.brightness = 73;
        appearance.language = "ru";
        store.setAppearance(appearance);
        store.setSteamGridDbApiKey("test-key-\"quoted\"");

        PersistenceStore::DesktopLayout desktop;
        desktop.id = 7;
        desktop.name = "Игры и приложения";
        desktop.tileIds.push_back(0x0100F2C0115B6000ull);
        desktop.tileIds.push_back(0x0100000000001000ull);
        store.desktops().push_back(desktop);

        PersistenceStore::WidgetState widget;
        widget.id = 21;
        widget.type = "clock";
        widget.columns = 2;
        widget.rows = 1;
        widget.positionX = 3;
        widget.positionY = 4;
        widget.assetRef = "assets/clock \"blue\".png";
        store.widgets().push_back(widget);

        assert(store.save());
    }

    {
        PersistenceStore loaded;
        assert(loaded.load());

        const auto& appearance = loaded.appearance();
        assert(appearance.theme == "Neon \"Night\"");
        assert(appearance.brightness == 73);
        assert(appearance.language == "ru");
        assert(loaded.steamGridDbApiKey() == "test-key-\"quoted\"");

        assert(loaded.desktops().size() == 1);
        const auto& desktop = loaded.desktops().front();
        assert(desktop.id == 7);
        assert(desktop.name == "Игры и приложения");
        assert(desktop.tileIds.size() == 2);
        assert(desktop.tileIds[0] == 0x0100F2C0115B6000ull);
        assert(desktop.tileIds[1] == 0x0100000000001000ull);

        assert(loaded.widgets().size() == 1);
        const auto& widget = loaded.widgets().front();
        assert(widget.id == 21);
        assert(widget.type == "clock");
        assert(widget.columns == 2);
        assert(widget.rows == 1);
        assert(widget.positionX == 3);
        assert(widget.positionY == 4);
        assert(widget.assetRef == "assets/clock \"blue\".png");
    }

    fs::current_path(originalDirectory, ec);
    assert(!ec);
    fs::remove_all(testDirectory, ec);
    assert(!ec);
    return 0;
}
