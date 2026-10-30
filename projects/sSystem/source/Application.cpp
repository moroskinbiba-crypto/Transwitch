// Derived from sLaunch (GPL-3.0-or-later); see THIRD_PARTY.md.
#include <desknx/system/Application.hpp>
#include <desknx/Protocol.hpp>
#include <cstring>
#include <memory>

void DaemonLog(const char *fmt, ...);   // main.cpp

namespace desknx::system::app {

    AppletApplication g_AppHolder   = {};
    u64               g_AppId       = 0;
    bool              g_AppRunning  = false;
    bool              g_AppHasFocus = false;
    u64               g_ExitAppId   = 0;

    // Tick when the current app was launched, for the suspend-stabilization guard.
    static u64        g_LaunchTick  = 0;
    // pmshell is optional during qlaunch startup. A failed PID query must not
    // be interpreted as proof of process exit when that service was unavailable.
    static bool       g_ProcessMonitorAvailable = false;

    // Atmosphere's pm module (15), ProcessNotFound result (description 1).
    // No other PM error is proof that the application's process has exited.
    static bool IsApplicationProcessMissing(Result rc) {
        return R_MODULE(rc) == 15 && R_DESCRIPTION(rc) == 1;
    }

    // IApplicationAccessor's state-change event also fires on focus/suspend
    // transitions. Query its explicit IsCompleted command (AM cmd 1) before
    // Join/GetResult so a stale event can never be mistaken for app exit.
    static Result CheckApplicationCompleted(bool* out_completed) {
        if (!out_completed)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        u8 completed = 0;
        const Result rc = serviceDispatchOut(&g_AppHolder.s, 1, completed);
        if (R_SUCCEEDED(rc)) *out_completed = completed != 0;
        return rc;
    }

