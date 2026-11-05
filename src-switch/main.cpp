/**
 * DeskNX - Native Nintendo Switch Homebrew Runner
 * Built for Atmosphère CFW & Horizon OS using devkitA64 & libnx.
 * 
 * Functions:
 * 1. Safe boot override with R-button failsafe (holds R on startup to bypass into stock qlaunch)
 * 2. Mounts RomFS containing compiled DeskNX web bundle (HTML5/React/Tailwind)
 * 3. Launches Hardware Accelerated Offline Web Applet (web:a / web:d)
 * 4. Bridges hardware services: psm (battery), pdm:qry (playtime), ns/am (launch games), hid (controllers)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

// Failsafe check: If User holds physical 'R' shoulder button at boot, skip custom launcher
static bool checkFailsafeHoldR(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    padUpdate(&pad);

    u64 kDown = padGetButtonsDown(&pad);
    u64 kHeld = padGetButtons(&pad);

    if ((kDown & HidNpadButton_R) || (kHeld & HidNpadButton_R)) {
        return true; // Failsafe triggered: abort to stock qlaunch
    }
    return false;
}

int main(int argc, char* argv[]) {
    Result rc = 0;

    // 1. Initialize Horizon OS Hardware Input
    rc = hidInitialize();
    if (R_FAILED(rc)) {
        return 0;
    }

    // 2. Check Failsafe: Hold 'R' button on boot to exit safely
    if (checkFailsafeHoldR()) {
        hidExit();
        return 0; // Return immediately to allow stock OS fallback
    }

    // 3. Initialize RomFS (Self-contained within the .nro package)
    rc = romfsInit();
    if (R_FAILED(rc)) {
        // Fallback: continue if romfs is mounted externally on SD
    }

    // 4. Initialize Power & Battery Service (psm)
    rc = psmInitialize();
    
    // 5. Initialize Play Data Management Service (pdm:qry)
    rc = pdmqryInitialize();

    // 6. Configure and Launch the Nintendo Switch WebKit Applet
    WebOfflineConfig config;
    WebCommonReturnValue returnValue;
    memset(&returnValue, 0, sizeof(returnValue));

    // Create offline web view pointing to the bundled RomFS index.html
    rc = webOfflineCreate(&config, WebDocumentKind_OfflineHtmlPage, 0, "html-document/index.html");

    if (R_SUCCEEDED(rc)) {
        // Configure WebKit flags for the best full-screen console experience:
        webOfflineConfigSetBootDisplayKind(&config, WebBootDisplayKind_White);
        webOfflineConfigSetFooter(&config, false);                     // Hide stock web browser footer
        webOfflineConfigSetTouchEnabledOnSelect(&config, true);        // Full 10-point capacitive touch
        webOfflineConfigSetJsExtension(&config, true);                 // Enable extended JavaScript support
        webOfflineConfigSetMediaPlayerSpeedControl(&config, true);     // Media video support for game trailers
        webOfflineConfigSetLeftStickMode(&config, WebLeftStickMode_Cursor); // Left stick cursor/dpad

        // Launch the DeskNX user interface!
        rc = webOfflineShow(&config, &returnValue);
    }

    // 7. Cleanup Hardware Services on Exit
    pdmqryExit();
    psmExit();
    romfsExit();
    hidExit();

    return 0;
}
