#include "SettingsScreen.hpp"
#include <SDL2/SDL.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <cstring>

namespace desknx::menu::settings {

namespace {
    const char* kDefaultLauncherMarker = "sdmc:/switch/DeskNX/.is_default_launcher";

    // Helper: check if default launcher marker exists
    bool isDefaultLauncherFile() {
        std::ifstream markerFile(kDefaultLauncherMarker);
        return markerFile.good();
    }

    // Helper: toggle default launcher marker
    void toggleDefaultLauncherFile() {
        if (isDefaultLauncherFile()) {
            std::remove(kDefaultLauncherMarker);
        } else {
            std::ofstream markerFile(kDefaultLauncherMarker);
            if (markerFile.is_open()) {
                markerFile << "DeskNX is registered as the default launcher\n";
                markerFile.close();
            }
        }
    }

    // Helper: draw a filled rectangle with color
    void drawFilledRect(SDL_Renderer* renderer, int x, int y, int w, int h, uint32_t color) {
        SDL_Rect rect = {x, y, w, h};
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &rect);
    }

    // Helper: draw a rectangle outline with color
    void drawRectOutline(SDL_Renderer* renderer, int x, int y, int w, int h, uint32_t color) {
        SDL_Rect rect = {x, y, w, h};
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderDrawRect(renderer, &rect);
    }

    // Simple character rendering (5×8 pixel font simulation)
    void drawChar(SDL_Renderer* renderer, char c, int x, int y, uint32_t color) {
        // This is a placeholder; in production, use a proper font library
        // For now, just draw a colored rectangle as a placeholder
        drawFilledRect(renderer, x, y, 8, 8, color);
    }
}

SettingsScreen::SettingsScreen() {
    loadState();
}

void SettingsScreen::show() {
    m_isActive = true;
    m_focusedItem = FocusedItem::DefaultLauncher;
    m_confirmationMessage.clear();
    refresh();
}

void SettingsScreen::hide() {
    m_isActive = false;
    m_confirmationMessage.clear();
}

bool SettingsScreen::isActive() const {
    return m_isActive;
}

void SettingsScreen::loadState() {
    m_isDefaultLauncher = isDefaultLauncherFile();
}

void SettingsScreen::refresh() {
    loadState();
    // Clear confirmation message if duration elapsed
    if (!m_confirmationMessage.empty() && m_confirmationStartTime > 0) {
        uint32_t elapsed = SDL_GetTicks() - m_confirmationStartTime;
        if (elapsed > kConfirmationDuration) {
            m_confirmationMessage.clear();
        }
    }
}

void SettingsScreen::handleInput(uint32_t buttonId) {
    if (!m_isActive) {
        return;
    }

    // D-pad up: move focus up (or wrap around)
    if (buttonId == 0x1000) {  // Assume HID up button
        if (m_focusedItem == FocusedItem::CloseButton) {
            m_focusedItem = FocusedItem::DefaultLauncher;
        }
    }
    // D-pad down: move focus down (or wrap around)
    else if (buttonId == 0x2000) {  // Assume HID down button
        if (m_focusedItem == FocusedItem::DefaultLauncher) {
            m_focusedItem = FocusedItem::CloseButton;
        }
    }
    // A button: toggle or confirm
    else if (buttonId == 0x0001) {  // Assume HID A button
        if (m_focusedItem == FocusedItem::DefaultLauncher) {
            toggleDefaultLauncherFile();
            loadState();
            m_confirmationMessage = "Launcher setting saved.";
            m_confirmationStartTime = SDL_GetTicks();
        } else if (m_focusedItem == FocusedItem::CloseButton) {
            hide();
        }
    }
    // B button: close settings
    else if (buttonId == 0x0002) {  // Assume HID B button
        hide();
    }
}