    static Result WaitForApplicationCompleted(u64 timeout_ns) {
        const u64 start = armGetSystemTick();
        const u64 frequency = armGetSystemTickFreq();
        const u64 timeout_ticks = (timeout_ns / 1'000'000'000ULL) * frequency +
            ((timeout_ns % 1'000'000'000ULL) * frequency) / 1'000'000'000ULL;
        const u64 deadline = start + timeout_ticks;

        do {
            bool completed = false;
            const Result rc = CheckApplicationCompleted(&completed);
            if (R_FAILED(rc)) return rc;
            if (completed) return 0;
            if (armGetSystemTick() >= deadline) break;
            svcSleepThread(50'000'000ULL);
        } while (true);

        return KERNELRESULT(TimedOut);
    }

    // Build the 0x88-byte preselected-user launch parameter expected by games.
    // Layout (from qlaunch / uLaunch): magic(4) + is_user_selected(1) + pad(3) +
    // uid(16) + padding. The is_user_selected byte MUST be 1 -- leaving it 0
    // makes the game think no user was preselected and it exits right after the
    // "Licensed by Nintendo" splash.
    static Result PushUserParam(AccountUid user) {
        struct alignas(4) UserParam {
            u32        magic;             // 0xC79497CA
            u8         is_user_selected;  // 1
            u8         pad[3];
            AccountUid uid;
            u8         unused[0x70];
        } param = {};
        static_assert(sizeof(param) == 0x88);
        param.magic            = 0xC79497CA;
        param.is_user_selected = 1;
        param.uid              = user;

        AppletStorage st;
        Result rc = appletCreateStorage(&st, sizeof(param));
        if (rc != 0) return rc;
        rc = appletStorageWrite(&st, 0, &param, sizeof(param));
        if (rc != 0) { appletStorageClose(&st); return rc; }
        rc = appletApplicationPushLaunchParameter(
            &g_AppHolder, AppletLaunchParameterKind_PreselectedUser, &st);
        appletStorageClose(&st);
        return rc;
    }

    // Stock qlaunch creates a game's save data before launching it, and games
    // rely on that: they mount their save without creating it, and abort with
    // 2002-1002 (target not found) when it is missing - which is every game
    // never played on that account before (issue #6). Same as qlaunch / uLaunch:
    // open it to see if it exists, create it from the NACP sizes if not.
    static Result EnsureSave(u64 app_id, u64 owner, AccountUid uid, FsSaveDataType type,
                           FsSaveDataSpaceId space, u64 size, u64 journal) {
        if (size == 0) return 0;
        const FsSaveDataAttribute attr = {
            .application_id = app_id, .uid = uid, .system_save_data_id = 0,
            .save_data_type = (u8)type, .save_data_rank = FsSaveDataRank_Primary,
            .save_data_index = 0,
        };
        FsFileSystem fs;
        const Result orc = fsOpenSaveDataFileSystem(&fs, space, &attr);
        if (R_SUCCEEDED(orc)) { fsFsClose(&fs); return 0; }
        const FsSaveDataCreationInfo info = {
            .save_data_size = (s64)size, .journal_size = (s64)journal,
            .available_size = 0x4000, .owner_id = owner, .flags = 0,
            .save_data_space_id = (u8)space,
        };
        const FsSaveDataMetaInfo meta = {
            .size = (type == FsSaveDataType_Bcat) ? 0u : 0x40060u,
            .type = (u8)((type == FsSaveDataType_Bcat) ? FsSaveDataMetaType_None
                                                       : FsSaveDataMetaType_Thumbnail),
        };
        const Result crc = fsCreateSaveDataFileSystem(&attr, &info, &meta);
        if (R_FAILED(crc)) {
            // The open may have raced another creator (for example, a save
            // created while the HOME transition was in progress). Only treat
            // the create failure as fatal if the save is still not mountable.
            FsFileSystem retryFs;
            const Result retryOpenRc = fsOpenSaveDataFileSystem(&retryFs, space, &attr);
            if (R_SUCCEEDED(retryOpenRc)) {
                fsFsClose(&retryFs);
                DaemonLog("save: app=0x%016lx type=%d appeared during create (create rc=0x%x)",
                          app_id, (int)type, crc);
                return 0;
            }
            DaemonLog("save: app=0x%016lx type=%d create failed rc=0x%x; retry open rc=0x%x",
                      app_id, (int)type, crc, retryOpenRc);
        } else {
            DaemonLog("save: app=0x%016lx type=%d size=0x%lx created after open rc=0x%x",
                      app_id, (int)type, size, orc);
        }
        return crc;
    }

    // fs 2002-0030..0045: one of the "not enough free space" results.
    bool IsNoSpace(Result rc) {
        return R_MODULE(rc) == 2 /* fs */ && R_DESCRIPTION(rc) >= 30 && R_DESCRIPTION(rc) <= 45;
    }

    // The first creation that failed, or 0.
    // `wants_user`: the NACP's StartupUserAccount is set. A game that has it
    // at None picks its players itself; handing it a preselected user anyway
    // opens that account for it, and picking the same one in the game's own
    // selector then loops. Unknown (no NACP) keeps the old behaviour.
    static Result EnsureSaves(u64 app_id, AccountUid user, bool &wants_user) {
        wants_user = true;
        auto ctl = std::make_unique<NsApplicationControlData>();
        u64 got = 0;
        const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id,
                                                      ctl.get(), sizeof(*ctl), &got);
        DaemonLog("save: app=0x%016lx user=%016lx%016lx control rc=0x%x", app_id,
                  user.uid[0], user.uid[1], rc);
        if (R_FAILED(rc)) return 0;   // no NACP: launch as before
        const NacpStruct &n = ctl->nacp;
        wants_user = n.startup_user_account != 0;
        DaemonLog("save: nacp user=%d account=0x%lx device=0x%lx temp=0x%lx cache=0x%lx bcat=0x%lx",
                  n.startup_user_account, n.user_account_save_data_size, n.device_save_data_size,
                  n.temporary_storage_size, n.cache_storage_size,
                  n.bcat_delivery_cache_storage_size);
        const u64 owner = n.save_data_owner_id;
        // Every save is still tried after one fails; the first failure decides.
        Result first = 0;
        auto keep = [&](Result r) { if (R_FAILED(r) && !first) first = r; };
        if (wants_user && accountUidIsValid(&user))
            keep(EnsureSave(app_id, owner, user, FsSaveDataType_Account, FsSaveDataSpaceId_User,
                       n.user_account_save_data_size, n.user_account_save_data_journal_size));
        keep(EnsureSave(app_id, owner, {}, FsSaveDataType_Device, FsSaveDataSpaceId_User,
                   n.device_save_data_size, n.device_save_data_journal_size));
        keep(EnsureSave(app_id, owner, {}, FsSaveDataType_Temporary, FsSaveDataSpaceId_Temporary,
                   n.temporary_storage_size, 0));
        keep(EnsureSave(app_id, owner, {}, FsSaveDataType_Cache, FsSaveDataSpaceId_User,
                   n.cache_storage_size, n.cache_storage_journal_size));
        keep(EnsureSave(app_id, 0x010000000000000CULL, {}, FsSaveDataType_Bcat, FsSaveDataSpaceId_User,
                   n.bcat_delivery_cache_storage_size, 0x200000));
        return first;
    }

    bool WantsUser(u64 app_id) {
        auto ctl = std::make_unique<NsApplicationControlData>();
        u64 got = 0;
        if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id,
                                                 ctl.get(), sizeof(*ctl), &got)))
            return true;   // unknown: behave as before
        return ctl->nacp.startup_user_account != 0;
    }

    Result Launch(u64 app_id, AccountUid user, u64 flags) {
        if (g_AppRunning) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        g_ExitAppId = 0;

        // Touch the app (marks as recently used in NS)
        nsTouchApplication(app_id);
        // Launching anyway would only start a game that aborts on its save
        // mount; the caller tells the user why instead.
        bool wants_user = true;
        const Result saveRc = EnsureSaves(app_id, user, wants_user);
        if (R_FAILED(saveRc)) {
            if (IsNoSpace(saveRc))
                DaemonLog("save: refusing launch of 0x%016lx because save data needs more free space",
                          app_id);
            else
                DaemonLog("save: refusing launch of 0x%016lx because save data is unavailable rc=0x%x",
                          app_id, saveRc);
            return saveRc;
        }
        // LNC-02: the menu can override the NACP verdict via the v2 flags -
        // NoPreuser means "the game picks players itself", so the preselected
        // user parameter must not be sent even when NACP asks for an account.
        if (flags & desknx::ipc::LaunchFlagNoPreuser) wants_user = false;
        else if (flags & desknx::ipc::LaunchFlagHasUser) wants_user = true;

        Result rc = appletCreateApplication(&g_AppHolder, app_id);
        if (rc != 0) return rc;

        rc = wants_user ? PushUserParam(user) : 0;
        if (rc == 0) {
            // Release foreground so the app can acquire it. Do not start
            // the title if AM refused the handoff.
            rc = appletUnlockForeground();
            if (R_SUCCEEDED(rc))
                rc = appletApplicationStart(&g_AppHolder);
        }
        // Otherwise the failed app's holder stays open: am keeps it as the
        // application, and the next launch is refused.
        if (rc != 0) {
            appletApplicationClose(&g_AppHolder);
            memset(&g_AppHolder, 0, sizeof(g_AppHolder));
            appletRequestToGetForeground();
            return rc;
        }

        // Publish the live holder before requesting foreground so a failed
        // handoff can be rolled back through the same verified termination
        // path used when the user selects a different title.
        g_AppId       = app_id;
        g_AppRunning  = true;
        g_AppHasFocus = false;
        g_LaunchTick  = armGetSystemTick();

        rc = appletApplicationRequestForApplicationToGetForeground(&g_AppHolder);
        if (R_FAILED(rc)) {
            DaemonLog("app: foreground request failed after launch rc=0x%08X",
                      static_cast<unsigned int>(rc));
            const Result terminateRc = Terminate();
            if (R_FAILED(terminateRc)) {
                const Result systemRc = FocusSystem();
                DaemonLog("app: launch rollback incomplete (focus=0x%08X terminate=0x%08X system=0x%08X); retaining holder",
                          static_cast<unsigned int>(rc),
                          static_cast<unsigned int>(terminateRc),
                          static_cast<unsigned int>(systemRc));
                return terminateRc;
            }
            DaemonLog("app: launch rolled back after foreground handoff failure");
            return rc;
        }

        g_AppHasFocus = true;
        return 0;
    }

    bool CanSuspend() {
        if (!g_AppRunning) return false;
        // Give the app ~1.5s to finish launching and take the foreground before a
        // HOME press is allowed to suspend it (otherwise am faults).
        const u64 settle = armGetSystemTickFreq() * 3 / 2;
        return (armGetSystemTick() - g_LaunchTick) >= settle;
    }

    Result Resume() {
        if (!g_AppRunning) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        return FocusApplication();
    }

    void SetProcessMonitorAvailable(bool available) {
        g_ProcessMonitorAvailable = available;
    }

    Result Terminate() {
        if (!g_AppRunning) return 0;
        g_ExitAppId = 0;

        appletApplicationTerminateAllLibraryApplets(&g_AppHolder);
        const Result requestRc = appletApplicationRequestExit(&g_AppHolder);
        if (R_FAILED(requestRc)) {
            DaemonLog("app: graceful exit request rc=0x%08X",
                      static_cast<unsigned int>(requestRc));
        }

        // StateChangedEvent is not proof of process exit: it also fires on
        // suspend and can remain signalled while the title is still alive.
        // Only use the PID service after its initialization succeeded.
        if (g_ProcessMonitorAvailable) {
            // Atmosphere's pm:shell returns pm::ResultProcessNotFound only
            // when there is no application process. Other PM errors are not
            // evidence of exit and must not cause us to discard the holder.
            auto queryProcess = [](u64 *pid) {
                return pmshellGetApplicationProcessIdForShell(pid);
            };
            const u64 hz = armGetSystemTickFreq();
            bool processMissing = false;
            u64 deadline = armGetSystemTick() + hz * 15;

            while (armGetSystemTick() < deadline) {
                u64 pid = 0;
                const Result queryRc = queryProcess(&pid);
                if (R_SUCCEEDED(queryRc)) {
                    if (pid == 0) {
                        DaemonLog("app: pm:shell returned an invalid zero PID; keeping application holder");
                        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    }
                    svcSleepThread(50'000'000ULL);
                    continue;
                }
                if (IsApplicationProcessMissing(queryRc)) {
                    processMissing = true;
                    break;
                }
                DaemonLog("app: pm:shell process query failed rc=0x%08X; keeping application holder",
                          static_cast<unsigned int>(queryRc));
                return queryRc;
            }

            if (!processMissing) {
                DaemonLog("app: 0x%016lx ignored the exit request - terminating", g_AppId);
                const Result terminateRc = appletApplicationTerminate(&g_AppHolder);
                if (R_FAILED(terminateRc)) {
                    DaemonLog("app: forced termination failed rc=0x%08X; keeping application holder",
                              static_cast<unsigned int>(terminateRc));
                    return terminateRc;
                }

                deadline = armGetSystemTick() + hz * 3;
                while (armGetSystemTick() < deadline) {
                    u64 pid = 0;
                    const Result queryRc = queryProcess(&pid);
                    if (R_SUCCEEDED(queryRc)) {
                        if (pid == 0) {
                            DaemonLog("app: pm:shell returned an invalid zero PID after termination; keeping holder");
                            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                        }
                        svcSleepThread(50'000'000ULL);
                        continue;
                    }
                    if (IsApplicationProcessMissing(queryRc)) {
                        processMissing = true;
                        break;
                    }
                    DaemonLog("app: pm:shell verification failed rc=0x%08X after termination; keeping holder",
                              static_cast<unsigned int>(queryRc));
                    return queryRc;
                }
                if (!processMissing) {
                    DaemonLog("app: process 0x%016lx is still reported alive after termination; keeping holder",
                              g_AppId);
                    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                }
            }
        } else {
            // Without pmshell, a failed PID query is ambiguous. Give the title
            // a short opportunity to handle RequestExit, then ask AM to
            // terminate it explicitly. Never close the holder on a guessed exit.
            DaemonLog("app: pmshell unavailable; using direct AM termination for title=0x%016lx",
                      g_AppId);
            svcSleepThread(500'000'000ULL);
            const Result terminateRc = appletApplicationTerminate(&g_AppHolder);
            if (R_FAILED(terminateRc)) {
                DaemonLog("app: direct AM termination failed rc=0x%08X; keeping application holder",
                          static_cast<unsigned int>(terminateRc));
                return terminateRc;
            }
        }

        // RequestExit/Terminate only asks AM to stop the title. The state
        // event may already be signalled by a focus transition, so verify the
        // explicit IsCompleted result before Join consumes GetResult.
        DaemonLog("app: waiting for AM application completion");
        const Result completedRc = WaitForApplicationCompleted(5'000'000'000ULL);
        if (R_FAILED(completedRc)) {
            DaemonLog("app: AM did not confirm completion rc=0x%08X; keeping holder",
                      static_cast<unsigned int>(completedRc));
            return completedRc;
        }
        appletApplicationJoin(&g_AppHolder);
        DaemonLog("app: AM application-exit result received");

        appletApplicationClose(&g_AppHolder);
        memset(&g_AppHolder, 0, sizeof(g_AppHolder));
        g_AppId       = 0;
        g_AppRunning  = false;
        g_AppHasFocus = false;

        const Result foregroundRc = appletRequestToGetForeground();
        if (R_FAILED(foregroundRc))
            DaemonLog("app: request foreground after termination rc=0x%08X",
                      static_cast<unsigned int>(foregroundRc));
        return 0;
    }

    bool Update(bool request_foreground) {
        if (!g_AppRunning) return false;

        // IApplicationAccessor's StateChangedEvent is not an exit event: it
        // can be signalled by focus/suspend transitions and remains signalled.
        // Use pm:shell's application-process query as the authoritative exit
        // signal. If the optional service is unavailable, preserve the holder
        // rather than mistaking HOME suspension for a natural game exit.
        if (!g_ProcessMonitorAvailable)
            return false;

        // Let AM/PM finish publishing the newly started application before
        // interpreting ProcessNotFound as a natural exit. This matches the
        // HOME-suspend stabilization window used by CanSuspend().
        const u64 now = armGetSystemTick();
        const u64 launchSettleTicks = armGetSystemTickFreq() * 3 / 2;
        if (now - g_LaunchTick < launchSettleTicks)
            return false;

        u64 pid = 0;
        const Result queryRc = pmshellGetApplicationProcessIdForShell(&pid);
        if (R_SUCCEEDED(queryRc)) {
            if (pid == 0)
                DaemonLog("app: pm:shell returned zero PID during exit poll; keeping application holder");
            return false;
        }
        if (!IsApplicationProcessMissing(queryRc)) {
            // PM can briefly fail while services are transitioning. A
            // transport/permission error must never close a live holder.
            static u64 lastQueryErrorLogTick = 0;
            const u64 now = armGetSystemTick();
            const u64 logInterval = armGetSystemTickFreq() * 5;
            if (lastQueryErrorLogTick == 0 || now - lastQueryErrorLogTick >= logInterval) {
                DaemonLog("app: natural-exit PM query failed rc=0x%08X; retaining application holder",
                          static_cast<unsigned int>(queryRc));
                lastQueryErrorLogTick = now;
            }
            return false;
        }

        // PM confirms that the process is gone, but wait for AM's own
        // IsCompleted state before consuming GetResult and releasing the
        // accessor. If AM is still finalizing, retain the holder and retry on
        // the next backend poll instead of blocking the qlaunch event loop.
        bool amCompleted = false;
        const Result completedRc = CheckApplicationCompleted(&amCompleted);
        if (R_FAILED(completedRc) || !amCompleted) {
            static u64 lastAmWaitLogTick = 0;
            const u64 now = armGetSystemTick();
            const u64 logInterval = armGetSystemTickFreq() * 5;
            if (lastAmWaitLogTick == 0 || now - lastAmWaitLogTick >= logInterval) {
                DaemonLog("app: PM reports process gone but AM completion is pending (rc=0x%08X completed=%d); retaining holder",
                          static_cast<unsigned int>(completedRc), amCompleted ? 1 : 0);
                lastAmWaitLogTick = now;
            }
            return false;
        }

        g_ExitAppId = g_AppId;
        appletApplicationJoin(&g_AppHolder);
        appletApplicationClose(&g_AppHolder);
        memset(&g_AppHolder, 0, sizeof(g_AppHolder));
        g_AppId       = 0;
        g_AppRunning  = false;
        g_AppHasFocus = false;

        if (request_foreground) {
            const Result foregroundRc = appletRequestToGetForeground();
            if (R_FAILED(foregroundRc))
                DaemonLog("app: request foreground after natural exit rc=0x%08X",
                          static_cast<unsigned int>(foregroundRc));
        }
        return true;
    }

    Result FocusApplication() {
        if (!g_AppRunning) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        const Result rc = appletApplicationRequestForApplicationToGetForeground(&g_AppHolder);
        if (R_SUCCEEDED(rc)) g_AppHasFocus = true;
        return rc;
    }

    Result FocusSystem() {
        const Result rc = appletRequestToGetForeground();
        if (R_SUCCEEDED(rc)) g_AppHasFocus = false;
        return rc;
    }

} // namespace desknx::system::app
