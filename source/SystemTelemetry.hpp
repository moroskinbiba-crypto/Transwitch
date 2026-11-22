#pragma once

#include <switch.h>
#include <switch/services/audctl.h>
#include <switch/services/nifm.h>
#include <switch/services/lbl.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>

// Horizon telemetry and quick-settings service calls are deliberately isolated
// from the input/render thread. The worker publishes small value snapshots;
// SDL textures, widget layout and presentation remain owned by the UI thread.
class SystemTelemetry final {
public:
    enum Demand : std::uint32_t {
        BatteryDemand = 1u << 0,
        SystemDemand = 1u << 1,
        QuickSettingsDemand = 1u << 2,
        DiagnosticsDemand = 1u << 3
    };

    struct JoyconState {
        bool connected{false};
        int level{-1}; // Horizon reports 0..4, not an exact percentage.
        bool charging{false};
    };

    struct Snapshot {
        int battery{-1};
        int chargerType{-1};
        JoyconState left{}, right{};
        bool joyconAvailable{false};
        Uint32 batteryUpdatedAt{0};

        float pcbTemperature{-1.0f};
        float socTemperature{-1.0f};
        bool pcbTemperatureAvailable{false};
        bool socTemperatureAvailable{false};
        std::uint32_t cpuClockHz{0};
        std::uint32_t gpuClockHz{0};
        bool cpuClockAvailable{false};
        bool gpuClockAvailable{false};
        Uint32 systemUpdatedAt{0};

        bool quickLoaded{false};
        bool airplaneMode{false};
        bool wifiEnabled{false};
        bool bluetoothEnabled{false};
        bool soundMuted{false};
        bool airplaneAvailable{false};
        bool wifiAvailable{false};
        bool bluetoothAvailable{false};
        bool soundAvailable{false};
        bool brightnessAvailable{false};
        bool internetConnected{false};
        unsigned wifiStrength{0};
        NifmInternetConnectionType connectionType{NifmInternetConnectionType_WiFi};
        AudioTarget audioTarget{AudioTarget_Speaker};
        float brightness{0.5f};
        Uint32 quickUpdatedAt{0};

        bool processRamAvailable{false};
        std::uint64_t processRamUsed{0};
        std::uint64_t processRamTotal{0};
        Uint32 diagnosticsUpdatedAt{0};

        std::uint64_t commandSequence{0};
        int commandAction{-1};
        int commandArgument{0};
        Result commandResult{0};
        bool commandRejected{false};
    };

    SystemTelemetry() = default;
    SystemTelemetry(const SystemTelemetry&) = delete;
    SystemTelemetry& operator=(const SystemTelemetry&) = delete;

    bool start(bool setsysReady) {
        if (thread_) return true;
        mutex_ = SDL_CreateMutex();
        condition_ = SDL_CreateCond();
        if (!mutex_ || !condition_) {
            destroySync();
            return false;
        }
        setsysReady_ = setsysReady;
        stop_ = false;
        forceQuick_ = false;
        forceDiagnostics_ = false;
        demandMask_ = 0;
        queueHead_ = queueCount_ = 0;
        snapshot_ = Snapshot{};
        thread_ = SDL_CreateThread(threadEntry, "DeskNX system telemetry", this);
        if (!thread_) {
            destroySync();
            return false;
        }
        return true;
    }

    void setDemandMask(std::uint32_t mask) {
        if (!mutex_) return;
        SDL_LockMutex(mutex_);
        if (demandMask_ != mask) {
            demandMask_ = mask;
            SDL_CondSignal(condition_);
        }
        SDL_UnlockMutex(mutex_);
    }

    void requestQuickSettings() {
        if (!mutex_) return;
        SDL_LockMutex(mutex_);
        forceQuick_ = true;
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
    }

    void requestDiagnostics() {
        if (!mutex_) return;
        SDL_LockMutex(mutex_);
        forceDiagnostics_ = true;
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
    }

    bool enqueueQuickAction(int action, int argument = 0) {
        if (!mutex_ || action < 0 || action > 4) return false;
        SDL_LockMutex(mutex_);
        if (stop_ || queueCount_ >= CommandCapacity) {
            SDL_UnlockMutex(mutex_);
            return false;
        }
        const std::size_t tail = (queueHead_ + queueCount_) % CommandCapacity;
        commands_[tail] = Command{action, argument, ++nextCommandSequence_};
        ++queueCount_;
        SDL_CondSignal(condition_);
        SDL_UnlockMutex(mutex_);
        return true;
    }

