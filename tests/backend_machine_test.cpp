// Host unit tests for the DeskNX backend state machine (ТЗ §3.2/§3.3).
#include <desknx/BackendMachine.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace desknx::backend;

namespace {

struct Runner {
    Machine m{Config{}};
    Action actions[MaxActionsPerEvent];
    std::size_t count{0};

    std::size_t feed(Event e, std::uint64_t now) {
        Input in;
        in.now_ms = now;
        count = m.OnEvent(e, in, actions, MaxActionsPerEvent);
        return count;
    }
    bool has(ActionKind k) const {
        for (std::size_t i = 0; i < count; ++i)
            if (actions[i].kind == k) return true;
        return false;
    }
};

} // namespace

int main() {
    // --- Insufficient output capacity must not partially commit a transition.
    {
        Machine machine;
        machine.NoteHbLaunched(0);
        machine.NoteHbRunning();
        Input input;
        input.now_ms = 100;
        Action tooSmall[1]{};
        assert(machine.OnEvent(Event::HomeShort, input, tooSmall, 1) == 0);
        assert(machine.menu() == MenuState::Hb); // no side effects were committed

        Action enough[MaxActionsPerEvent]{};
        assert(machine.OnEvent(Event::HomeShort, input, enough, MaxActionsPerEvent) == 3);
        assert(enough[0].kind == ActionKind::StopMenu);
        assert(enough[1].kind == ActionKind::UnregisterEcs);
        assert(enough[2].kind == ActionKind::LaunchMenu);
        assert(machine.menu() == MenuState::None);
    }

    // --- HOME with nothing running launches the menu (ТЗ row 1).
    {
        Runner r;
        assert(r.feed(Event::HomeShort, 1000) == 1);
        assert(r.has(ActionKind::LaunchMenu));
    }

    // --- HOME while the menu is starting does not double-launch (BK-10).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        assert(r.feed(Event::HomeShort, 500) == 0);
    }

    // --- HOME over a settled game: launch menu; game suspends when menu opens.
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        assert(r.feed(Event::HomeShort, 5000) == 1);
        assert(r.has(ActionKind::LaunchMenu));
        r.m.NoteMenuLaunching(5100);
        r.m.NoteAppSuspended();
        r.m.OnMenuReady(6000);
        assert(r.m.menu() == MenuState::Visible);
        assert(r.m.app() == AppState::Suspended);
    }

    // --- HOME during the stabilization window is ignored and logged (row 3).
    {
        Runner r;
        r.m.NoteAppCreated(1000);
        assert(r.feed(Event::HomeShort, 1500) == 1);
        assert(r.has(ActionKind::LogDropHome));
        assert(!r.has(ActionKind::LaunchMenu));
        // After the window it works.
        assert(r.feed(Event::HomeShort, 2600) == 1);
        assert(r.has(ActionKind::LaunchMenu));
    }

    // --- HOME with the menu visible and a suspended game closes the menu
    //     and returns focus (row 4).
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        r.feed(Event::HomeShort, 5000);
        r.m.NoteMenuLaunching(5000);
        r.m.OnMenuReady(6000);
        r.m.NoteAppSuspended();
        assert(r.feed(Event::HomeShort, 9000) == 1);
        assert(r.has(ActionKind::StopMenu));
        assert(r.feed(Event::MenuClosed, 9100) == 1);
        assert(r.has(ActionKind::FocusGame));
    }

    // --- HOME with the menu visible and no game is a no-op (row 5).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        assert(r.feed(Event::HomeShort, 500) == 0);
    }

    // --- HOME during homebrew stops hbloader, drops ECS, launches menu (row 6).
    {
        Runner r;
        r.m.NoteHbLaunched(0);
        r.m.NoteHbRunning();
        assert(r.feed(Event::HomeShort, 5000) == 3);
        assert(r.has(ActionKind::StopMenu));
        assert(r.has(ActionKind::UnregisterEcs));
        assert(r.has(ActionKind::LaunchMenu));
        assert(r.m.menu() == MenuState::None);
    }

    // --- Deferred title launch fires when the menu closes (LNC-01).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        r.m.RequestLaunchTitle(0x0100ABCDULL);
        assert(r.feed(Event::MenuClosed, 500) == 1);
        assert(r.has(ActionKind::LaunchDeferredTitle));
        assert(r.actions[0].value == 0x0100ABCDULL);
        assert(!r.m.hasDeferredTitle());
    }

    // --- Deferred homebrew launch fires when the menu closes.
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        const char path[] = "sdmc:/switch/demo.nro";
        r.m.RequestLaunchHomebrew(path, sizeof(path) - 1);
        assert(r.feed(Event::MenuClosed, 500) == 1);
        assert(r.has(ActionKind::LaunchDeferredHb));
        // A closing menu must not relaunch hbloader after HOME stopped it.
        r.m.RequestLaunchHomebrew(path, sizeof(path) - 1);
        r.m.NoteHbLaunched(600);
        assert(r.feed(Event::HomeShort, 700) == 3);
        assert(r.feed(Event::MenuClosed, 800) == 0);
    }

    // --- Quick exit before MenuReady advances to the next slot (§3.2).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        assert(r.feed(Event::MenuClosed, 1000) == 1); // lived 1 s < 3 s quick-exit
        assert(r.has(ActionKind::NextSlot));
    }
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        assert(r.feed(Event::MenuClosed, 5000) == 0); // clean close, nothing requested
    }

    // --- Game exits by itself: menu gets AppExited notification (BK-15/LNC-04).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        r.m.NoteAppCreated(200); // started then closed earlier... keep simple below
        r.m.NoteAppForeground();
        assert(r.feed(Event::AppExited, 5000) == 1);
        assert(r.has(ActionKind::NotifyAppExited));
        assert(r.m.app() == AppState::None);
    }
    // --- Game exits with no menu at all: menu is launched.
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        assert(r.feed(Event::AppExited, 5000) == 1);
        assert(r.has(ActionKind::LaunchMenu));
    }

    // --- Sleep sequence: everything is ignored until SleepFinished (BK-16).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        assert(r.feed(Event::PowerRequested, 200) == 1);
        assert(r.has(ActionKind::EnterSleep));
        assert(r.feed(Event::HomeShort, 300) == 0);
        assert(r.feed(Event::HeartbeatTimeout, 400) == 0);
        assert(r.feed(Event::SleepFinished, 1000) == 1);
        assert(r.has(ActionKind::LaunchMenu));
        assert(!r.m.sleeping());
    }

    // --- SD removal while hb runs: stop, unregister, degraded relaunch (REC-05).
    {
        Runner r;
        r.m.NoteHbLaunched(0);
        r.m.NoteHbRunning();
        Input in;
        in.sd_mounted = false;
        in.now_ms = 1000;
        Action out[4];
        const std::size_t n = r.m.OnEvent(Event::SdRemoved, in, out, 4);
        assert(n == 3);
        assert(r.m.degraded());
    }

    // --- MenuReady timeout with a suspended game: focus + Exhausted (BK-12).
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        r.feed(Event::HomeShort, 5000);
        r.m.NoteMenuLaunching(5000);
        r.m.NoteAppSuspended();
        assert(r.feed(Event::MenuReadyTimeout, 30000) == 3);
        assert(r.has(ActionKind::FocusGame));
        assert(r.has(ActionKind::EnterExhausted));
        assert(r.m.exhausted());
    }
    // --- MenuReady timeout without a game: next slot.
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        assert(r.feed(Event::MenuReadyTimeout, 25000) == 2);
        assert(r.has(ActionKind::StopMenu));
        assert(r.has(ActionKind::NextSlot));
    }

    // --- Heartbeat timeout without a game restarts the same slot (BK-13).
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(100);
        assert(r.feed(Event::HeartbeatTimeout, 20000) == 2);
        assert(r.has(ActionKind::StopMenu));
        assert(r.has(ActionKind::LaunchMenu));
        assert(!r.has(ActionKind::NextSlot));
    }

    // --- Busy pauses heartbeat monitoring; deadline re-arms on end.
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.m.OnMenuReady(1000);
        assert(r.m.needsHeartbeatTimer());
        r.m.OnBusyStart(9000);
        assert(!r.m.needsHeartbeatTimer());
        assert(r.feed(Event::BusyEnded, 9000) == 0);
        assert(r.m.needsHeartbeatTimer());
        assert(r.m.heartbeatDeadlineMs() >= 9000);
    }

    // --- Tick drives AppStarting -> AppForeground and Exhausted retry.
    {
        Runner r;
        r.m.NoteAppCreated(0);
        assert(r.m.app() == AppState::Starting);
        r.feed(Event::Tick, 1000); // inside settle window
        assert(r.m.app() == AppState::Starting);
        r.feed(Event::Tick, 1600); // past 1.5 s
        assert(r.m.app() == AppState::Foreground);
    }
    {
        Runner r;
        r.m.NoteMenuLaunching(0);
        r.feed(Event::HeartbeatTimeout, 20000);
        // simulate exhausted entry via ready timeout with a game
    }
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        r.feed(Event::HomeShort, 5000);
        r.m.NoteMenuLaunching(5000);
        r.m.NoteAppSuspended();
        r.feed(Event::MenuReadyTimeout, 30000);
        assert(r.m.exhausted());
        assert(r.feed(Event::Tick, 34000) == 0); // retry not due yet (5 s)
        assert(r.feed(Event::Tick, 36000) == 1); // retry fired
        assert(r.has(ActionKind::LaunchMenu));
        assert(!r.m.exhausted());
    }

    // --- Hb startup watchdog: hbloader never handed control (Tick).
    {
        Runner r;
        r.m.NoteHbLaunched(0);
        assert(r.feed(Event::Tick, 19000) == 0);
        assert(r.feed(Event::Tick, 21000) == 3);
        assert(r.has(ActionKind::StopMenu));
        assert(r.has(ActionKind::UnregisterEcs));
        assert(r.has(ActionKind::LaunchMenu));
    }

    // --- ReturnToGame / CloseApp intents resolve at menu close.
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        r.feed(Event::HomeShort, 5000);
        r.m.NoteMenuLaunching(5000);
        r.m.OnMenuReady(6000);
        r.m.NoteAppSuspended();
        r.m.RequestReturnToGame();
        assert(r.feed(Event::MenuClosed, 7000) == 1);
        assert(r.has(ActionKind::FocusGame));
    }
    {
        Runner r;
        r.m.NoteAppCreated(0);
        r.m.NoteAppForeground();
        r.feed(Event::HomeShort, 5000);
        r.m.NoteMenuLaunching(5000);
        r.m.OnMenuReady(6000);
        r.m.RequestCloseApp();
        assert(r.feed(Event::MenuClosed, 7000) == 1);
        assert(r.has(ActionKind::TerminateApp));
    }

    // --- Requests against no app are dropped, not crashed (IPC distrust).
    {
        Runner r;
        const auto before = r.m.droppedEvents();
        r.m.RequestReturnToGame();
        r.m.RequestCloseApp();
        r.m.OnMenuReady(0); // not starting
        assert(r.m.droppedEvents() == before + 3);
    }

    // --- Unknown events are ignored and counted (§3.1).
    {
        Runner r;
        const auto before = r.m.droppedEvents();
        r.feed(static_cast<Event>(9999), 0);
        assert(r.m.droppedEvents() == before + 1);
    }

    // --- IPC-06 style app_state codes stay consistent.
    {
        Runner r;
        assert(r.m.appStateCode() == 0);
        r.m.NoteAppCreated(0);
        assert(r.m.appStateCode() == 1);
        r.feed(Event::Tick, 2000);
        assert(r.m.appStateCode() == 2);
        r.m.NoteAppSuspended();
        assert(r.m.appStateCode() == 3);
    }

    std::puts("backend_machine_test: all assertions passed");
    return 0;
}
