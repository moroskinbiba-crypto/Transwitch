#include <switch.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define DEFAULT_NRO "sdmc:/hbmenu.nro"

// DeskNX fork: one-shot target file written by DeskNX system backend right before this
// loader is launched. If it exists, the NRO named inside is loaded instead of
// DEFAULT_NRO, and when that NRO chain finishes the process EXITS (returning
// control to DeskNX) instead of stock hbloader's "reload the default NRO
// forever" loop, which made closed homebrew reopen endlessly.
#define TARGET_FILE "sdmc:/switch/DeskNX/hbtarget.txt"

// DeskNX intentionally launches homebrew in the album/library-applet slot.
// This keeps the implementation independent from a donor game's process and
// avoids pretending that applet-mode memory/permissions are equivalent to a
// full application. A target .nro can chainload another NRO in this same slot;
// once the chain finishes, the loader exits and the HOME backend reopens DeskNX.
#define HB_LOG_FILE  "sdmc:/switch/DeskNX/logs/hbloader.log"

const char g_noticeText[] =
    "nx-hbloader " VERSION "\0"
    "Do you mean to tell me that you're thinking seriously of building that way, when and if you are an architect?";

static char g_argv[2048];
static char g_nextArgv[2048];
static char g_nextNroPath[512];
u64  g_nroAddr = 0;
static u64  g_nroSize = 0;
static NroHeader g_nroHeader;
static bool g_isApplication = 0;
static bool g_targetMode = 0;   // DeskNX fork: loaded a specific NRO from TARGET_FILE

static bool g_isAutomaticGameplayRecording = 0;
static enum {
    CodeMemoryUnavailable    = 0,
    CodeMemoryForeignProcess = BIT(0),
    CodeMemorySameProcess    = BIT(0) | BIT(1),
} g_codeMemoryCapability = CodeMemoryUnavailable;

static Handle g_procHandle;

static void*  g_heapAddr;
static size_t g_heapSize;

static u64 g_appletHeapSize = 0;
static u64 g_appletHeapReservationSize = 0;

static u128 g_userIdStorage;

static u8 g_savedTls[0x100];

// Minimize fs resource usage
u32 __nx_fs_num_sessions = 1;
u32 __nx_fsdev_direntry_cache_size = 1;
bool __nx_fsdev_support_cwd = false;

// Used by trampoline.s
Result g_lastRet = 0;

void NX_NORETURN nroEntrypointTrampoline(const ConfigEntry* entries, u64 handle, u64 entrypoint);

void __libnx_initheap(void)
{
    static char g_innerheap[0x4000];

    extern char* fake_heap_start;
    extern char* fake_heap_end;

    fake_heap_start = &g_innerheap[0];
    fake_heap_end   = &g_innerheap[sizeof g_innerheap];
}

static Result readSetting(const char* key, void* buf, size_t size)
{
    Result rc;
    u64 actual_size;
    const char* const section_name = "hbloader";
    rc = setsysGetSettingsItemValueSize(section_name, key, &actual_size);
    if (R_SUCCEEDED(rc) && actual_size != size)
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (R_SUCCEEDED(rc))
        rc = setsysGetSettingsItemValue(section_name, key, buf, size, &actual_size);
    if (R_SUCCEEDED(rc) && actual_size != size)
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (R_FAILED(rc)) memset(buf, 0, size);
    return rc;
}

void __appInit(void)
{
    Result rc;

    // Detect Atmosphere early on. This is required for hosversion logic.
    // In the future, this check will be replaced by detectMesosphere().
    Handle dummy;
    rc = svcConnectToNamedPort(&dummy, "ams");
    u32 ams_flag = (R_VALUE(rc) != KERNELRESULT(NotFound)) ? BIT(31) : 0;
    if (R_SUCCEEDED(rc))
        svcCloseHandle(dummy);

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 1));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(ams_flag | MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        readSetting("applet_heap_size", &g_appletHeapSize, sizeof(g_appletHeapSize));
        readSetting("applet_heap_reservation_size", &g_appletHeapReservationSize, sizeof(g_appletHeapReservationSize));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 2));
}

