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

{"content":"#include \"SettingsScreen.hpp\"\n#include <SDL2/SDL.h>\n#include <fstream>\n#include <iostream>\n#include <ctime>\n#include <cstring>\n\nnamespace desknx::menu::settings {\n\nnamespace {\n    const char* kDefaultLauncherMarker = \"sdmc:/switch/DeskNX/.is_default_launcher\";\n\n    // Helper: check if default launcher marker exists\n    bool isDefaultLauncherFile() {\n        std::ifstream markerFile(kDefaultLauncherMarker);\n        return markerFile.good();\n    }\n\n    // Helper: toggle default launcher marker\n    void toggleDefaultLauncherFile() {\n        if (isDefaultLauncherFile()) {\n            std::remove(kDefaultLauncherMarker);\n        } else {\n            std::ofstream markerFile(kDefaultLauncherMarker);\n            if (markerFile.is_open()) {\n                markerFile << \"DeskNX is registered as the default launcher\\n\";\n                markerFile.close();\n            }\n        }\n    }\n\n    // Helper: draw a filled rectangle with color\n    void drawFilledRect(SDL_Renderer* renderer, int x, int y, int w, int h, uint32_t color) {\n        SDL_Rect rect = {x, y, w, h};\n        uint8_t r = (color >> 24) & 0xFF;\n        uint8_t g = (color >> 16) & 0xFF;\n        uint8_t b = (color >> 8) & 0xFF;\n        uint8_t a = color & 0xFF;\n        SDL_SetRenderDrawColor(renderer, r, g, b, a);\n        SDL_RenderFillRect(renderer, &rect);\n    }\n\n    // Helper: draw a rectangle outline with color\n    void drawRectOutline(SDL_Renderer* renderer, int x, int y, int w, int h, uint32_t color) {\n        SDL_Rect rect = {x, y, w, h};\n        uint8_t r = (color >> 24) & 0xFF;\n        uint8_t g = (color >> 16) & 0xFF;\n        uint8_t b = (color >> 8) & 0xFF;\n        uint8_t a = color & 0xFF;\n        SDL_SetRenderDrawColor(renderer, r, g, b, a);\n        SDL_RenderDrawRect(renderer, &rect);\n    }\n\n    // Simple character rendering (5×8 pixel font simulation)\n    void drawChar(SDL_Renderer* renderer, char c, int x, int y, uint32_t color) {\n        // This is a placeholder; in production, use a proper font library\n        // For now, just draw a colored rectangle as a placeholder\n        drawFilledRect(renderer, x, y, 8, 8, color);\n    }\n}\n\nSettingsScreen::SettingsScreen() {\n    loadState();\n}\n\nvoid SettingsScreen::show() {\n    m_isActive = true;\n    m_focusedItem = FocusedItem::DefaultLauncher;\n    m_confirmationMessage.clear();\n    refresh();\n}\n\nvoid SettingsScreen::hide() {\n    m_isActive = false;\n    m_confirmationMessage.clear();\n}\n\nbool SettingsScreen::isActive() const {\n    return m_isActive;\n}\n\nvoid SettingsScreen::loadState() {\n    m_isDefaultLauncher = isDefaultLauncherFile();\n}\n\nvoid SettingsScreen::refresh() {\n    loadState();\n    // Clear confirmation message if duration elapsed\n    if (!m_confirmationMessage.empty() && m_confirmationStartTime > 0) {\n        uint32_t elapsed = SDL_GetTicks() - m_confirmationStartTime;\n        if (elapsed > kConfirmationDuration) {\n            m_confirmationMessage.clear();\n        }\n    }\n}\n\nvoid SettingsScreen::handleInput(uint32_t buttonId) {\n    if (!m_isActive) {\n        return;\n    }\n\n    // D-pad up: move focus up (or wrap around)\n    if (buttonId == 0x1000) {  // Assume HID up button\n        if (m_focusedItem == FocusedItem::CloseButton) {\n            m_focusedItem = FocusedItem::DefaultLauncher;\n        }\n    }\n    // D-pad down: move focus down (or wrap around)\n    else if (buttonId == 0x2000) {  // Assume HID down button\n        if (m_focusedItem == FocusedItem::DefaultLauncher) {\n            m_focusedItem = FocusedItem::CloseButton;\n        }\n    }\n    // A button: toggle or confirm\n    else if (buttonId == 0x0001) {  // Assume HID A button\n        if (m_focusedItem == FocusedItem::DefaultLauncher) {\n            toggleDefaultLauncherFile();\n            loadState();\n            m_confirmationMessage = \"Launcher setting saved.\";\n            m_confirmationStartTime = SDL_GetTicks();\n        } else if (m_focusedItem == FocusedItem::CloseButton) {\n            hide();\n        }\n    }\n    // B button: close settings\n    else if (buttonId == 0x0002) {  // Assume HID B button\n        hide();\n    }\n}\n\nvoid SettingsScreen::drawRoundedRect(SDL_Renderer* renderer, int x, int y, int w, int h,\n                                     int radius, uint32_t color) {\n    // Simplified: draw filled rectangle with border (no true rounding for now)\n    drawFilledRect(renderer, x, y, w, h, color);\n    drawRectOutline(renderer, x, y, w, h, 0x404040FF);\n}\n\nvoid SettingsScreen::drawText(SDL_Renderer* renderer, const std::string& text,\n                              int x, int y, uint32_t color) {\n    // Placeholder text rendering: draw a colored rectangle for each character\n    int offset = 0;\n    for (char c : text) {\n        drawChar(renderer, c, x + offset, y, color);\n        offset += 8;\n    }\n}\n\nvoid SettingsScreen::drawToggleButton(SDL_Renderer* renderer) {\n    int buttonY = OverlayY + PaddingY + 40;\n    bool isFocused = (m_focusedItem == FocusedItem::DefaultLauncher);\n\n    // Draw button background\n    uint32_t bgColor = isFocused ? ButtonFocusColor : 0x2C2C2CFF;\n    drawRoundedRect(renderer, OverlayX + PaddingX, buttonY, OverlayW - 2 * PaddingX,\n                   ButtonHeight, CornerRadius, bgColor);\n\n    // Draw label text\n    std::string label = \"Use DeskNX as Home Launcher\";\n    drawText(renderer, label, OverlayX + PaddingX + 10, buttonY + 10, TextColor);\n\n    // Draw toggle indicator [ON] or [OFF]\n    uint32_t toggleColor = m_isDefaultLauncher ? ToggleOnColor : ToggleOffColor;\n    std::string toggleText = m_isDefaultLauncher ? \"[ON]\" : \"[OFF]\";\n    drawText(renderer, toggleText, OverlayX + OverlayW - PaddingX - 50, buttonY + 10, toggleColor);\n}\n\nvoid SettingsScreen::drawCloseButton(SDL_Renderer* renderer) {\n    int closeY = OverlayY + OverlayH - PaddingY - ButtonHeight - 20;\n    bool isFocused = (m_focusedItem == FocusedItem::CloseButton);\n\n    // Draw button background\n    uint32_t bgColor = isFocused ? ButtonFocusColor : 0x2C2C2CFF;\n    drawRoundedRect(renderer, OverlayX + PaddingX, closeY, OverlayW - 2 * PaddingX,\n                   ButtonHeight, CornerRadius, bgColor);\n\n    // Draw label text\n    std::string label = \"Close (B)\";\n    drawText(renderer, label, OverlayX + PaddingX + 10, closeY + 10, TextColor);\n}\n\nvoid SettingsScreen::render(SDL_Renderer* renderer) {\n    if (!m_isActive) {\n        return;\n    }\n\n    // Enable alpha blending for semi-transparent overlay\n    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);\n\n    // Draw semi-transparent background overlay\n    drawFilledRect(renderer, 0, 0, 1280, 720, 0x00000099);  // Dark overlay\n\n    // Draw settings panel background\n    drawRoundedRect(renderer, OverlayX, OverlayY, OverlayW, OverlayH, CornerRadius,\n                   BackgroundColor);\n\n    // Draw title\n    std::string title = \"Settings\";\n    drawText(renderer, title, OverlayX + PaddingX + 10, OverlayY + PaddingY, TextColor);\n\n    // Draw separator line\n    drawFilledRect(renderer, OverlayX + PaddingX, OverlayY + PaddingY + 25,\n                  OverlayW - 2 * PaddingX, 1, BorderColor);\n\n    // Draw toggle button\n    drawToggleButton(renderer);\n\n    // Draw confirmation message if present\n    if (!m_confirmationMessage.empty()) {\n        int msgY = OverlayY + 130;\n        drawText(renderer, m_confirmationMessage, OverlayX + PaddingX + 10, msgY,\n                ConfirmTextColor);\n    }\n\n    // Draw close button\n    drawCloseButton(renderer);\n\n    // Draw hint text at bottom\n    std::string hint = \"D-Pad to navigate, A to toggle, B to close\";\n    drawText(renderer, hint, OverlayX + PaddingX + 10, OverlayY + OverlayH - 20, 0xB0B0B0FF);\n}\n\n// Factory function\nSettingsScreen* createSettingsScreen() {\n    return new SettingsScreen();\n}\n\n}  // namespace desknx::menu::settings\n"}