// DeskNX backend state machine implementation (ТЗ §3, BK-01..BK-17).
// Pure logic: the sysmodule translates real AM/applet events into Event
// calls and executes the returned Action list. Host-testable without libnx.

#include "desknx/BackendMachine.hpp"

#include <cstring>

namespace desknx::backend {

namespace {

bool InSettleWindow(std::uint64_t now_ms, std::uint64_t launch_ms, std::uint64_t settle_ms) {
    return now_ms - launch_ms < settle_ms;
}

} // namespace

void Machine::Reset() {
    *this = Machine{config_};
}

void Machine::NoteMenuLaunching(std::uint64_t now_ms) {
    menu_ = MenuState::Starting;
    ready_seen_ = false;
    quick_exit_ = false;
    busy_active_ = false;
    menu_start_ms_ = now_ms;
    ready_deadline_ms_ = now_ms + config_.menu_ready_ms;
    exhausted_ = false;
}

void Machine::NoteHbLaunched(std::uint64_t now_ms) {
    menu_ = MenuState::Hb;
    hb_start_ms_ = now_ms;
    hb_running_ = false;
    exhausted_ = false;
}

void Machine::NoteHbRunning() {
    if (menu_ == MenuState::Hb) {
        hb_running_ = true;
    }
}

void Machine::NoteAppCreated(std::uint64_t now_ms) {
    app_ = AppState::Starting;
    app_launch_ms_ = now_ms;
}

void Machine::NoteAppForeground() {
    if (app_ == AppState::Starting) {
        app_ = AppState::Foreground;
    }
}

void Machine::NoteAppSuspended() {
    if (app_ == AppState::Foreground || app_ == AppState::Starting) {
        app_ = AppState::Suspended;
    }
}

void Machine::OnMenuReady(std::uint64_t now_ms) {
    if (menu_ != MenuState::Starting) {
        ++dropped_;
        return;
    }
    menu_ = MenuState::Visible;
    ready_seen_ = true;
    last_heartbeat_ms_ = now_ms;
    heartbeat_deadline_ms_ = now_ms + config_.heartbeat_ms;
}

void Machine::OnHeartbeat(std::uint64_t now_ms) {
    if (menu_ != MenuState::Visible) {
        ++dropped_;
        return;
    }
    last_heartbeat_ms_ = now_ms;
    heartbeat_deadline_ms_ = now_ms + config_.heartbeat_ms;
}

void Machine::OnBusyStart(std::uint64_t until_ms) {
    if (menu_ != MenuState::Visible) {
        ++dropped_;
        return;
    }
    busy_active_ = true;
    busy_until_ms_ = until_ms;
}

void Machine::OnBusyEnd() {
    busy_active_ = false;
    busy_until_ms_ = 0;
}

void Machine::RequestLaunchTitle(std::uint64_t title_id) {
    deferred_title_ = title_id;
    pending_intent_ = Deferred::Title;
}

void Machine::RequestLaunchHomebrew(const char* path, std::size_t length) {
    if (path == nullptr || length == 0 || length >= sizeof(deferred_hb_path_)) {
        ++dropped_;
        return;
    }
    std::memcpy(deferred_hb_path_, path, length);
    deferred_hb_path_[length] = '\0';
    deferred_hb_ = true;
    pending_intent_ = Deferred::Hb;
}

void Machine::RequestReturnToGame() {
    if (app_ == AppState::None) {
        ++dropped_;
        return;
    }
    pending_intent_ = Deferred::Return;
}

void Machine::RequestCloseApp() {
    if (app_ == AppState::None) {
        ++dropped_;
        return;
    }
    pending_intent_ = Deferred::Close;
}

std::size_t Machine::OnEvent(Event event, const Input& input, Action* out, std::size_t capacity) {
    Action* writer = out;
    std::size_t written = 0;
    auto push = [&](ActionKind kind, std::uint64_t value) {
        if (writer != nullptr && written < capacity) {
            writer->kind = kind;
            writer->value = value;
            ++writer;
        }
        ++written;
    };

    const std::uint64_t now = input.now_ms;

    // BK-16: during the sleep sequence everything but SleepFinished is ignored.
    if (sleeping_ && event != Event::SleepFinished) {
        ++dropped_;
        return 0;
    }

    switch (event) {
    case Event::HomeShort: {
        const bool game_running = app_ == AppState::Foreground || app_ == AppState::Suspended ||
                                  app_ == AppState::Starting;
        if (menu_ == MenuState::Hb) {
            // HOME during homebrew: stop hbloader, drop ECS, open the menu.
            // The state settles to None right away; a MenuClosed event for the
            // killed hbloader applet must not re-trigger any deferred intent.
            push(ActionKind::StopMenu, 0);
            push(ActionKind::UnregisterEcs, 0);
            pending_intent_ = Deferred::None; // closed menu must not relaunch hbloader
            deferred_hb_ = false;
            deferred_title_ = 0;
            menu_ = MenuState::None;
            push(ActionKind::LaunchMenu, 0);
            break;
        }
        if (menu_ == MenuState::Visible) {
            if (game_running) {
                // Close the menu and hand focus back to the suspended game.
                pending_intent_ = Deferred::Return;
                push(ActionKind::StopMenu, 0);
            }
            // Menu visible without a game: HOME keeps the menu open (no-op).
            break;
        }
        if (menu_ == MenuState::Starting) {
            // Launch already in flight; do not double-launch (BK-10).
            break;
        }
        if (!game_running) {
            push(ActionKind::LaunchMenu, 0);
            break;
        }
        if (app_ == AppState::Starting && InSettleWindow(now, app_launch_ms_, config_.app_settle_ms)) {
            // Protection against an am/HOME race right after app creation.
            ++dropped_;
            push(ActionKind::LogDropHome, 0);
            break;
        }
        // Game is up and settled: bring the system forward and start the menu.
        push(ActionKind::LaunchMenu, 0);
        break;
    }
    case Event::MenuClosed: {
        const std::uint64_t lived = now - menu_start_ms_;
        menu_ = MenuState::None;
        busy_active_ = false;
        if (pending_intent_ == Deferred::Title) {
            pending_intent_ = Deferred::None;
            push(ActionKind::LaunchDeferredTitle, deferred_title_);
            deferred_title_ = 0;
            break;
        }
        if (pending_intent_ == Deferred::Hb) {
            pending_intent_ = Deferred::None;
            push(ActionKind::LaunchDeferredHb, 0);
            deferred_hb_ = false;
            break;
        }
        if (pending_intent_ == Deferred::Return) {
            pending_intent_ = Deferred::None;
            if (app_ == AppState::Suspended || app_ == AppState::Foreground) {
                push(ActionKind::FocusGame, 0);
            }
            break;
        }
        if (pending_intent_ == Deferred::Close) {
            pending_intent_ = Deferred::None;
            if (app_ != AppState::None) {
                push(ActionKind::TerminateApp, 0);
            }
            break;
        }
        // Nothing requested. A menu that died quickly and never reached
        // MenuReady means the slot is bad: move on (ТЗ §3.2).
        if (!ready_seen_ && lived < config_.quick_exit_ms) {
            push(ActionKind::NextSlot, selected_slot_);
        }
        break;
    }
    case Event::AppExited: {
        const bool had_app = app_ != AppState::None;
        app_ = AppState::None;
        deferred_title_ = 0;
        if (had_app && (menu_ == MenuState::Visible || menu_ == MenuState::Starting)) {
            push(ActionKind::NotifyAppExited, 0);
        } else if (menu_ == MenuState::None) {
            push(ActionKind::LaunchMenu, 0);
        }
        break;
    }
    case Event::PowerRequested: {
        sleeping_ = true;
        push(ActionKind::EnterSleep, 0);
        break;
    }
    case Event::SleepFinished: {
        const bool menu_was_open = menu_ != MenuState::None;
        sleeping_ = false;
        if (menu_was_open) {
            menu_ = MenuState::None;
            push(ActionKind::LaunchMenu, 0);
        }
        break;
    }
    case Event::SdRemoved: {
        if (menu_ == MenuState::Hb) {
            // Homebrew lives on the SD: stop it and its ECS registration.
            push(ActionKind::StopMenu, 0);
            push(ActionKind::UnregisterEcs, 0);
            menu_ = MenuState::None;
            deferred_hb_ = false;
            if (pending_intent_ == Deferred::Hb) {
                pending_intent_ = Deferred::None;
            }
            degraded_ = true;
            push(ActionKind::LaunchMenu, 0);
            break;
        }
        if (menu_ == MenuState::Visible || menu_ == MenuState::Starting) {
            push(ActionKind::StopMenu, 0);
            menu_ = MenuState::None;
            degraded_ = true;
            push(ActionKind::LaunchMenu, 0);
            break;
        }
        degraded_ = !input.sd_mounted;
        break;
    }
    case Event::MenuReadyTimeout: {
        if (menu_ != MenuState::Starting) {
            ++dropped_;
            break;
        }
        push(ActionKind::StopMenu, 0);
        menu_ = MenuState::None;
        if (app_ == AppState::Suspended) {
            push(ActionKind::FocusGame, 0);
            push(ActionKind::EnterExhausted, 0);
            exhausted_ = true;
            exhausted_since_ms_ = now;
        } else {
            push(ActionKind::NextSlot, selected_slot_);
        }
        break;
    }
    case Event::HeartbeatTimeout: {
        if (menu_ != MenuState::Visible) {
            ++dropped_;
            break;
        }
        push(ActionKind::StopMenu, 0);
        menu_ = MenuState::None;
        if (app_ == AppState::Suspended) {
            // BK-12: the game gets its focus back, backend waits in Exhausted.
            push(ActionKind::FocusGame, 0);
            push(ActionKind::EnterExhausted, 0);
            exhausted_ = true;
            exhausted_since_ms_ = now;
        } else {
            // BK-13: no game — restart the same slot.
            push(ActionKind::LaunchMenu, 0);
        }
        break;
    }
    case Event::BusyStarted: {
        if (menu_ != MenuState::Visible) {
            ++dropped_;
            break;
        }
        busy_active_ = true;
        break;
    }
    case Event::BusyEnded: {
        OnBusyEnd();
        // Re-arm the watchdog from "now" so a long pause does not expire instantly.
        if (menu_ == MenuState::Visible) {
            last_heartbeat_ms_ = now;
            heartbeat_deadline_ms_ = now + config_.heartbeat_ms;
        }
        break;
    }
    case Event::Tick: {
        const std::size_t before = written;
        // HbStarting watchdog: hbloader never handed control to the NRO.
        if (menu_ == MenuState::Hb && !hb_running_ &&
            now - hb_start_ms_ >= config_.hb_startup_ms) {
            push(ActionKind::StopMenu, 0);
            push(ActionKind::UnregisterEcs, 0);
            menu_ = MenuState::None;
            push(ActionKind::LaunchMenu, 0);
        }
        // AppStarting -> AppForeground once the stabilization window passes.
        if (app_ == AppState::Starting && !InSettleWindow(now, app_launch_ms_, config_.app_settle_ms)) {
            app_ = AppState::Foreground;
        }
        // Exhausted retry: try the slots again, no more often than the retry period.
        if (exhausted_ && now - exhausted_since_ms_ >= config_.exhausted_retry_ms) {
            exhausted_ = false;
            push(ActionKind::LaunchMenu, 0);
        }
        if (written != before) {
            return written;
        }
        break;
    }
    default: {
        // Undefined event for this state: ignore and count (ТЗ §3.1).
        ++dropped_;
        return 0;
    }
    }
    return written;
}

} // namespace desknx::backend
