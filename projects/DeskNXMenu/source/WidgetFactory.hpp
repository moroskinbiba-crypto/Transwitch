#pragma once

#include <desknx/Model.hpp>
#include <string>
#include <ctime>

namespace desknx::menu::widgets {

// Mock widget data sources for rendering
class WidgetFactory {
public:
    struct ClockData {
        int hour = 12;
        int minute = 0;
        int second = 0;
    };
    
    struct BatteryData {
        int percentage = 75;
        bool charging = false;
    };
    
    struct RecentGameData {
        std::string title;
        uint64_t playtimeMinutes = 0;
        std::string icon; // path or placeholder
    };
    
    static ClockData getCurrentClock() {
        time_t now = time(nullptr);
        tm* local = localtime(&now);
        return {local->tm_hour, local->tm_min, local->tm_sec};
    }
    
    static BatteryData getMockBattery() {
        // In a real implementation, query via PSM service or PSC.
        return {75, false};
    }
    
    static RecentGameData getMockRecentGame() {
        // In a real implementation, query recent activity from WorkspaceProvider.
        return {"Recent Game", 120, "game-icon.jpg"};
    }
    
    /// Create a widget tile for a given kind and size.
    static model::Tile createWidgetTile(model::Kind kind, int w = 1, int h = 1) {
        model::Tile tile;
        tile.kind = kind;
        tile.w = w;
        tile.h = h;
        
        switch (kind) {
            case model::Kind::Clock:
                tile.title = "Clock";
                tile.data = "clock";
                break;
            case model::Kind::Battery:
                tile.title = "Battery";
                tile.data = "battery";
                break;
            case model::Kind::Recent:
                tile.title = "Recent";
                tile.data = "recent";
                break;
            default:
                break;
        }
        return tile;
    }
};

} // namespace desknx::menu::widgets