    Snapshot snapshot() const {
        Snapshot result{};
        if (!mutex_) return result;
        SDL_LockMutex(mutex_);
        result = snapshot_;
        SDL_UnlockMutex(mutex_);
        return result;
    }

    void stop() {
        if (!thread_) {
            destroySync();
            return;
        }
        SDL_LockMutex(mutex_);
        stop_ = true;
        SDL_CondBroadcast(condition_);
        SDL_UnlockMutex(mutex_);
        SDL_WaitThread(thread_, nullptr);
        thread_ = nullptr;
        destroySync();
    }

    bool running() const noexcept { return thread_ != nullptr; }

private:
    struct Command {
        int action{-1};
        int argument{0};
        std::uint64_t sequence{0};
    };
    static constexpr std::size_t CommandCapacity = 8;

    static int SDLCALL threadEntry(void* context) {
        return context ? static_cast<SystemTelemetry*>(context)->run() : 1;
    }

    void destroySync() {
        if (condition_) SDL_DestroyCond(condition_);
        if (mutex_) SDL_DestroyMutex(mutex_);
        condition_ = nullptr;
        mutex_ = nullptr;
    }

    void publish(const Snapshot& value) {
        SDL_LockMutex(mutex_);
        snapshot_ = value;
        SDL_UnlockMutex(mutex_);
    }

    bool popWork(std::uint32_t& demand, bool& forceQuick,
                 bool& forceDiagnostics, Command& command, bool& hasCommand) {
        SDL_LockMutex(mutex_);
        if (!stop_ && queueCount_ == 0 && !forceQuick_ &&
            !forceDiagnostics_ && demandMask_ == 0)
            (void)SDL_CondWaitTimeout(condition_, mutex_, 250);
        if (stop_) {
            SDL_UnlockMutex(mutex_);
            return false;
        }
        demand = demandMask_;
        forceQuick = forceQuick_;
        forceDiagnostics = forceDiagnostics_;
        forceQuick_ = false;
        forceDiagnostics_ = false;
        hasCommand = queueCount_ != 0;
        if (hasCommand) {
            command = commands_[queueHead_];
            queueHead_ = (queueHead_ + 1) % CommandCapacity;
            --queueCount_;
        }
        SDL_UnlockMutex(mutex_);
        return true;
    }

    static void resetJoycon(JoyconState& target) {
        target = JoyconState{};
    }

    static void assignJoycon(JoyconState& target, const HidPowerInfo& info) {
        target.connected = true;
        target.level = static_cast<int>(std::min<u32>(info.battery_level, 4));
        target.charging = info.is_charging;
    }

    static void sampleJoycons(Snapshot& out) {
        resetJoycon(out.left);
        resetJoycon(out.right);
        auto inspect = [&](HidNpadIdType id) {
            const u32 device = hidGetNpadDeviceType(id);
            const bool left = (device &
                (HidDeviceTypeBits_JoyLeft | HidDeviceTypeBits_HandheldLeft)) != 0;
            const bool right = (device &
                (HidDeviceTypeBits_JoyRight | HidDeviceTypeBits_HandheldRight)) != 0;
            if (!left && !right) return;
            if (left && right) {
                HidPowerInfo leftInfo{}, rightInfo{};
                if (R_SUCCEEDED(hidGetNpadPowerInfoSplit(id, &leftInfo, &rightInfo))) {
                    if (!out.left.connected) assignJoycon(out.left, leftInfo);
                    if (!out.right.connected) assignJoycon(out.right, rightInfo);
                }
                return;
            }
            HidPowerInfo info{};
            if (R_SUCCEEDED(hidGetNpadPowerInfoSingle(id, &info))) {
                if (left && !out.left.connected) assignJoycon(out.left, info);
                if (right && !out.right.connected) assignJoycon(out.right, info);
            }
        };
        inspect(HidNpadIdType_Handheld);
        for (int i = 0; i < 8; ++i)
            inspect(static_cast<HidNpadIdType>(i));
        out.joyconAvailable = out.left.connected || out.right.connected;
    }

