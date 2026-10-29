// Adapted from sLaunch's ECS implementation (GPL-3.0-or-later); see THIRD_PARTY.md.
#include <desknx/system/ExternalContent.hpp>
#include <stratosphere.hpp>
#include <stratosphere/fssrv/interface_adapters/fssrv_filesystem_interface_adapter.hpp>
#include <stratosphere/fssystem/fssystem_subdirectory_filesystem.hpp>
#include <stratosphere/fs/fs_remote_filesystem.hpp>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <sys/stat.h>

// ECS implementation adapted from uLaunch's ul::system::ecs / sf::IpcManager
// (https://github.com/Xortroll/uLaunch, GPLv2, (C) Xortroll & contributors).

namespace desknx::system::ecs {

    namespace {

        // ---- sf server manager (serves ECS filesystem sessions) --------------
        struct ServerOptions {
            static constexpr size_t PointerBufferSize   = 0x800;
            static constexpr size_t MaxDomains          = 0x40;
            static constexpr size_t MaxDomainObjects    = 0x100;
            static constexpr bool   CanDeferInvokeRequest = false;
            static constexpr bool   CanManageMitmServers  = false;
        };
        // One session per registered program id, and the menu registers every
        // id its slot might resolve to on each launch, so leave headroom.
        constexpr size_t MaxEcsSessions = 12;

        struct RegisteredContent {
            u64 program_id;
            char exefs_path[128];
            bool active;
        };
        RegisteredContent g_RegisteredContents[MaxEcsSessions]{};

        // No named ports: sessions are added manually via RegisterSession.
        class EcsServerManager : public ams::sf::hipc::ServerManager<0, ServerOptions, MaxEcsSessions> {
            private:
                ams::Result OnNeedsToAccept(int, Server *) override {
                    return ams::ResultSuccess();
                }
        };

        EcsServerManager    g_Manager;
        ams::os::ThreadType g_ManagerThread;
        alignas(ams::os::ThreadStackAlignment) u8 g_ManagerThreadStack[0x8000];

        using Allocator     = ams::sf::ExpHeapAllocator;
        using ObjectFactory = ams::sf::ObjectFactory<ams::sf::ExpHeapAllocator::Policy>;

        ams::os::SdkMutex        g_AllocatorLock;
        alignas(0x40) constinit u8 g_AllocatorHeap[0x8000];
        ams::lmem::HeapHandle    g_AllocatorHeapHandle;
        Allocator                g_Allocator;
        bool                     g_AllocatorReady = false;
        bool                     g_ServerUp = false; // main thread only
        bool                     g_ServerThreadPrepared = false; // main thread only
        std::atomic<bool>        g_ServerThreadExited{true}; // written by server thread

        // Separate file: this runs on the serve thread, so writing to daemon.log
        // would race with the main thread's writes.
        void EcsLog(const char *msg) {
            mkdir("sdmc:/switch", 0777);
            mkdir("sdmc:/switch/DeskNX", 0777);
            mkdir("sdmc:/switch/DeskNX/logs", 0777);
            // Rotation like daemon.log: the ECS thread is long-lived and a
            // chatty failure path could otherwise grow ecs.log without bound.
            struct stat st{};
            if (stat("sdmc:/switch/DeskNX/logs/ecs.log", &st) == 0 &&
                st.st_size > 256 * 1024) {
                remove("sdmc:/switch/DeskNX/logs/ecs.prev.log");
                rename("sdmc:/switch/DeskNX/logs/ecs.log",
                       "sdmc:/switch/DeskNX/logs/ecs.prev.log");
            }
            FILE *fp = fopen("sdmc:/switch/DeskNX/logs/ecs.log", "a");
            if (!fp) return;
            fprintf(fp, "%s\n", msg);
            fclose(fp);
        }