void __wrap_exit(void)
{
    // exit() effectively never gets called, so let's stub it out.
    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 39));
}

static u64 calculateMaxHeapSize(void)
{
    u64 size = 0;
    u64 mem_available = 0, mem_used = 0;

    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    if (mem_available > mem_used+0x200000)
        size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
        size = 0x2000000*16;
    if (size > 0x6000000 && g_isAutomaticGameplayRecording)
        size -= 0x6000000;

    return size;
}

static void setupHbHeap(void)
{
    void* addr = NULL;
    u64 size = calculateMaxHeapSize();

    if (!g_isApplication) {
        if (g_appletHeapSize) {
            u64 requested_size = (g_appletHeapSize + 0x1FFFFF) &~ 0x1FFFFF;
            if (requested_size < size)
                size = requested_size;
        }
        else if (g_appletHeapReservationSize) {
            u64 reserved_size = (g_appletHeapReservationSize + 0x1FFFFF) &~ 0x1FFFFF;
            if (reserved_size < size)
                size -= reserved_size;
        }
    }

    Result rc = svcSetHeapSize(&addr, size);

    if (R_FAILED(rc) || addr==NULL)
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 9));

    g_heapAddr = addr;
    g_heapSize = size;
}

static void procHandleReceiveThread(void* arg)
{
    Handle session = (Handle)(uintptr_t)arg;
    Result rc;

    void* base = armGetTls();
    hipcMakeRequestInline(base);

    s32 idx = 0;
    rc = svcReplyAndReceive(&idx, &session, 1, INVALID_HANDLE, UINT64_MAX);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 15));

    HipcParsedRequest r = hipcParseRequest(base);
    if (r.meta.num_copy_handles != 1)
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 17));

    g_procHandle = r.data.copy_handles[0];
    svcCloseHandle(session);
}

// Sets g_isApplication if the hbloader process is running as an Application.
static void getIsApplication(void)
{
    Result rc;

    // Try asking the kernel directly (only works on [9.0.0+] or mesosphere)
    u64 flag=0;
    rc = svcGetInfo(&flag, InfoType_IsApplication, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(rc)) {
        g_isApplication = flag!=0;
        return;
    }

    // Retrieve our process' PID
    u64 cur_pid=0;
    rc = svcGetProcessId(&cur_pid, CUR_PROCESS_HANDLE);
    if (R_FAILED(rc)) diagAbortWithResult(rc); // shouldn't happen

    // Try reading the current application PID through pm:shell - if it matches ours then we are indeed an application
    rc = pmshellInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 app_pid=0;
        rc = pmshellGetApplicationProcessIdForShell(&app_pid);
        pmshellExit();

        if (cur_pid == app_pid)
            g_isApplication = 1;
    }
}

// Sets g_isAutomaticGameplayRecording if the current program has automatic gameplay recording enabled in its NACP.
//Gets the control.nacp for the current program id, and then sets g_isAutomaticGameplayRecording if less memory should be allocated.
static void getIsAutomaticGameplayRecording(void)
{
    Result rc;

    // Do nothing if the HOS version predates [4.0.0], or we're not an application.
    if (hosversionBefore(4,0,0) || !g_isApplication)
        return;

    // Retrieve our process' Program ID
    u64 cur_progid=0;
    rc = svcGetInfo(&cur_progid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc)) diagAbortWithResult(rc); // shouldn't happen

    // Try reading our NACP
    rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        NsApplicationControlData data; // note: this is 144KB, which still fits comfortably within the 1MB of stack we have
        u64 size=0;
        rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, cur_progid, &data, sizeof(data), &size);
        nsExit();

        if (R_SUCCEEDED(rc) && data.nacp.video_capture == 2)
            g_isAutomaticGameplayRecording = 1;
    }
}

