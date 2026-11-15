#pragma once

#include <cstdint>
#include <string>

struct SDL_Renderer;

namespace desknx::menu::settings {

// Settings overlay screen for DeskNX menu
// Accessible via `-` button from grid view
// Displays toggles and options for the launcher
class SettingsScreen {
public:
    enum class FocusedItem {
        DefaultLauncher,
        CloseButton,
        None = -1,
    };

    SettingsScreen();
    ~SettingsScreen() = default;

    /// Show the settings overlay
    void show();

    /// Hide the settings overlay
    void hide();

    /// Check if the settings overlay is currently active (visible)
    bool isActive() const;

    /// Handle HID input (D-pad up/down, A to toggle, B to close)
    void handleInput(uint32_t buttonId);

    /// Render the settings panel overlay on top of the grid
    void render(SDL_Renderer* renderer);

    /// Refresh UI state from file system (call after toggle operations)
    void refresh();

private:
    bool m_isActive = false;
    FocusedItem m_focusedItem = FocusedItem::DefaultLauncher;
    bool m_isDefaultLauncher = false;
    std::string m_confirmationMessage;
    uint32_t m_confirmationStartTime = 0;
    static constexpr uint32_t kConfirmationDuration = 2000;  // ms

    /// Layout constants for 1280×720 display
    static constexpr int OverlayX = 440;      // (1280 - 400) / 2
    static constexpr int OverlayY = 210;      // (720 - 300) / 2
    static constexpr int OverlayW = 400;
    static constexpr int OverlayH = 300;
    static constexpr int PaddingX = 20;
    static constexpr int PaddingY = 20;
    static constexpr int ButtonHeight = 50;
    static constexpr int ButtonMargin = 15;
    static constexpr int CornerRadius = 12;

    /// Color palette
    static constexpr uint32_t BackgroundColor = 0x1A1A1AEE;    // Semi-transparent dark
    static constexpr uint32_t BorderColor = 0x404040FF;
    static constexpr uint32_t TextColor = 0xFFFFFFFF;
    static constexpr uint32_t ButtonFocusColor = 0x404080FF;   // Blue tinted for focus
    static constexpr uint32_t ToggleOnColor = 0x32CD32FF;      // Lime green
    static constexpr uint32_t ToggleOffColor = 0xCC3333FF;     // Red
    static constexpr uint32_t ConfirmTextColor = 0x90EE90FF;   // Light green

    /// Load the current default launcher status from file
    void loadState();

    /// Draw a rounded rectangle (helper)
    void drawRoundedRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                        int radius, uint32_t color);

    /// Draw text (simplified, uses basic character rendering)
    void drawText(SDL_Renderer* renderer, const std::string& text,
                 int x, int y, uint32_t color);

    /// Draw the toggle button with [ON] or [OFF] indicator
    void drawToggleButton(SDL_Renderer* renderer);

    /// Draw the close button (B button hint)
    void drawCloseButton(SDL_Renderer* renderer);
};

// Factory
SettingsScreen* createSettingsScreen();

}  // namespace desknx::menu::settings