        void ManagerThreadFunc(void *) {
            EcsLog("server thread: entering LoopProcess");
            g_Manager.LoopProcess();
            // LoopProcess returns when its wait set becomes empty. Keep this
            // signal atomic: the main thread will join/destroy and recreate the
            // ended thread before relying on the ECS server again.
            g_ServerThreadExited.store(true, std::memory_order_release);
            EcsLog("server thread: LoopProcess returned; restart required on next registration");
        }

        template<typename Impl, typename Iface, typename ...Args>
        auto MakeSharedObject(Args &&...args) {
            std::scoped_lock lk(g_AllocatorLock);
            return ObjectFactory::CreateSharedEmplaced<Iface, Impl>(
                std::addressof(g_Allocator), std::forward<Args>(args)...);
        }

        // ldr:shel AtmosphereRegisterExternalCode (cmd 65000): returns a session
        // handle we serve the replacement exefs on.
        inline Result RegisterExternalCodeCmd(u64 program_id, Handle *out_h) {
            return serviceDispatchIn(ldrShellGetServiceSession(), 65000, program_id,
                .out_handle_attrs = { SfOutHandleAttr_HipcMove },
                .out_handles      = out_h,
            );
        }

        // ldr:shel AtmosphereUnregisterExternalCode (cmd 65001).
        inline Result UnregisterExternalCodeCmd(u64 program_id) {
            return serviceDispatchIn(ldrShellGetServiceSession(), 65001, program_id);
        }

    } // namespace

    // Allocator must be ready before we build served objects; the serve thread
    // is started only AFTER the first session is registered, so LoopProcess
    // never begins with an empty wait set (which would make it exit at once).
    Result InitializeServer() {
        if (g_AllocatorReady) return 0;
        g_AllocatorHeapHandle = ams::lmem::CreateExpHeap(
            g_AllocatorHeap, sizeof(g_AllocatorHeap), ams::lmem::CreateOption_None);
        g_Allocator.Attach(g_AllocatorHeapHandle);
        g_AllocatorReady = true;
        return 0;
    }

    // Create the thread before changing Atmosphere's loader mapping. A
    // creation failure therefore has no external side effects to roll back.
    // The thread is not started until RegisterSession has populated the wait set.
    static Result PrepareServerThread(bool *prepared_here) {
        if (prepared_here == nullptr)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        *prepared_here = false;

        if (g_ServerUp && !g_ServerThreadExited.load(std::memory_order_acquire))
            return 0;

        if (g_ServerUp) {
            // The thread has returned, but its handle and stack still belong to
            // this process. Join and destroy before reusing the ThreadType.
            ams::os::WaitThread(&g_ManagerThread);
            ams::os::DestroyThread(&g_ManagerThread);
            g_ServerUp = false;
        }

        if (g_ServerThreadPrepared) {
            *prepared_here = true;
            return 0;
        }

        const ams::Result rc = ams::os::CreateThread(
            &g_ManagerThread, &ManagerThreadFunc, nullptr,
            g_ManagerThreadStack, sizeof(g_ManagerThreadStack), 10);
        if (R_FAILED(rc)) {
            EcsLog("ECS server thread creation failed (rc=0x%08X)",
                   static_cast<unsigned int>(rc.GetValue()));
            return rc.GetValue();
        }
        g_ServerThreadPrepared = true;
        *prepared_here = true;
        return 0;
    }

    static void CancelPreparedServerThread(bool prepared_here) {
        if (!prepared_here || !g_ServerThreadPrepared) return;
        // This thread was created but never started; DestroyThread releases its
        // kernel object without waiting for a thread entry point that never ran.
        ams::os::DestroyThread(&g_ManagerThread);
        g_ServerThreadPrepared = false;
    }