static void getOwnProcessHandle(void)
{
    Result rc;

    Handle server_handle, client_handle;
    rc = svcCreateSession(&server_handle, &client_handle, 0, 0);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 12));

    Thread t;
    rc = threadCreate(&t, &procHandleReceiveThread, (void*)(uintptr_t)server_handle, NULL, 0x1000, 0x20, 0);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 10));

    rc = threadStart(&t);
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 13));

    hipcMakeRequestInline(armGetTls(),
        .num_copy_handles = 1,
    ).copy_handles[0] = CUR_PROCESS_HANDLE;

    svcSendSyncRequest(client_handle);
    svcCloseHandle(client_handle);

    threadWaitForExit(&t);
    threadClose(&t);
}

static bool isKernel5xOrLater(void)
{
    u64 dummy = 0;
    Result rc = svcGetInfo(&dummy, InfoType_UserExceptionContextAddress, INVALID_HANDLE, 0);
    return R_VALUE(rc) != KERNELRESULT(InvalidEnumValue);
}

static bool isKernel4x(void)
{
    u64 dummy = 0;
    Result rc = svcGetInfo(&dummy, InfoType_InitialProcessIdRange, INVALID_HANDLE, 0);
    return R_VALUE(rc) != KERNELRESULT(InvalidEnumValue);
}

static void getCodeMemoryCapability(void)
{
    if (detectMesosphere()) {
        // Mesosphere allows for same-process code memory usage.
        g_codeMemoryCapability = CodeMemorySameProcess;
    } else if (isKernel5xOrLater()) {
        // On [5.0.0+], the kernel does not allow the creator process of a CodeMemory object
        // to use svcControlCodeMemory on itself, thus returning InvalidMemoryState (0xD401).
        // However the kernel can be patched to support same-process usage of CodeMemory.
        // We can detect that by passing a bad operation and observe if we actually get InvalidEnumValue (0xF001).
        Handle code;
        Result rc = svcCreateCodeMemory(&code, g_heapAddr, 0x1000);
        if (R_SUCCEEDED(rc)) {
            rc = svcControlCodeMemory(code, (CodeMapOperation)-1, 0, 0x1000, 0);
            svcCloseHandle(code);

            if (R_VALUE(rc) == KERNELRESULT(InvalidEnumValue))
                g_codeMemoryCapability = CodeMemorySameProcess;
            else
                g_codeMemoryCapability = CodeMemoryForeignProcess;
        }
    } else if (isKernel4x()) {
        // On [4.0.0-4.1.0] there is no such restriction on same-process CodeMemory usage.
        g_codeMemoryCapability = CodeMemorySameProcess;
    } else {
        // This kernel is too old to support CodeMemory syscalls.
        g_codeMemoryCapability = CodeMemoryUnavailable;
    }
}

