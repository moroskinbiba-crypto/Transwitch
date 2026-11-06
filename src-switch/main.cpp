/**
 * DeskNX - Native Nintendo Switch Homebrew Runner
 * Built for Atmosphère CFW & Horizon OS using devkitA64 & libnx.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

// Failsafe check: If User holds physical 'R' shoulder button at boot, skip custom launcher
static bool checkFailsafeHoldR(PadState* pad) {
    padUpdate(pad);
    u64 kDown = padGetButtonsDown(pad);
    u64 kHeld = padGetButtons(pad);

    if ((kDown & HidNpadButton_R) || (kHeld & HidNpadButton_R)) {
        return true; // Failsafe triggered: abort to stock qlaunch
    }
    return false;
}

static void showDiagnosticScreen(const char* title, Result rc, bool isAppletMode) {
    consoleInit(NULL);
    printf("\x1b[1;36m=====================================================\x1b[0m\n");
    printf("\x1b[1;37m        DeskNX Launcher - Nintendo Switch            \x1b[0m\n");
    printf("\x1b[1;36m=====================================================\x1b[0m\n\n");

    if (isAppletMode) {
        printf("\x1b[1;31m[!] RUNNING IN APPLET MODE (ALBUM)\x1b[0m\n\n");
        printf("Nintendo Switch limits memory in Album mode (~400MB)\n");
        printf("and blocks launching the Horizon WebKit UI applet.\n\n");
        printf("\x1b[1;33m>>> HOW TO LAUNCH IN TITLE OVERRIDE (REQUIRED): <<<\x1b[0m\n");
        printf("1. Close this app and return to the main Switch menu.\n");
        printf("2. Hold down the \x1b[1;32m'R' shoulder button\x1b[0m on your controller.\n");
        printf("3. While holding 'R', press \x1b[1;32m'A'\x1b[0m on ANY installed game.\n");
        printf("4. Keep holding 'R' until the Homebrew Menu opens.\n");
        printf("5. Select and launch DeskNX from Homebrew Menu.\n\n");
    } else {
        printf("\x1b[1;31m[!] Failed to launch WebKit UI applet.\x1b[0m\n\n");
        printf("Error Code: \x1b[1;31m0x%08X\x1b[0m (%s)\n\n", (unsigned int)rc, title);
        printf("Please ensure RomFS is bundled inside DeskNX.nro.\n\n");
    }

    printf("\x1b[1;37mPress '+' or 'B' to exit back to Homebrew Menu.\x1b[0m\n");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & (HidNpadButton_Plus | HidNpadButton_B | HidNpadButton_A)) {
            break;
        }
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
}

int main(int argc, char* argv[]) {
    Result rc = 0;

    // 1. Initialize Horizon OS Hardware Input
    rc = hidInitialize();
    if (R_FAILED(rc)) {
        return 0;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // 2. Check Failsafe: Hold 'R' button on boot to exit safely
    if (checkFailsafeHoldR(&pad)) {
        hidExit();
        return 0; // Return immediately to allow stock OS fallback
    }

    // 3. Initialize RomFS (Self-contained within the .nro package)
    Result romfsRc = romfsInit();

    // 4. Check if launched in Applet Mode (Album) vs Title Override (Game)
    AppletType appletType = appletGetAppletType();
    bool isAppletMode = (appletType != AppletType_Application);

    if (isAppletMode) {
        showDiagnosticScreen("Applet Mode Restriction", 0x2BA4, true);
        if (R_SUCCEEDED(romfsRc)) romfsExit();
        hidExit();
        return 0;
    }

    // 5. Initialize Power & Battery Service (psm)
    psmInitialize();
    
    // 6. Initialize Play Data Management Service (pdm:qry)
    pdmqryInitialize();

    // 7. Configure and Launch the Nintendo Switch WebKit Applet
    WebCommonConfig config;
    WebCommonReply reply;
    memset(&reply, 0, sizeof(reply));

    // Try candidate RomFS document paths relative to html-document/
    rc = webOfflineCreate(&config, WebDocumentKind_OfflineHtmlPage, 0, "app.htdocs/index.html");
    if (R_FAILED(rc)) {
        rc = webOfflineCreate(&config, WebDocumentKind_OfflineHtmlPage, 0, "index.html");
    }
    if (R_FAILED(rc)) {
        rc = webOfflineCreate(&config, WebDocumentKind_OfflineHtmlPage, 0, ".htdocs/index.html");
    }

    if (R_SUCCEEDED(rc)) {
        // Configure WebKit flags for the best full-screen console experience:
        webConfigSetBootDisplayKind(&config, WebBootDisplayKind_White);
        webConfigSetFooter(&config, false);                     // Hide stock web browser footer
        webConfigSetTouchEnabledOnContents(&config, true);       // Full 10-point capacitive touch
        webConfigSetJsExtension(&config, true);                 // Enable extended JavaScript support
        webConfigSetMediaPlayerSpeedControl(&config, true);     // Media video support for game trailers
        webConfigSetLeftStickMode(&config, WebLeftStickMode_Cursor); // Left stick cursor/dpad

        // Launch the DeskNX user interface!
        rc = webConfigShow(&config, &reply);
    }

    if (R_FAILED(rc)) {
        showDiagnosticScreen("webConfigShow Failure", rc, false);
    }

    // 8. Cleanup Hardware Services on Exit
    pdmqryExit();
    psmExit();
    if (R_SUCCEEDED(romfsRc)) {
        romfsExit();
    }
    hidExit();

    return 0;
}