    static Result StartServerThreadOnce() {
        if (g_ServerUp && !g_ServerThreadExited.load(std::memory_order_acquire))
            return 0;

        if (g_ServerUp) {
            ams::os::WaitThread(&g_ManagerThread);
            ams::os::DestroyThread(&g_ManagerThread);
            g_ServerUp = false;
        }

        if (!g_ServerThreadPrepared) {
            const ams::Result rc = ams::os::CreateThread(
                &g_ManagerThread, &ManagerThreadFunc, nullptr,
                g_ManagerThreadStack, sizeof(g_ManagerThreadStack), 10);
            if (R_FAILED(rc)) {
                EcsLog("ECS server restart failed (rc=0x%08X)",
                       static_cast<unsigned int>(rc.GetValue()));
                return rc.GetValue();
            }
            g_ServerThreadPrepared = true;
        }

        // Publish state before the thread starts, so a very fast return cannot
        // be overwritten by the caller after StartThread.
        g_ServerThreadExited.store(false, std::memory_order_release);
        g_ServerUp = true;
        g_ServerThreadPrepared = false;
        ams::os::StartThread(&g_ManagerThread);
        return 0;
    }

    Result RegisterExternalContent(u64 program_id, const char *exefs_path) {
        if (program_id == 0 || exefs_path == nullptr || exefs_path[0] != '/' ||
            std::strlen(exefs_path) >= sizeof(g_RegisteredContents[0].exefs_path) ||
            std::strstr(exefs_path, "..") != nullptr) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        size_t free_index = MaxEcsSessions;
        for (size_t i = 0; i < MaxEcsSessions; ++i) {
            if (!g_RegisteredContents[i].active) {
                if (free_index == MaxEcsSessions) free_index = i;
                continue;
            }
            if (g_RegisteredContents[i].program_id != program_id) continue;
            if (std::strcmp(g_RegisteredContents[i].exefs_path, exefs_path) == 0) {
                // Idempotent registration also gives a returned server thread
                // a chance to restart if the old wait set became empty.
                return StartServerThreadOnce();
            }
            EcsLog("ECS registration refused: program id already maps to another path");
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        if (free_index == MaxEcsSessions) {
            EcsLog("ECS registration refused: registration table is full");
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        InitializeServer();
        bool preparedThreadHere = false;
        const Result prepareRc = PrepareServerThread(&preparedThreadHere);
        if (R_FAILED(prepareRc)) {
            EcsLog("ECS registration refused: server thread cannot be prepared");
            return prepareRc;
        }

        Handle move_h = INVALID_HANDLE;
        bool loader_registered = false;
        bool session_registered = false;

        // Run the ams-result logic in a lambda; convert to libnx Result at the
        // boundary so callers (which use libnx Result) stay simple.
        const ams::Result r = [&]() -> ams::Result {
            R_TRY(RegisterExternalCodeCmd(program_id, &move_h));
            loader_registered = true;

            FsFileSystem sd_fs;
            R_TRY(fsOpenSdCardFileSystem(&sd_fs));

            std::shared_ptr<ams::fs::fsa::IFileSystem> remote_sd =
                std::make_shared<ams::fs::RemoteFileSystem>(sd_fs);
            auto subdir_fs =
                std::make_shared<ams::fssystem::SubDirectoryFileSystem>(std::move(remote_sd));

            ams::fs::Path exefs_fs_path;
            R_TRY(exefs_fs_path.Initialize(exefs_path, std::strlen(exefs_path)));
            R_TRY(exefs_fs_path.Normalize(ams::fs::PathFlags{}));
            R_TRY(subdir_fs->Initialize(exefs_fs_path));

            auto sd_ifs = MakeSharedObject<ams::fssrv::impl::FileSystemInterfaceAdapter,
                                           ams::fssrv::sf::IFileSystem>(std::move(subdir_fs), false);
            R_TRY(g_Manager.RegisterSession(
                move_h, ams::sf::cmif::ServiceObjectHolder(std::move(sd_ifs))));
            session_registered = true;
            move_h = INVALID_HANDLE; // Ownership transferred to ServerManager.
            R_SUCCEED();
        }();

        if (R_FAILED(r)) {
            if (loader_registered) {
                const Result unreg_rc = UnregisterExternalCodeCmd(program_id);
                if (R_FAILED(unreg_rc))
                    EcsLog("ECS rollback warning: could not unregister failed program id");
            }
            if (!session_registered && move_h != INVALID_HANDLE)
                svcCloseHandle(move_h);
            CancelPreparedServerThread(preparedThreadHere);
            EcsLog("ECS registration failed; rolled back loader mapping where possible");
            return r.GetValue();
        }

        auto &entry = g_RegisteredContents[free_index];
        entry.program_id = program_id;
        std::strncpy(entry.exefs_path, exefs_path, sizeof(entry.exefs_path) - 1);
        entry.active = true;

        // Start the serving thread only after the first session is registered.
        const Result startRc = StartServerThreadOnce();
        if (R_FAILED(startRc)) {
            entry = {};
            const Result unregRc = UnregisterExternalCodeCmd(program_id);
            if (R_FAILED(unregRc))
                EcsLog("ECS rollback warning: could not unregister mapping after thread-start failure");
            EcsLog("ECS registration failed: server thread could not start (rc=0x%08X)",
                   static_cast<unsigned int>(startRc));
            return startRc;
        }
        return r.GetValue();
    }

    // `am`'s applet-id -> program-id table, as far as it is publicly known
    // (libnx's AppletId comments, plus 0x...1042 which newer firmware launches
    // for the shop slot). Used to turn a program id from takeover.txt back into
    // the applet slot to start.
    AppletId AppletIdForProgramId(u64 program_id) {
        struct Entry { u64 program_id; AppletId applet_id; };
        constexpr Entry Table[] = {
            { 0x0100000000001001ULL, AppletId_LibraryAppletAuth         },
            { 0x0100000000001002ULL, AppletId_LibraryAppletCabinet      },
            { 0x0100000000001003ULL, AppletId_LibraryAppletController   },
            { 0x0100000000001004ULL, AppletId_LibraryAppletDataErase    },
            { 0x0100000000001005ULL, AppletId_LibraryAppletError        },
            { 0x0100000000001006ULL, AppletId_LibraryAppletNetConnect   },
            { 0x0100000000001007ULL, AppletId_LibraryAppletPlayerSelect },
            { 0x0100000000001008ULL, AppletId_LibraryAppletSwkbd        },
            { 0x0100000000001009ULL, AppletId_LibraryAppletMiiEdit      },
            { 0x010000000000100AULL, AppletId_LibraryAppletWeb          },
            { 0x010000000000100BULL, AppletId_LibraryAppletShop         },
            { 0x0100000000001042ULL, AppletId_LibraryAppletShop         },
            { 0x010000000000100DULL, AppletId_LibraryAppletPhotoViewer  },
            { 0x010000000000100EULL, AppletId_LibraryAppletSet          },
            { 0x010000000000100FULL, AppletId_LibraryAppletOfflineWeb   },
            { 0x0100000000001010ULL, AppletId_LibraryAppletLoginShare   },
            { 0x0100000000001011ULL, AppletId_LibraryAppletWifiWebAuth  },
            { 0x0100000000001013ULL, AppletId_LibraryAppletMyPage       },
        };
        for (const auto &e : Table)
            if (e.program_id == program_id) return e.applet_id;
        return AppletId_None;
    }

    Result UnregisterExternalContent(u64 program_id) {
        // Tell Atmosphere's loader to stop using our external code. Once the
        // peer closes its session, ServerManager can reap the stale connection.
        const Result rc = UnregisterExternalCodeCmd(program_id);
        if (R_SUCCEEDED(rc)) {
            for (size_t i = 0; i < MaxEcsSessions; ++i) {
                if (g_RegisteredContents[i].active &&
                    g_RegisteredContents[i].program_id == program_id) {
                    g_RegisteredContents[i] = {};
                    break;
                }
            }
        }
        return rc;
    }

} // namespace desknx::system::ecs
