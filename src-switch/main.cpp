/**
 * DeskNX - Native Nintendo Switch Homebrew Runner
 * Built for Atmosphère CFW & Horizon OS using devkitA64 & libnx.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>
#include "../include/desknx/Diagnostics.hpp"

extern "C" void userAppInit(void) {
    // libnx calls this after its default services and SD mount, before main().
    struct stat sdRoot{};
    const bool sdMounted = stat("sdmc:/", &sdRoot) == 0;
    desknx::diag::Initialize(sdMounted);
    desknx::diag::Write("standalone", "libnx_init_complete",
        "sd_mounted=%d applet_type=%u", sdMounted ? 1 : 0,
        static_cast<unsigned int>(appletGetAppletType()));
}

extern "C" void userAppExit(void) {
    desknx::diag::Write("standalone", "libnx_exit", "orderly_exit=1");
}

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
    (void)argv;
    desknx::diag::Write("standalone", "main_enter", "argc=%d", argc);

    // libnx normally initializes HID before this entry point. Keep the
    // reference-counted init for the controller fail-safe and log failures.
    Result rc = hidInitialize();
    desknx::diag::Write("standalone", "hid_init", "rc=0x%08X",
                        static_cast<unsigned int>(rc));
    if (R_FAILED(rc)) return 1;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);

    if (checkFailsafeHoldR(&pad)) {
        desknx::diag::Write("standalone", "failsafe_r", "held=1");
        hidExit();
        return 0;
    }

    const AppletType appletType = appletGetAppletType();
    const bool isAppletMode = (appletType != AppletType_Application);
    desknx::diag::Write("standalone", "applet_mode",
        "applet_type=%u is_applet=%d", static_cast<unsigned int>(appletType),
        isAppletMode ? 1 : 0);

    if (isAppletMode) {
        showDiagnosticScreen("Applet Mode Restriction", 0x2BA4, true);
        desknx::diag::Write("standalone", "applet_mode_exit", "title_override_required=1");
        hidExit();
        return 0;
    }

    const Result romfsRc = romfsInit();
    desknx::diag::Write("standalone", "romfs_init", "rc=0x%08X",
                        static_cast<unsigned int>(romfsRc));
    if (R_FAILED(romfsRc)) {
        showDiagnosticScreen("romfsInit failed (NRO has no embedded RomFS)", romfsRc, false);
        hidExit();
        return 0;
    }

    // Both documented paths contain ".htdocs/" and are relative to
    // html-document/. A bare "index.html" is not a valid offline docPath.
    WebCommonConfig config{};
    WebCommonReply reply{};
    const char* documentPaths[] = {
        "app.htdocs/index.html",
        ".htdocs/index.html",
    };
    for (const char* path : documentPaths) {
        const Result pathRc = webOfflineCreate(
            &config, WebDocumentKind_OfflineHtmlPage, 0, path);
        desknx::diag::Write("standalone", "web_offline_create",
            "path=%s rc=0x%08X", path, static_cast<unsigned int>(pathRc));
        rc = pathRc;
        if (R_SUCCEEDED(rc)) break;
    }

    if (R_FAILED(rc)) {
        showDiagnosticScreen("webOfflineCreate failed", rc, false);
        romfsExit();
        hidExit();
        return 0;
    }

    // These options are version-gated. Log optional failures but still try
    // to display the offline document.
#define DESKNX_WEB_OPTION(label, expression) do { \
        const Result optionRc = (expression); \
        if (R_FAILED(optionRc)) \
            desknx::diag::Write("standalone", "web_option_failed", \
                "option=%s rc=0x%08X", label, static_cast<unsigned int>(optionRc)); \
    } while (0)
    DESKNX_WEB_OPTION("boot_display", webConfigSetBootDisplayKind(&config, WebBootDisplayKind_White));
    DESKNX_WEB_OPTION("footer", webConfigSetFooter(&config, false));
    DESKNX_WEB_OPTION("touch", webConfigSetTouchEnabledOnContents(&config, true));
    DESKNX_WEB_OPTION("js_extension", webConfigSetJsExtension(&config, true));
    DESKNX_WEB_OPTION("media_speed_control", webConfigSetMediaPlayerSpeedControl(&config, true));
    DESKNX_WEB_OPTION("left_stick", webConfigSetLeftStickMode(&config, WebLeftStickMode_Cursor));
#undef DESKNX_WEB_OPTION

    desknx::diag::Write("standalone", "web_show_begin", "launching_offline_page=1");
    rc = webConfigShow(&config, &reply);
    desknx::diag::Write("standalone", "web_show_end", "rc=0x%08X",
                        static_cast<unsigned int>(rc));
    if (R_FAILED(rc))
        showDiagnosticScreen("webConfigShow failed", rc, false);

    romfsExit();
    hidExit();
    return 0;
}
