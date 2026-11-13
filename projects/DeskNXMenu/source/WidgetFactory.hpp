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

{"content":"#pragma once\n\n#include <desknx/Model.hpp>\n#include <string>\n#include <ctime>\n\nnamespace desknx::menu::widgets {\n\n// Mock widget data sources for rendering\nclass WidgetFactory {\npublic:\n    struct ClockData {\n        int hour = 12;\n        int minute = 0;\n        int second = 0;\n    };\n    \n    struct BatteryData {\n        int percentage = 75;\n        bool charging = false;\n    };\n    \n    struct RecentGameData {\n        std::string title;\n        uint64_t playtimeMinutes = 0;\n        std::string icon; // path or placeholder\n    };\n    \n    static ClockData getCurrentClock() {\n        time_t now = time(nullptr);\n        tm* local = localtime(&now);\n        return {local->tm_hour, local->tm_min, local->tm_sec};\n    }\n    \n    static BatteryData getMockBattery() {\n        // In a real implementation, query via PSM service or PSC.\n        return {75, false};\n    }\n    \n    static RecentGameData getMockRecentGame() {\n        // In a real implementation, query recent activity from WorkspaceProvider.\n        return {\"Recent Game\", 120, \"game-icon.jpg\"};\n    }\n    \n    /// Create a widget tile for a given kind and size.\n    static model::Tile createWidgetTile(model::Kind kind, int w = 1, int h = 1) {\n        model::Tile tile;\n        tile.kind = kind;\n        tile.w = w;\n        tile.h = h;\n        \n        switch (kind) {\n            case model::Kind::Clock:\n                tile.title = \"Clock\";\n                tile.data = \"clock\";\n                break;\n            case model::Kind::Battery:\n                tile.title = \"Battery\";\n                tile.data = \"battery\";\n                break;\n            case model::Kind::Recent:\n                tile.title = \"Recent\";\n                tile.data = \"recent\";\n                break;\n            default:\n                break;\n        }\n        return tile;\n    }\n};\n\n} // namespace desknx::menu::widgets\n"}