    void sampleBattery(Snapshot& out, bool& psmReady,
                       Uint32& nextPsmRetry, Uint32 now) {
        if (!psmReady && static_cast<Sint32>(now - nextPsmRetry) >= 0) {
            psmReady = R_SUCCEEDED(psmInitialize());
            nextPsmRetry = now + 5000;
        }
        if (psmReady) {
            u32 percent = 0;
            if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent)) && percent <= 100u)
                out.battery = static_cast<int>(percent);
            PsmChargerType charger = PsmChargerType_Unconnected;
            if (R_SUCCEEDED(psmGetChargerType(&charger)))
                out.chargerType = static_cast<int>(charger);
            else
                out.chargerType = -1;
        }
        sampleJoycons(out);
        out.batteryUpdatedAt = now;
    }

    void sampleSystem(Snapshot& out, bool& tsReady, bool& tsInternalOpen,
                      bool& tsSocOpen, TsSession& tsInternal, TsSession& tsSoc,
                      bool& clockReady, bool& cpuOpen, bool& gpuOpen,
                      ClkrstSession& cpuSession, ClkrstSession& gpuSession,
                      Uint32& nextTsRetry, Uint32& nextClockRetry, Uint32 now) {
        out.pcbTemperatureAvailable = false;
        out.socTemperatureAvailable = false;
        out.cpuClockAvailable = false;
        out.gpuClockAvailable = false;

        if (hosversionGet() >= MAKEHOSVERSION(10, 0, 0)) {
            if (!tsReady && static_cast<Sint32>(now - nextTsRetry) >= 0) {
                tsReady = R_SUCCEEDED(tsInitialize());
                nextTsRetry = now + 5000;
            }
            if (tsReady) {
                if (!tsInternalOpen && R_SUCCEEDED(tsOpenSession(
                    &tsInternal, TsDeviceCode_LocationInternal))) tsInternalOpen = true;
                if (!tsSocOpen && R_SUCCEEDED(tsOpenSession(
                    &tsSoc, TsDeviceCode_LocationExternal))) tsSocOpen = true;
                float temperature = 0.0f;
                if (tsInternalOpen && R_SUCCEEDED(tsSessionGetTemperature(
                    &tsInternal, &temperature)) &&
                    temperature >= -20.0f && temperature <= 120.0f) {
                    out.pcbTemperature = temperature;
                    out.pcbTemperatureAvailable = true;
                }
                temperature = 0.0f;
                if (tsSocOpen && R_SUCCEEDED(tsSessionGetTemperature(
                    &tsSoc, &temperature)) &&
                    temperature >= -20.0f && temperature <= 120.0f) {
                    out.socTemperature = temperature;
                    out.socTemperatureAvailable = true;
                }
            }
        } else {
            if (!tsReady && static_cast<Sint32>(now - nextTsRetry) >= 0) {
                tsReady = R_SUCCEEDED(tsInitialize());
                nextTsRetry = now + 5000;
            }
            if (tsReady) {
                s32 temperature = 0;
                if (R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &temperature)) &&
                    temperature >= -20 && temperature <= 120) {
                    out.pcbTemperature = static_cast<float>(temperature);
                    out.pcbTemperatureAvailable = true;
                }
                temperature = 0;
                if (R_SUCCEEDED(tsGetTemperature(TsLocation_External, &temperature)) &&
                    temperature >= -20 && temperature <= 120) {
                    out.socTemperature = static_cast<float>(temperature);
                    out.socTemperatureAvailable = true;
                }
            }
        }

        if (hosversionGet() >= MAKEHOSVERSION(8, 0, 0)) {
            if (!clockReady && static_cast<Sint32>(now - nextClockRetry) >= 0) {
                clockReady = R_SUCCEEDED(clkrstInitialize());
                nextClockRetry = now + 5000;
            }
            if (clockReady) {
                if (!cpuOpen && R_SUCCEEDED(clkrstOpenSession(
                    &cpuSession, PcvModuleId_CpuBus, 3))) cpuOpen = true;
                if (!gpuOpen && R_SUCCEEDED(clkrstOpenSession(
                    &gpuSession, PcvModuleId_GPU, 3))) gpuOpen = true;
                std::uint32_t rate = 0;
                if (cpuOpen && R_SUCCEEDED(clkrstGetClockRate(&cpuSession, &rate)) &&
                    rate >= 1000000u && rate <= 4000000000u) {
                    out.cpuClockHz = rate;
                    out.cpuClockAvailable = true;
                }
                rate = 0;
                if (gpuOpen && R_SUCCEEDED(clkrstGetClockRate(&gpuSession, &rate)) &&
                    rate >= 1000000u && rate <= 4000000000u) {
                    out.gpuClockHz = rate;
                    out.gpuClockAvailable = true;
                }
            }
        }
        out.systemUpdatedAt = now;
    }

    void sampleQuickSettings(Snapshot& out, Uint32 now) {
        out.airplaneAvailable = out.wifiAvailable = out.bluetoothAvailable = false;
        out.soundAvailable = out.brightnessAvailable = false;
        out.internetConnected = false;
        out.wifiStrength = 0;

        if (setsysReady_) {
            out.wifiAvailable = R_SUCCEEDED(setsysGetWirelessLanEnableFlag(&out.wifiEnabled));
            out.bluetoothAvailable = R_SUCCEEDED(
                setsysGetBluetoothEnableFlag(&out.bluetoothEnabled));
        }

        if (R_SUCCEEDED(nifmInitialize(NifmServiceType_System))) {
            bool wirelessEnabled = true;
            out.airplaneAvailable =
                R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&wirelessEnabled));
            if (out.airplaneAvailable) out.airplaneMode = !wirelessEnabled;
            NifmInternetConnectionType type = NifmInternetConnectionType_WiFi;
            NifmInternetConnectionStatus status =
                NifmInternetConnectionStatus_ConnectingUnknown1;
            u32 strength = 0;
            if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
                out.connectionType = type;
                out.internetConnected = status == NifmInternetConnectionStatus_Connected;
                out.wifiStrength = std::min<u32>(strength, 3);
            }
            nifmExit();
        }

        if (R_SUCCEEDED(audctlInitialize())) {
            AudioTarget target = AudioTarget_Invalid;
            if (R_FAILED(audctlGetActiveOutputTarget(&target)) ||
                target == AudioTarget_Invalid)
                (void)audctlGetDefaultTarget(&target);
            if (target == AudioTarget_Invalid) target = AudioTarget_Speaker;
            out.audioTarget = target;
            out.soundAvailable = R_SUCCEEDED(audctlIsTargetMute(&out.soundMuted, target));
            audctlExit();
        }

        if (R_SUCCEEDED(lblInitialize())) {
            out.brightnessAvailable =
                R_SUCCEEDED(lblGetCurrentBrightnessSetting(&out.brightness));
            lblExit();
        }
        out.quickLoaded = true;
        out.quickUpdatedAt = now;
    }

    Result executeQuickAction(int action, int argument, Snapshot& out,
                              bool& rejected, Uint32 now) {
        rejected = false;
        Result rc = 0;
        if (action == 0) {
            if (R_FAILED(audctlInitialize()))
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            AudioTarget target = AudioTarget_Invalid;
            if (R_FAILED(audctlGetActiveOutputTarget(&target)) ||
                target == AudioTarget_Invalid)
                (void)audctlGetDefaultTarget(&target);
            if (target == AudioTarget_Invalid) target = AudioTarget_Speaker;
            bool muted = false;
            rc = audctlIsTargetMute(&muted, target);
            if (R_SUCCEEDED(rc)) rc = audctlSetTargetMute(target, !muted);
            audctlExit();
        } else if (action == 1) {
            if (R_FAILED(nifmInitialize(NifmServiceType_System)))
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            bool wirelessEnabled = true;
            rc = nifmIsWirelessCommunicationEnabled(&wirelessEnabled);
            if (R_SUCCEEDED(rc))
                rc = nifmSetWirelessCommunicationEnabled(!wirelessEnabled);
            nifmExit();
        } else if (action == 2) {
            if (!setsysReady_)
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            bool wifiEnabled = false;
            rc = setsysGetWirelessLanEnableFlag(&wifiEnabled);
            if (R_SUCCEEDED(rc)) {
                bool wirelessEnabled = true;
                if (R_SUCCEEDED(nifmInitialize(NifmServiceType_System))) {
                    if (R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&wirelessEnabled)) &&
                        !wifiEnabled && !wirelessEnabled)
                        rc = nifmSetWirelessCommunicationEnabled(true);
                    nifmExit();
                }
                if (R_SUCCEEDED(rc))
                    rc = setsysSetWirelessLanEnableFlag(!wifiEnabled);
            }
        } else if (action == 3) {
            if (!setsysReady_)
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            bool enabled = false;
            rc = setsysGetBluetoothEnableFlag(&enabled);
            if (R_SUCCEEDED(rc) && enabled &&
                appletGetOperationMode() != AppletOperationMode_Handheld) {
                rejected = true;
                return rc;
            }
            if (R_SUCCEEDED(rc)) rc = setsysSetBluetoothEnableFlag(!enabled);
        } else if (action == 4) {
            if (R_FAILED(lblInitialize()))
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            float brightness = out.brightness;
            rc = lblGetCurrentBrightnessSetting(&brightness);
            if (R_SUCCEEDED(rc)) {
                brightness = std::max(0.10f, std::min(1.0f,
                    brightness + static_cast<float>(argument) * 0.05f));
                rc = lblSetCurrentBrightnessSetting(brightness);
                if (R_SUCCEEDED(rc))
                    rc = lblApplyCurrentBrightnessSettingToBacklight();
                if (R_SUCCEEDED(rc)) (void)lblSaveCurrentSetting();
            }
            lblExit();
        }
        if (R_SUCCEEDED(rc)) sampleQuickSettings(out, now);
        return rc;
    }

    int run() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        Snapshot current{};
        bool psmReady = false;
        bool tsReady = false, tsInternalOpen = false, tsSocOpen = false;
        bool clockReady = false, cpuOpen = false, gpuOpen = false;
        TsSession tsInternal{}, tsSoc{};
        ClkrstSession cpuSession{}, gpuSession{};
        Uint32 nextBattery = 0, nextSystem = 0, nextQuick = 0;
        Uint32 nextPsmRetry = 0, nextTsRetry = 0, nextClockRetry = 0;
        Uint32 nextDiagnostics = 0;

        for (;;) {
            std::uint32_t demand = 0;
            bool forceQuick = false, forceDiagnostics = false, hasCommand = false;
            Command command{};
            if (!popWork(demand, forceQuick, forceDiagnostics, command, hasCommand))
                break;
            const Uint32 now = SDL_GetTicks();
            bool changed = false;

            if ((demand & BatteryDemand) && static_cast<Sint32>(now - nextBattery) >= 0) {
                sampleBattery(current, psmReady, nextPsmRetry, now);
                nextBattery = now + 3000;
                changed = true;
            }
            if ((demand & SystemDemand) && static_cast<Sint32>(now - nextSystem) >= 0) {
                sampleSystem(current, tsReady, tsInternalOpen, tsSocOpen,
                    tsInternal, tsSoc, clockReady, cpuOpen, gpuOpen,
                    cpuSession, gpuSession, nextTsRetry, nextClockRetry, now);
                nextSystem = now + 2500;
                changed = true;
            }
            if (((demand & QuickSettingsDemand) &&
                 static_cast<Sint32>(now - nextQuick) >= 0) || forceQuick) {
                sampleQuickSettings(current, now);
                nextQuick = now + 3500;
                changed = true;
            }
            if (((demand & DiagnosticsDemand) &&
                 static_cast<Sint32>(now - nextDiagnostics) >= 0) || forceDiagnostics) {
                u64 used = 0, total = 0;
                const Result usedRc = svcGetInfo(
                    &used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
                const Result totalRc = svcGetInfo(
                    &total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
                current.processRamAvailable =
                    R_SUCCEEDED(usedRc) && R_SUCCEEDED(totalRc) && total > 0;
                current.processRamUsed = current.processRamAvailable ? used : 0;
                current.processRamTotal = current.processRamAvailable ? total : 0;
                current.diagnosticsUpdatedAt = now;
                nextDiagnostics = now + 5000;
                changed = true;
            }
            if (hasCommand) {
                bool rejected = false;
                const Result rc = executeQuickAction(
                    command.action, command.argument, current, rejected, now);
                current.commandSequence = command.sequence;
                current.commandAction = command.action;
                current.commandArgument = command.argument;
                current.commandResult = rc;
                current.commandRejected = rejected;
                changed = true;
                // A command always publishes a fresh settings snapshot, even
                // if the caller has already closed the quick-settings surface.
                if (!current.quickLoaded || now - current.quickUpdatedAt >= 3500)
                    sampleQuickSettings(current, now);
            }
            if (changed) publish(current);
        }

        if (tsInternalOpen) (void)tsSessionClose(&tsInternal);
        if (tsSocOpen) (void)tsSessionClose(&tsSoc);
        if (tsReady) tsExit();
        if (cpuOpen) (void)clkrstCloseSession(&cpuSession);
        if (gpuOpen) (void)clkrstCloseSession(&gpuSession);
        if (clockReady) clkrstExit();
        if (psmReady) psmExit();
        return 0;
    }

    mutable SDL_mutex* mutex_{nullptr};
    SDL_cond* condition_{nullptr};
    SDL_Thread* thread_{nullptr};
    bool stop_{false};
    bool setsysReady_{false};
    bool forceQuick_{false};
    bool forceDiagnostics_{false};
    std::uint32_t demandMask_{0};
    std::array<Command, CommandCapacity> commands_{};
    std::size_t queueHead_{0};
    std::size_t queueCount_{0};
    std::uint64_t nextCommandSequence_{0};
    Snapshot snapshot_{};
};