// DeskNX fork: a line per load decision. The daemon can only see that this
// process exited, not why - whether the NRO it was told to chain to was never
// named, or was named and could not be opened. Those need telling apart from
// off-console, so they get written down. Assumes the SD is already mounted.
static void HbLog(const char *fmt, ...)
{
    FILE *fp = fopen(HB_LOG_FILE, "a");
    if (!fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

// A path with no "device:" prefix is resolved against whatever newlib has as
// the default device, which is not something this loader sets up. hbmenu always
// passed fully qualified "sdmc:/..." paths so it never came up; Sphaira hands
// over bare "/switch/foo.nro" (see its own playlog.ini), and an open() of that
// is one unlucky default away from failing - which in target mode ends the
// process and drops the user back in the menu.
static void qualifyPath(char *path, size_t cap)
{
    if (!path[0] || path[0] != '/') return;      // already qualified, or empty
    char tmp[FS_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "sdmc:%s", path);
    snprintf(path, cap, "%s", tmp);
}

void loadNro(void)
{
    NroHeader* header = NULL;
    size_t rw_size=0;
    Result rc=0;

    memcpy((u8*)armGetTls() + 0x100, g_savedTls, 0x100);

    if (g_nroSize > 0)
    {
        // Unmap previous NRO.
        header = &g_nroHeader;
        rw_size = header->segments[2].size + header->bss_size;
        rw_size = (rw_size+0xFFF) & ~0xFFF;

        svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PreUnloadDll, g_nroAddr, g_nroSize);

        // .text
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[0].file_off, ((u64) g_heapAddr) + header->segments[0].file_off, header->segments[0].size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 24));

        // .rodata
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[1].file_off, ((u64) g_heapAddr) + header->segments[1].file_off, header->segments[1].size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 25));

       // .data + .bss
        rc = svcUnmapProcessCodeMemory(
            g_procHandle, g_nroAddr + header->segments[2].file_off, ((u64) g_heapAddr) + header->segments[2].file_off, rw_size);

        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 26));

        svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PostUnloadDll, g_nroAddr, g_nroSize);

        g_nroAddr = g_nroSize = 0;
    }

    // DeskNX fork: on the very first load, honor TARGET_FILE if present. On
    // every later load (the running NRO exited), exit the process when we were
    // in target mode and the NRO did not chainload into another one -- that is
    // what hands control back to DeskNX instead of relaunching the homebrew.
    static bool s_firstLoad = true;
    if (s_firstLoad)
    {
        s_firstLoad = false;
        if (R_SUCCEEDED(fsdevMountSdmc()))
        {
            int tfd = open(TARGET_FILE, O_RDONLY);
            if (tfd >= 0)
            {
                char tbuf[sizeof g_nextNroPath + sizeof g_nextArgv + 2];
                ssize_t tlen = read(tfd, tbuf, sizeof(tbuf)-1);
                close(tfd);
                if (tlen > 0)
                {
                    tbuf[tlen] = '\0';
                    // First line is the .nro. A second line, when DeskNX system backend wrote
                    // one, is the argv to hand it - that is how arguments
                    // survive a launch made from the menu or from a queued
                    // request, instead of every .nro being told only its own
                    // path.
                    char *nl = strpbrk(tbuf, "\r\n");
                    char *targ = NULL;
                    if (nl)
                    {
                        *nl = '\0';
                        targ = nl + 1;
                        while (*targ == '\r' || *targ == '\n') targ++;
                        targ[strcspn(targ, "\r\n")] = '\0';
                        if (!targ[0]) targ = NULL;
                    }
                    if (tbuf[0])
                    {
                        g_targetMode = true;
                        strncpy(g_nextNroPath, tbuf, sizeof(g_nextNroPath)-1);
                        g_nextNroPath[sizeof(g_nextNroPath)-1] = '\0';
                        if (targ)
                            snprintf(g_nextArgv, sizeof(g_nextArgv), "%s", targ);
                        else
                            // Same argv convention hbmenu uses: quoted self-path.
                            snprintf(g_nextArgv, sizeof(g_nextArgv), "\"%s\"", tbuf);
                    }
                }
            }
            fsdevUnmountAll();
        }
    }
    else
    {
        if (R_SUCCEEDED(fsdevMountSdmc()))
        {
            qualifyPath(g_nextNroPath, sizeof(g_nextNroPath));
            HbLog("reload: app=%d target=%d next=\"%s\" argv=\"%s\"",
                  (int)g_isApplication, (int)g_targetMode,
                  g_nextNroPath, g_nextArgv);
            fsdevUnmountAll();
        }

        if (g_targetMode && g_nextNroPath[0] == '\0')
        {
            // Target NRO chain is done; terminate so DeskNX system backend sees the applet
            // (or the donor application) finish and brings the menu back.
            svcExitProcess();
        }

    }

    if (g_nextNroPath[0] == '\0')
    {
        memcpy(g_nextNroPath, DEFAULT_NRO, sizeof(DEFAULT_NRO));
        memcpy(g_nextArgv,    DEFAULT_NRO, sizeof(DEFAULT_NRO));
    }

    memcpy(g_argv, g_nextArgv, sizeof g_argv);

    svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PreLoadDll, (uintptr_t)g_argv, sizeof(g_argv));

    uint8_t *nrobuf = (uint8_t*) g_heapAddr;

    NroStart*  start  = (NroStart*)  (nrobuf + 0);
    header = (NroHeader*) (nrobuf + sizeof(NroStart));
    uint8_t*   rest   = (uint8_t*)   (nrobuf + sizeof(NroStart) + sizeof(NroHeader));

    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 404));

    int fd = open(g_nextNroPath, O_RDONLY);
    if (fd < 0)
    {
        HbLog("open failed: \"%s\" (errno %d) - returning to the menu",
              g_nextNroPath, errno);
        // DeskNX fork: a missing target NRO just hands control back to the
        // menu instead of hard-aborting the applet with a crash dialog.
        if (g_targetMode)
            svcExitProcess();
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 3));
    }

    // Reset NRO path to load hbmenu by default next time.
    g_nextNroPath[0] = '\0';

    struct stat nro_stat;
    if (fstat(fd, &nro_stat) != 0 || nro_stat.st_size < 0)
    {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: could not stat file");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 41));
    }

    if (read(fd, start, sizeof(*start)) != sizeof(*start))
    {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: truncated start header");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 4));
    }

    if (read(fd, header, sizeof(*header)) != sizeof(*header))
    {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: truncated header");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 4));
    }

    if (header->magic != NROHEADER_MAGIC)
    {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: bad magic");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 5));
    }

    // Validate every size/offset before reading into the fixed applet heap.
    // NROs come from removable storage, so malformed headers must never make
    // rest_size underflow or let a segment extend beyond the mapped image.
    const u64 header_min = sizeof(NroStart) + sizeof(NroHeader);
    const u64 image_size = header->size;
    const u64 bss_size = header->bss_size;
    bool invalid_image = image_size < header_min ||
                         image_size > (u64)nro_stat.st_size ||
                         image_size > (u64)g_heapSize ||
                         bss_size > (u64)g_heapSize - image_size;
    for (int i = 0; i < 3 && !invalid_image; ++i) {
        const u64 segment_start = header->segments[i].file_off;
        const u64 segment_size = header->segments[i].size;
        if (segment_start >= image_size || segment_size > image_size ||
            segment_start + segment_size > image_size)
            invalid_image = true;
    }
    if (invalid_image) {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: size, BSS, or segment range is out of bounds");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 42));
    }

    size_t rest_size = (size_t)(image_size - header_min);
    if (read(fd, rest, rest_size) != (ssize_t)rest_size)
    {
        close(fd);
        if (g_targetMode) {
            HbLog("invalid target NRO: truncated image data");
            svcExitProcess();
        }
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 7));
    }

    close(fd);
    fsdevUnmountAll();

    size_t total_size = (size_t)(image_size + bss_size);
    total_size = (total_size + 0xFFF) & ~(size_t)0xFFF;

    rw_size = (size_t)header->segments[2].size + (size_t)bss_size;
    rw_size = (rw_size + 0xFFF) & ~(size_t)0xFFF;

    // todo: Detect whether NRO fits into heap or not.

    // Copy header to elsewhere because we're going to unmap it next.
    memcpy(&g_nroHeader, header, sizeof(g_nroHeader));
    header = &g_nroHeader;

    // Map code memory to a new randomized address
    virtmemLock();
    void* map_addr = virtmemFindCodeMemory(total_size, 0);
    rc = svcMapProcessCodeMemory(g_procHandle, (u64)map_addr, (u64)nrobuf, total_size);
    virtmemUnlock();

    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 18));

    // .text
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[0].file_off, header->segments[0].size, Perm_R | Perm_X);

    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 19));

    // .rodata
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[1].file_off, header->segments[1].size, Perm_R);

    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 20));

    // .data + .bss
    rc = svcSetProcessMemoryPermission(
        g_procHandle, (u64)map_addr + header->segments[2].file_off, rw_size, Perm_Rw);

    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 21));

    u64 nro_size = header->segments[2].file_off + rw_size;
    u64 nro_heap_start = ((u64) g_heapAddr) + nro_size;
    u64 nro_heap_size  = g_heapSize + (u64) g_heapAddr - (u64) nro_heap_start;

    #define M EntryFlag_IsMandatory

    static ConfigEntry entries[] = {
        { EntryType_MainThreadHandle,     0, {0, 0} },
        { EntryType_ProcessHandle,        0, {0, 0} },
        { EntryType_AppletType,           0, {AppletType_LibraryApplet, 0} },
        { EntryType_OverrideHeap,         M, {0, 0} },
        { EntryType_Argv,                 0, {0, 0} },
        { EntryType_NextLoadPath,         0, {0, 0} },
        { EntryType_LastLoadResult,       0, {0, 0} },
        { EntryType_SyscallAvailableHint, 0, {UINT64_MAX, UINT64_MAX} },
        { EntryType_SyscallAvailableHint2, 0, {UINT64_MAX, 0} },
        { EntryType_RandomSeed,           0, {0, 0} },
        { EntryType_UserIdStorage,        0, {(u64)(uintptr_t)&g_userIdStorage, 0} },
        { EntryType_HosVersion,           0, {0, 0} },
        { EntryType_EndOfList,            0, {(u64)(uintptr_t)g_noticeText, sizeof(g_noticeText)} }
    };

    ConfigEntry *entry_AppletType = &entries[2];
    ConfigEntry *entry_Syscalls   = &entries[7];

    if (g_isApplication) {
        entry_AppletType->Value[0] = AppletType_SystemApplication;
        entry_AppletType->Value[1] = EnvAppletFlags_ApplicationOverride;
    }

    if (!(g_codeMemoryCapability & BIT(0))) {
        // Revoke access to svcCreateCodeMemory if it's not available.
        entry_Syscalls->Value[0x4B/64] &= ~(1UL << (0x4B%64));
    }

    if (!(g_codeMemoryCapability & BIT(1))) {
        // Revoke access to svcControlCodeMemory if it's not available for same-process usage.
        entry_Syscalls->Value[0x4C/64] &= ~(1UL << (0x4C%64)); // svcControlCodeMemory
    }

    // MainThreadHandle
    entries[0].Value[0] = envGetMainThreadHandle();
    // ProcessHandle
    entries[1].Value[0] = g_procHandle;
    // OverrideHeap
    entries[3].Value[0] = nro_heap_start;
    entries[3].Value[1] = nro_heap_size;
    // Argv
    entries[4].Value[1] = (u64)(uintptr_t)&g_argv[0];
    // NextLoadPath
    entries[5].Value[0] = (u64)(uintptr_t)&g_nextNroPath[0];
    entries[5].Value[1] = (u64)(uintptr_t)&g_nextArgv[0];
    // LastLoadResult
    entries[6].Value[0] = g_lastRet;
    // RandomSeed
    entries[9].Value[0] = randomGet64();
    entries[9].Value[1] = randomGet64();
    // HosVersion
    entries[11].Value[0] = hosversionGet();
    entries[11].Value[1] = hosversionIsAtmosphere() ? 0x41544d4f53504852UL : 0; // 'ATMOSPHR'

    g_nroAddr = (u64)map_addr;
    g_nroSize = nro_size;

    svcBreak(BreakReason_NotificationOnlyFlag | BreakReason_PostLoadDll, g_nroAddr, g_nroSize);

    nroEntrypointTrampoline(&entries[0], -1, g_nroAddr);
}

int main(int argc, char **argv)
{
    memcpy(g_savedTls, (u8*)armGetTls() + 0x100, 0x100);

    getIsApplication();
    getIsAutomaticGameplayRecording();
    smExit(); // Close SM as we don't need it anymore.
    setupHbHeap();
    getOwnProcessHandle();
    getCodeMemoryCapability();
    loadNro();

    diagAbortWithResult(MAKERESULT(Module_HomebrewLoader, 8));
    return 0;
}