void SettingsScreen::drawRoundedRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                                     int radius, uint32_t color) {
    // Simplified: draw filled rectangle with border (no true rounding for now)
    drawFilledRect(renderer, x, y, w, h, color);
    drawRectOutline(renderer, x, y, w, h, 0x404040FF);
}

void SettingsScreen::drawText(SDL_Renderer* renderer, const std::string& text,
                              int x, int y, uint32_t color) {
    // Placeholder text rendering: draw a colored rectangle for each character
    int offset = 0;
    for (char c : text) {
        drawChar(renderer, c, x + offset, y, color);
        offset += 8;
    }
}

void SettingsScreen::drawToggleButton(SDL_Renderer* renderer) {
    int buttonY = OverlayY + PaddingY + 40;
    bool isFocused = (m_focusedItem == FocusedItem::DefaultLauncher);

    // Draw button background
    uint32_t bgColor = isFocused ? ButtonFocusColor : 0x2C2C2CFF;
    drawRoundedRect(renderer, OverlayX + PaddingX, buttonY, OverlayW - 2 * PaddingX,
                   ButtonHeight, CornerRadius, bgColor);

    // Draw label text
    std::string label = "Use DeskNX as Home Launcher";
    drawText(renderer, label, OverlayX + PaddingX + 10, buttonY + 10, TextColor);

    // Draw toggle indicator [ON] or [OFF]
    uint32_t toggleColor = m_isDefaultLauncher ? ToggleOnColor : ToggleOffColor;
    std::string toggleText = m_isDefaultLauncher ? "[ON]" : "[OFF]";
    drawText(renderer, toggleText, OverlayX + OverlayW - PaddingX - 50, buttonY + 10, toggleColor);
}

void SettingsScreen::drawCloseButton(SDL_Renderer* renderer) {
    int closeY = OverlayY + OverlayH - PaddingY - ButtonHeight - 20;
    bool isFocused = (m_focusedItem == FocusedItem::CloseButton);

    // Draw button background
    uint32_t bgColor = isFocused ? ButtonFocusColor : 0x2C2C2CFF;
    drawRoundedRect(renderer, OverlayX + PaddingX, closeY, OverlayW - 2 * PaddingX,
                   ButtonHeight, CornerRadius, bgColor);

    // Draw label text
    std::string label = "Close (B)";
    drawText(renderer, label, OverlayX + PaddingX + 10, closeY + 10, TextColor);
}

void SettingsScreen::render(SDL_Renderer* renderer) {
    if (!m_isActive) {
        return;
    }

    // Enable alpha blending for semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Draw semi-transparent background overlay
    drawFilledRect(renderer, 0, 0, 1280, 720, 0x00000099);  // Dark overlay

    // Draw settings panel background
    drawRoundedRect(renderer, OverlayX, OverlayY, OverlayW, OverlayH, CornerRadius,
                   BackgroundColor);

    // Draw title
    std::string title = "Settings";
    drawText(renderer, title, OverlayX + PaddingX + 10, OverlayY + PaddingY, TextColor);

    // Draw separator line
    drawFilledRect(renderer, OverlayX + PaddingX, OverlayY + PaddingY + 25,
                  OverlayW - 2 * PaddingX, 1, BorderColor);

    // Draw toggle button
    drawToggleButton(renderer);

    // Draw confirmation message if present
    if (!m_confirmationMessage.empty()) {
        int msgY = OverlayY + 130;
        drawText(renderer, m_confirmationMessage, OverlayX + PaddingX + 10, msgY,
                ConfirmTextColor);
    }

    // Draw close button
    drawCloseButton(renderer);

    // Draw hint text at bottom
    std::string hint = "D-Pad to navigate, A to toggle, B to close";
    drawText(renderer, hint, OverlayX + PaddingX + 10, OverlayY + OverlayH - 20, 0xB0B0B0FF);
}

// Factory function
SettingsScreen* createSettingsScreen() {
    return new SettingsScreen();
}

}  // namespace desknx::menu::settings
