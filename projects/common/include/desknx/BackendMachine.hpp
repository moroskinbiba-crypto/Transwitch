#pragma once

// Host-testable backend state machine (ТЗ §3, BK-01..BK-16). The sysmodule
// translates real AM/applet events into MachineEvent calls and executes the
// returned MachineAction list; all decision logic lives here so it can be
// unit-tested on the host without libnx (audit recommendation P1).

#include <cstdint>
#include <cstddef>

namespace desknx::backend {

enum class MenuState : std::uint8_t { None, Starting, Visible, Hb };
enum class AppState : std::uint8_t { None, Starting, Foreground, Suspended };

enum class Event : std::uint32_t {
    HomeShort = 1,        // HOME pressed (short)
    MenuClosed,           // menu applet finished / holder closed
    AppExited,            // application exited by itself
    PowerRequested,       // system power / sleep request
    SleepFinished,        // wake-up sequence completed
    SdRemoved,            // SD card pulled out
    MenuReadyTimeout,     // no MenuReady within the configured window
    HeartbeatTimeout,     // no heartbeat within the configured window
    BusyStarted,          // menu reported Busy (pause heartbeat watchdog)
    BusyEnded,            // busy pause expired or ended early
    Tick,                 // periodic loop tick (16 ms)
};

enum class ActionKind : std::uint32_t {
    Noop = 0,
    LaunchMenu,           // BK-10: register ECS first (BK-11), then start slot
    StopMenu,             // stop/terminate current menu applet
    NextSlot,             // try the next menu slot (after quick-exit/timeout)
    FocusGame,            // give foreground back to the suspended game
    DeferLaunchTitle,     // store a queued game launch until the menu closes
    DeferLaunchHb,        // store a queued homebrew launch until the menu closes
    LaunchDeferredTitle,  // execute the queued game launch now
    LaunchDeferredHb,     // write target file + launch hbloader now
    TerminateApp,         // soft+hard close of the running application
    EnterSleep,           // run the sleep sequence (PWR-01/BK-16)
    UnregisterEcs,        // tear down ECS registrations (REC path)
    EnterExhausted,       // no slot worked; wait for retry/recovery
    LogDropHome,          // HOME ignored during stabilization window
    NotifyAppExited,      // push AppExited event to the open menu (BK-15)
};

struct Action {
    ActionKind kind{ActionKind::Noop};
    std::uint64_t value{0}; // title id / hb path hash / slot index, where used
};

constexpr std::size_t MaxActionsPerEvent = 4;

// Timeout configuration (milliseconds). Defaults are the values from the ТЗ
// table §3.3; the sysmodule may override them from its config file at boot.
struct Config {
    std::uint64_t menu_ready_ms{25000};
    std::uint64_t heartbeat_ms{15000};
    std::uint64_t quick_exit_ms{3000};
    std::uint64_t exhausted_retry_ms{5000};
    std::uint64_t app_settle_ms{1500};
    std::uint64_t hb_startup_ms{20000}; // HbStarting -> HbRunning watchdog
};

struct Input {
    bool menu_alive{false};        // an applet holder exists in any state
    bool sd_mounted{true};
    std::uint64_t now_ms{0};       // monotonic clock supplied by the caller
};

class Machine {
  public:
    Machine() = default;
    explicit Machine(const Config& config) : config_(config) {}

    // Feed one event; returns the number of actions written to `out`.
    // If capacity cannot hold every action this event would emit, no state
    // transition is committed and zero is returned so the caller can retry.
    std::size_t OnEvent(Event event, const Input& input, Action* out, std::size_t capacity);

    // IPC-driven inputs that also mutate state but are not table events.
    void OnMenuReady(std::uint64_t now_ms);
    void OnHeartbeat(std::uint64_t now_ms);
    void OnBusyStart(std::uint64_t until_ms);
    void OnBusyEnd();
    void RequestLaunchTitle(std::uint64_t title_id);
    void RequestLaunchHomebrew(const char* path, std::size_t length);
    void RequestReturnToGame();
    void RequestCloseApp();

    MenuState menu() const { return menu_; }
    AppState app() const { return app_; }
    bool sleeping() const { return sleeping_; }
    bool exhausted() const { return exhausted_; }
    bool hasDeferredTitle() const { return deferred_title_ != 0; }
    bool hasDeferredHb() const { return deferred_hb_; }
    std::uint64_t deferredTitleId() const { return deferred_title_; }
    bool degraded() const { return degraded_; }   // REC-04: running without SD assets
    std::uint32_t droppedEvents() const { return dropped_; }   // IPC-01 drop counter

    // Applet-state view for IPC-06 initial data: 0 none, 1 starting,
    // 2 running (foreground), 3 suspended.
    int appStateCode() const {
        switch (app_) {
            case AppState::None: return 0;
            case AppState::Starting: return 1;
            case AppState::Foreground: return 2;
            case AppState::Suspended: return 3;
        }
        return 0;
    }

    // Timeouts the caller must arm/disarm (single source of truth).
    bool needsMenuReadyTimer() const { return menu_ == MenuState::Starting && !ready_seen_; }
    bool needsHeartbeatTimer() const {
        return menu_ == MenuState::Visible && !busy_active_ && !sleeping_;
    }
    std::uint64_t menuReadyDeadlineMs() const { return ready_deadline_ms_; }
    std::uint64_t heartbeatDeadlineMs() const { return heartbeat_deadline_ms_; }

    void Reset();

    // Called by the sysmodule when it successfully starts a slot
    // (LaunchMenu/LaunchDeferredHb executed) or observes real transitions.
    void NoteMenuLaunching(std::uint64_t now_ms);   // -> MenuStarting
    void NoteHbLaunched(std::uint64_t now_ms);      // -> HbStarting
    void NoteHbRunning();                           // NRO took control
    void NoteAppCreated(std::uint64_t now_ms);      // -> AppStarting
    void NoteAppForeground();                       // settle window passed
    void NoteAppSuspended();                        // menu came over the game
    void SetSelectedSlot(std::uint32_t slot) { selected_slot_ = slot; }
    std::uint32_t selectedSlot() const { return selected_slot_; }

  private:
    enum class Deferred : std::uint8_t { None, Title, Hb, Return, Close };

    std::size_t emit(ActionKind kind, std::uint64_t value, Action* out, std::size_t capacity) const;

    Config config_{};
    std::uint32_t selected_slot_{0};
    MenuState menu_{MenuState::None};
    AppState app_{AppState::None};
    bool sleeping_{false};
    bool exhausted_{false};
    bool degraded_{false};
    bool ready_seen_{false};
    bool quick_exit_{false};       // menu died before ever being ready
    bool hb_running_{false};       // NRO took control of the album slot
    std::uint64_t hb_start_ms_{0}; // when hbloader was launched (HbStarting)
    bool busy_active_{false};
    std::uint64_t busy_until_ms_{0};
    std::uint64_t menu_start_ms_{0};
    std::uint64_t last_heartbeat_ms_{0};
    std::uint64_t ready_deadline_ms_{0};
    std::uint64_t heartbeat_deadline_ms_{0};
    std::uint64_t app_launch_ms_{0};
    std::uint64_t exhausted_since_ms_{0};
    std::uint64_t deferred_title_{0};
    char deferred_hb_path_[512];
    bool deferred_hb_{false};
    Deferred pending_intent_{Deferred::None};
    std::uint32_t dropped_{0};     // events ignored in the current state (logged)
};

} // namespace desknx::backend
