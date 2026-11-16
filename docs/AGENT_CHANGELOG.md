# DeskNX / Transwitch — журнал работ агента

> Это точка продолжения для следующих чатов. Перед новыми изменениями прочитать этот журнал и проверить актуальный main. Добавлять записи ниже, не перезаписывая историю. Успешная компиляция не является доказательством работы на физической консоли.

## Текущее состояние на 15 ноября 2026

- Репозиторий: moroskinbiba-crypto/Transwitch
- Ветка: main
- Базовый commit перед текущим циклом: 2eaff6522a339c21c27a54f20dfdbb3bfcddfc0b
- Три CI-сборки на базовом commit завершились успешно:
  - Build DeskNX: https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43740538559
  - Build Nintendo Switch NRO: https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43740538605
  - Build experimental DeskNX HOME backend: https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43740538717
- Экспериментальный артефакт: DeskNX-system-experimental, artifact ID 14173015202, размер 3,849,914 bytes. Это подтверждает сборку/упаковку, но не runtime-совместимость.
- Hardware-проверки в docs/COMPAT.md не отмечены как выполненные. Не использовать system HOME override как повседневную замену HOME до полного smoke-test и проверенной процедуры отката.

## Важные архитектурные выводы аудита

1. В репозитории есть несколько разных UI/build paths:
   - src/App.tsx + romfs/ — React/Web UI, упаковываемый через Vite.
   - src-switch/main.cpp + Makefile.switch — standalone NRO-хост для offline WebKit UI.
   - source/main.cpp — intended production native HOME UI: implements protocol IPC, heartbeat, input/touch, catalogue and launching; it must be compiled into system library applet.
   - projects/DeskNXMenu/source/main.cpp — simplified prototype UI entry point; release must exclude it while linking source/main.cpp.
   - src-switch/main.cpp — standalone web runtime host.
2. HOME applet must use source/main.cpp plus supporting source/*.cpp and projects/DeskNXMenu/source/*.cpp (excluding the prototype main.cpp). The Makefile VPATH order matters because both directories contain a main.cpp. Standalone WebKit shell and C++ HOME applet are different UI paths.
3. Production persistence реализован непосредственно в source/main.cpp:
   - layout: sdmc:/switch/DeskNX/layout.tsv + layout.tmp + layout.bak;
   - active desk: active-workspace.dat + .tmp + .bak;
   - appearance: settings.ini + .tmp + .bak;
   - launch history: recent.tsv + .tmp + .bak.
   - source/PersistenceStore.* — отдельный JSON-модуль с round-trip host test; production HOME UI source/main.cpp сейчас его не вызывает. projects/DeskNXMenu/source/LayoutStore.* относится к prototype entry point, который исключён из release.
4. System backend использует отдельные projects/sSystem lifecycle / ECS / hbloader компоненты. Нужна runtime-проверка запуска, возврата из игры/NRO, sleep/wake и recovery на железе.

## Ранее сделанные изменения (до текущего цикла)

- 7f09ac3840819970cbfda8e51f9f7a34c0d25870: очищены ошибочные JSON response-хвосты в 56 файлах; восстановлены workflow и scanner fixes.
- 66149fb9df6ed0798ebb087d3bc9392fc376c42a: свой JSON parser/serializer для persistence при -fno-exceptions, добавлен round-trip test.
- 81f0bee86960281fdbdc48e90aeea88d91cb3538: исправлен include path persistence test.
- 77887738d77041d7a5d51e5dbff16d139663ebe7: SteamGridDbCache избавлен от nlohmann JSON/exception dependency.
- 8393a200068ba438ef2846741bd134a86f91af69: исправлен duplicate main.cpp в HOME applet Makefile.
- 2eaff6522a339c21c27a54f20dfdbb3bfcddfc0b: выровнены input/focus API applet и debugPrintf.
- CI уже содержит host tests модели, IPC, backend state machine, installer rollback, workspace/grid interactions и persistence JSON round-trip.
- На последнем CI прошли standalone NRO, DeskNX menu applet, Atmosphère libstratosphere, hbloader, qlaunch backend и упаковка system bundle.

## Текущий цикл — 15 ноября 2026

### Подтверждённые проблемы до исправления

- .gitignore имел в конце сериализованный JSON-фрагмент {"content": ...}.
- В Git отслеживались Vite machine-cache файлы .vite/deps/_metadata.json и .vite/deps/package.json, а также устаревший локальный desknx-release.zip.
- HOME Makefile исключал полноценный source/main.cpp и выбирал упрощённый prototype projects/DeskNXMenu/source/main.cpp без backend IPC, heartbeat и рабочего touch handler.
- В проекте несколько persistence реализаций. Форматы и пути нельзя объединять без трассировки production call sites и миграционных тестов.

### Первый этап — repo hygiene и точность документации

В этом commit:
- очищен .gitignore;
- удалены отслеживаемые .vite cache и устаревший ZIP;
- добавлен scripts/check-repo-hygiene.sh;
- проверка hygiene включена в три Actions workflow;
- README и HOME integration docs исправлены: production source/main.cpp реально входит в HOME applet; prototype main исключён.
- добавлен этот журнал работ.

Проверки после commit вынесены в отдельную запись ниже.

### Следующие задачи (приоритет)

1. P0 — завершить проверку experimental HOME backend: ECS register/unregister, HOME lifecycle, повторный возврат из игры и sleep/wake.
2. P0 — добавить host tests для production parser/layout recovery/settings/history backup recovery и проверок fsdevCommitDevice fail points.
3. P1 — проверить installer/uninstaller при прерывании на каждом шаге, повторное выполнение и безопасное обновление без потери резервной копии.
4. P1 — профилировать catalogue/artwork pipeline по времени и памяти; проверить lazy decode и texture eviction под Switch memory budget.
5. P1 — аудит touchInput/keyInput: edge cases multi-touch, drag/drop, modal focus, controller/touch handoff, управление пустыми ячейками.
6. P2 — закрепить toolchain refs вместо плавающего devkitpro/devkita64:latest только после сравнения версий и совместимости.
7. Hardware smoke test: cold boot, HOME open, launch title, HOME from game, launch NRO, HOME from NRO, repeated loops, sleep/wake, recovery. Записать HOS/Atmosphère/libnx/build ID и результат.

## Формат следующих записей

Для каждого завершённого этапа записывать дату, commit SHA, список файлов, причину, тесты/CI со ссылками, открытые риски и hardware checks (passed / not run). Предыдущие записи не удалять.


## Уточнение production HOME entry point — 15 ноября 2026

В ходе аудита фактического build path обнаружено, что предыдущая запись о том, что source/main.cpp является неиспользуемой legacy-реализацией, была неверной. Это полноценный system-applet UI: в нём реализованы MenuReady, heartbeat, backend IPC, запуск Title/NRO, события, сенсорное управление, рабочие столы, фон/шрифты/иконки и диагностика. Упрощённый projects/DeskNXMenu/source/main.cpp не содержит этих протокольных вызовов; его onTouchEvent — заглушка. Исправление Makefile должно выбирать production source/main.cpp и исключать prototype entry point. Добавлен guard script, который проверяет оба набора символов и порядок VPATH.


## Запись работ — 15 ноября 2026 (продолжение)

### Commit 57400126dd42e7201cf6bd5bfa1b4903177a8102 — repo hygiene

- Очищен .gitignore от сериализованного JSON-хвоста.
- Удалены tracked generated files: .vite/deps/_metadata.json, .vite/deps/package.json, desknx-release.zip.
- Добавлен scripts/check-repo-hygiene.mjs (затем заменён на bash-версию из-за отсутствия Node в devkitPro container).
- README и docs/HOME_INTEGRATION.md получили уточнение build paths.
- Workflow проверка была изначально ошибочной из-за отсутствия Node в контейнере; не считать этот commit прошедшим CI.

### Commit 35e3cc9af66b7157c87399dd8b7736cf96e3aae4 — временная проба persistence prototype

- Начата интеграция WorkspaceFileStore в projects/DeskNXMenu/source/main.cpp.
- После дальнейшего аудита выяснилось, что это prototype entry point, а не production HOME UI; он не содержит IPC/heartbeat/запуска приложений. Его изменения отменены следующим переключением build path. Не ориентироваться на этот commit как на актуальную реализацию.

### Commits 0024467ca3846bad7fd87cd681f5fc1cedd02642 и 228f4d67fee11866c6a26ff8e1b1d33d18d91bb5 — CI hygiene

- Проверка переведена с Node script на scripts/check-repo-hygiene.sh.
- git ls-files запускается с точечным -c safe.directory="$PWD" для чтения индекса Docker checkout; глобальная Git security policy не отключается.
- Проверка hygiene и guard production entry point выполняются до компиляции.
- Run 43746917772: Build DeskNX success; run 43746917774: standalone NRO success. Backend run был отменён новым commit.
- Причина первых CI failures: Node отсутствовал в контейнере; затем Git отказал из-за dubious ownership. Исправлено.

### Commit 2f9546499811b874c04c4690d287677c7b99443d — важное исправление HOME build path

- projects/DeskNXMenu/Makefile теперь задаёт SOURCES := ../../source ./source и помещает COMMON_CPPFILES (включая production source/main.cpp) перед menu modules.
- Prototype projects/DeskNXMenu/source/main.cpp исключён через filter-out main.cpp.
- Добавлен scripts/check-home-applet-entrypoint.sh; проверяется наличие system IPC/heartbeat/title/NRO launch/touch handling.
- README и docs/HOME_INTEGRATION.md исправлены.
- CI: [Build DeskNX run 43747357944](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43747357944) — success; [NRO run 43747357968](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43747357968) — success. Это подтверждает компиляцию выбранного build path; physical Switch не тестировался.
- Experimental backend run 43747358014 был на стадии Build Atmosphere libstratosphere и позже был superseded/cancelled.

### Commit 43a8a41c49399e3f82b41a7d02d67895973bf583 — durable SD commit

- source/main.cpp: добавлен commitSdCard() на основе fsdevCommitDevice("sdmc").
- Для settings, recent history, active-workspace marker и layout выполняется commit temp file до atomic rename и commit нового состояния после rename.
- Layout publish commit failure блокирует дальнейшую перезапись и сохраняет layout.bak как last-known-good copy.
- CI на этом commit: [Build DeskNX 43748271652](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43748271652) и [NRO 43748271725](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43748271725) — success; backend workflow отменён при появлении следующего commit.

### Commit 2896a1f4bf2f02e17d4dc0b7436a99860132e603 — recovery fallback

- loadLauncherSettings() пытается settings.ini и settings.bak.
- loadHistory() пытается recent.tsv и recent.bak; malformed/oversized files не загружаются в live state.
- loadActiveWorkspaceState() пытается active-workspace.dat и .bak, принимая только marker, проходящий parseActiveWorkspaceState against loaded workspaces.
- backupBrokenLayout() фиксирует rename испорченной раскладки через fsdevCommitDevice.
- В scripts/check-home-applet-entrypoint.sh добавлена статическая защита наличия durable SD commit и backup fallback.
- CI после этого commit: runs 43748420562 (NRO), 43748420602 (DeskNX), 43748420582 (experimental backend) — ожидают завершения на момент записи.
- Hardware проверки: NOT RUN; необходим Switch с настроенной Atmosphère и возможностью восстановить stock qlaunch.



## Диагностика раннего падения на реальной консоли — 15 ноября 2026

### Подготовлен commit с ранним журналированием (после hardware failure report)

Проблема: пользователь сообщил, что DeskNX закрывается на физической Switch, но
`switch/DeskNX` и диагностические файлы не создаются.

Аудит выявил пробелы:
- system backend создавал `daemon.log` только после инициализации всех сервисов;
- HOME UI создавал `diagnostics.log` только при успешном mount SD и не создавал файл
  непосредственно в `Diagnostics::Initialize`, а ждал первого последующего события;
- если процесс падал до этих точек, SD оставалась без диагностических следов.

В текущем изменении:
- переносится mount SD системного backend сразу после успешного `sm::Initialize` и
  `fsInitialize`, до остальных системных сервисов;
- каждый вызов `RecordInit` сразу пишет Result-код и критичность в `daemon.log`;
- ранние ошибки `sm`, `fs` и mount SD выводят короткий breadcrumb в kernel debug output,
  поскольку записать на SD до монтирования невозможно;
- `Diagnostics::Initialize` сразу создаёт `diagnostics.log` и marker
  `logger_ready`, чтобы подтвердить вход в main() и успешное монтирование SD;
- backend log flush перед закрытием файла.

CI и физическая проверка для этого изменения: NOT RUN на момент создания записи.
Остающийся риск: падение до выполнения системного init callback либо до монтирования SD
по-прежнему не может оставить файл на карте; тогда нужны Atmosphère crash report и код ошибки.


### Итог аудита и CI — commit 3fc11bf + 5e16a64

- Подтверждённый root cause: в `Makefile.switch` передавался `--romfsdir`, но не `--nacp`/`--icon`/`--romfs`. `elf2nro` в таком случае выполнял ранний выход и не писал ASET. Оба старых NRO (208896 bytes) не содержали ни NACP, ни RomFS.
- Исправление: `--nacp=$(OUTPUT).nacp` + `--romfsdir=$(TOPDIR)/romfs`; `build.yml` теперь перестраивает Vite UI и RomFS, как и отдельный NRO workflow.
- Добавлен `scripts/check-nro-assets.py`; оба workflows падают, если в NRO нет NRO0, ASET v0, непустого NACP или RomFS, либо секции выходят за границы файла.
- Реальный скачанный артефакт commit 5e16a64: NRO 2526432 bytes; ASET найден; NACP 16384 bytes; RomFS 2288808 bytes; end-offset RomFS точно совпадает с размером файла. В RomFS обнаруживается имя `index.html`.
- CI: [Build DeskNX 43754783292](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43754783292) — success; [Build Nintendo Switch NRO 43754783303](https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43754783303) — success; script validation passed in both.
- Runtime NRO теперь пишет `diagnostics.log` из libnx `userAppInit`, а затем пишет каждый этап HID/applet mode/RomFS/webOfflineCreate/WebKit. Неверный bare `index.html` fallback удалён; конфиг WebCommonConfig обнуляется до заполнения; неиспользуемые PSM/PDMQRY init calls убраны.
- Изменения runtime/source и README/diagnostics docs находятся в commit `5e16a64ae21cfb4f8c984f364c0eb621d091314f`. В исходном commit `3fc11bfb054c9d64b7982b2c63dd5811dd2c8adf` закрыт основной packaging bug.
- Hardware test: NOT RUN после этих исправлений. Не утверждать, что запуск на Switch подтверждён, пока пользователь не проверит новый NRO.



## Phase 0 — native standalone NRO, удаление HTML/WebKit — 16 ноября 2026

Цель: убрать зависимость standalone приложения от Nintendo Offline Web Applet и
использовать единый native C++/SDL2 интерфейс для NRO и HOME library applet.

Что изменено:
- `Makefile.switch` теперь собирает production `source/main.cpp` и тот же набор
  supporting modules, что и HOME applet; prototype entry point исключён.
- Standalone target не определяет `DESKNX_SYSTEM_APPLET`: IPC/heartbeat остаются
  только в HOME applet режиме.
- Удаляются `src/`, `src-switch/`, `romfs/`, React/TypeScript/Vite/package files,
  HTML entrypoint и scripts подготовки/сверки web bundle.
- Оба основных build workflow и system-backend workflow собирают native NRO;
  npm/Vite/RomFS шаги удалены.
- `scripts/check-standalone-entrypoint.sh` проверяет выбор production main,
  SDL2 dependencies и отсутствие WebKit API в standalone runtime.
- `scripts/check-nro-assets.py` требует NRO0/ASET/NACP и запрещает RomFS section.
- `scripts/check-repo-hygiene.sh` запрещает повторное добавление web runtime/build files.
- Обновлены README, feature inventory, native frontend roadmap и diagnostics;
  добавлен `docs/AUDIT_2026-11-16.md`.

Статус проверок на момент записи:
- GitHub Actions cross-build/host tests: NOT RUN (ожидают push ветки).
- NRO asset validation: NOT RUN (запускается после сборки).
- Реальная Switch: NOT RUN. Устранение `2808-1037` нельзя считать подтверждённым,
  пока новый NRO не проверен на устройстве.
- Системный qlaunch backend по-прежнему experimental; этот этап его не устанавливает.

Следующий этап по плану: после успешного CI проверить standalone NRO на физической
Switch, затем продолжить Phase 1/2.3 — аудит базового controller flow и long-press
touch drag. Не начинать qlaunch takeover до стабилизации standalone NRO.


### Уточнение CI guard и фиксация commit SHA — 16 ноября 2026

- Phase 0 native NRO commit: `920cf795fd2091f720507e6e68cb5ad761bb6348`.
- Исправление guard: `36f8cc5e64e5d4b6a085fdff2e47d87b99f533b4`.
- Первые CI runs `43767698395`, `43767698350` и `43767674134` остановились на новой статической проверке до компиляции. Причина: guard искал макрос `DESKNX_SYSTEM_APPLET` также в комментариях Makefile, где он описывал отличие двух режимов сборки. Это ошибка самой проверки, а не установленная ошибка C++/SDL-сборки.
- Guard исправлен: при проверке Makefile комментарии исключаются; проверяется именно активное определение system-applet macro, ROMFS variable или `--romfsdir`. Запрос WebKit API проверяется отдельно в native source.
- Повторные CI результаты для исправленного commit ещё не получены на момент записи.
- Аппаратная проверка NRO по-прежнему NOT RUN.


### Исправление native include paths после фактической CI-компиляции — 16 ноября 2026

- Повторный CI после guard fix прошёл repo hygiene и standalone entrypoint checks.
- Host tests workspace/persistence и другие предыдущие unit-test steps в Build DeskNX завершились успешно.
- Native NRO cross-build остановился на `source/slaunch_applications.hpp:5`: `fatal error: switch.h: No such file or directory`. Это показало, что отдельный standalone Makefile не добавлял include-пути libnx в фактические C/C++ compile flags.
- Исправлен `Makefile.switch`: include-пути формируются относительно экспортированного `TOPDIR` до вычисления `CFLAGS/CXXFLAGS`, включая devkitPro libnx и Switch portlibs. Это также исключает ошибочную зависимость от `CURDIR`, который меняется при рекурсивной сборке из каталога `build/`.
- Результат новой cross-build проверки ожидается. Не считать native NRO собранным, пока Actions не подтвердит compile/link и NRO asset checker.
- Физическая Switch: NOT RUN.


### Исправление Makefile и безопасной инициализации SD для standalone NRO — 16 ноября 2026

CI после первого include-path изменения выявил ещё две конкретные ошибки:
1. Перенос переменной `INCLUDE` был синтаксически некорректен из-за экранирования переносов строк в Makefile; сборка остановилась на `Makefile.switch:47: missing separator`.
2. Аудит libnx показал, что стандартный NRO runtime уже монтирует `sdmc` до входа в `main()`. Повторный вызов `fsdevMountSdmc()` в production UI мог завершиться ошибкой повторного mount. Libnx отклоняет повторное устройство с тем же именем; это опасно для диагностик, так как `sdMounted` мог оставаться false.

Исправления в текущем изменении:
- Include flags перенесены перед вычислением `CFLAGS/CXXFLAGS` и собраны в однострочное определение относительно `TOPDIR`; теперь путь `$(DEVKITPRO)/libnx/include` попадает непосредственно в compiler flags.
- В standalone режиме добавлен `userAppInit`: если libnx уже смонтировал SD, hook сразу создаёт diagnostics log до `main()`.
- В `main()` сначала проверяется `stat("sdmc:/")`, и только если устройство не смонтировано, вызывается `fsdevMountSdmc()`.
- Добавлен отдельный флаг `sdMountedByDeskNX`: при завершении приложение размонтирует только устройство, которое оно само смонтировало, и не снимает libnx-owned mount.
- HOME applet compile mode не экспортирует standalone-only `userAppInit` hook.
- Обновлён standalone guard для проверки раннего hook и безопасного владения SD mount.

Источники фактического поведения libnx: `fsdevMountDevice` отвергает уже смонтированное устройство с тем же именем; `fsdevMountSdmc` преобразует такой отказ в ошибку. Дополнительный libnx example подтверждает, что сообщение «already mounted by libnx» является нормальным для NRO startup.

CI native cross-build и NRO checker после этого исправления: ожидаются.
Физическая Switch: NOT RUN.

## HomeNX architecture audit and launch-path parity — 16 November 2026

Upstream audited: ad3333m/homenx@c744796cf686ab3886ffc7c432af7bdf4411bacd
(GPL-3.0; see docs/HOMENX_PORT_AUDIT_2026-11-16.md).

### Changes in this commit
- source/main.cpp: explicitly exports __nx_applet_type as AppletType_Application
  for standalone NRO and AppletType_LibraryApplet for the system HOME frontend.
  Startup diagnostic records the selected applet type. HomeNX uses the same process
  contract. We leave __nx_heap_size dynamic for now: unlike HomeNX's fixed 208 MiB
  heap, our launch environment may provide a heap override or a smaller applet budget;
  forcing a fixed allocation can abort during libnx initialization before diagnostics.
- projects/sSystem/source/main.cpp: fixed qlaunch build failure at the SD bootstrap
  log. The Stratosphere ams::Result has an explicit GetValue() accessor and cannot
  be directly cast to unsigned int with the selected libnx ref.
- scripts/check-standalone-entrypoint.sh: asserts Application applet type.
- scripts/check-home-applet-entrypoint.sh: asserts LibraryApplet applet type.
- Added docs/HOMENX_PORT_AUDIT_2026-11-16.md: current upstream architecture, port map,
  licensing constraints and future integration plan.

### CI evidence before this change
- Standalone NRO build + NRO structure checker: run 43768488600 success.
- Host tests + native NRO + HOME library applet build: run 43768488594 success.
- Experimental HOME backend run 43768481615 failed in Build qlaunch backend:
  projects/sSystem/source/main.cpp:1615:15 invalid static_cast from const ams::Result
  to unsigned int. This is the failure fixed here.

### Verification state after this change
- New cross-build and host tests: pending; not yet run for this commit.
- Physical Switch launch: NOT RUN. Explicit Application applet type is taken from
  HomeNX's standalone build; it must still be tested with the actual launcher/forwarder.
- Do not mark the original startup exit as fixed until a new artifact has launched on
  physical hardware and its diagnostics log confirms the full startup path.

### Next HomeNX-derived tasks
1. Complete lifecycle comparison/port: event-driven applet wait, ApplicationSession
   bounded join/terminate, menu holder lazy event initialization and safe retry.
2. Move more catalogue ownership to the daemon only if system backend is enabled;
   keep standalone catalogue path as a separately tested fallback.
3. Implement/test the full-memory Homebrew host-title + ECS route, including cleanup
   and host fallback, only after the system backend build and hardware recovery tests.
4. Wire the existing HoldToDrag helper into the SDL touch path instead of starting a
   tile drag after a 12-pixel movement. Preserve the current gesture unit tests.

### Native touch: wire the tested long-press gesture into SDL input — 16 November 2026

- source/main.cpp now uses the existing desknx::touch::HoldToDrag helper; a stationary
  420 ms hold starts a tile drag, while movement beyond the tested 12-pixel threshold
  before the hold cancels the drag candidate. The SDL frame loop polls the hold timer
  so a finger does not need to move again after the hold delay.
- Drag motion and finger release are now bound to the initiating SDL_FingerID, avoiding
  a second finger moving or committing the first finger's tile.
- A short touch in edit mode selects a tile without immediately switching to drag.
- scripts/check-standalone-entrypoint.sh checks that the production SDL source actually
  calls both HoldToDrag::tick and observeMotion; tests/touch_gesture_test.cpp remains
  the host-tested behavior contract.
- Physical touchscreen test: NOT RUN; long-press timing and finger-ID behavior require
  validation on a Switch touchscreen.

- Cleanup after wiring long-press: removed the obsolete immediate-drag start coordinates and previous-selection locals; the gesture helper now owns the press origin and timing.

### HOME applet holder completion barrier — HomeNX lifecycle parity — 16 November 2026

- Audit found that projects/sSystem/source/LibraryApplet.cpp waited once for
  StateChangedEvent and then unconditionally called appletHolderJoin/Close. A
  StateChangedEvent can represent foreground/suspend state changes; it is not, by
  itself, proof that the library applet has finished.
- StopMenu now loops until appletHolderCheckFinished() confirms terminal completion.
  StateChangedEvent is only a wake-up hint. Each wait is bounded to 250 ms, the overall
  completion budget is 5 s, unexpected wait errors preserve the holder and schedule a
  retry, and a signalled-but-not-finished event backs off briefly rather than spinning.
- appletHolderJoin/Close are reachable only after the explicit finished check succeeds.
- scripts/check-home-applet-entrypoint.sh now guards the completion check.
- This adopts the lifecycle invariant used by HomeNX's library_applet_runner, without
  copying its GPL-3.0 source implementation.
- Physical HOME/menu lifecycle validation: NOT RUN; this needs HOME/game switching on
  a recoverable Switch setup.

### Коррекция guard для проверки Menu holder lifecycle — 16 ноября 2026

- CI runs 43774029088 (Build DeskNX) и 43774024893 (experimental HOME backend)
  остановились на статической проверке: guard искал appletHolderCheckFinished() в
  source/main.cpp, хотя исправление находится в projects/sSystem/source/LibraryApplet.cpp.
- Исправлен scope проверки: HOME UI guard продолжает проверять AppletType_LibraryApplet
  в source/main.cpp, а условие завершения holder теперь проверяется в
  projects/sSystem/source/LibraryApplet.cpp.
- Это не компиляционная ошибка и не ошибка самой логики; следующая CI-сборка
  должна проверить реальную компиляцию изменённого LibraryApplet.cpp и qlaunch backend.

- Corrected the StopMenu comment so it no longer describes StateChangedEvent as a completion barrier; only appletHolderCheckFinished() confirms terminal exit.

### Уточнение семантики libnx LibraryApplet StateChangedEvent — 16 ноября 2026

- Дополнительно проверен исходник libnx: appletHolderCheckFinished() — zero-timeout
  check LibraryApplet StateChangedEvent. appletHolderRequestExitOrTerminate()
  посылает RequestExit, ждёт event и при timeout посылает Terminate, но не ждёт
  последнего завершения после Terminate.
- Предыдущая формулировка, будто LibraryApplet StateChangedEvent может быть обычной
  сменой foreground/suspend, была слишком широкой и исправлена. Это относится к
  отдельному IApplicationAccessor event в Application.cpp; там проверяется IsCompleted.
- StopMenu теперь ждёт завершение после fallback Terminate и перепроверяет
  appletHolderCheckFinished() перед Join/Close. Убран лишний цикл 250-мс polling.
- Основание: libnx applet.h API contract и applet.c implementation для
  appletHolderRequestExitOrTerminate(), appletHolderCheckFinished() и appletHolderJoin().

### Коррекция HOME lifecycle guard после уточнения libnx semantics — 16 ноября 2026

- CI run 43774838316 остановился на guard, потому что он проверял старую форму
  цикла `while (!appletHolderCheckFinished(...))`, заменённую на bounded wait после
  RequestExitOrTerminate. Логика не потеряла обязательную проверку завершения.
- Guard теперь проверяет сам API-вызов appletHolderCheckFinished() в
  projects/sSystem/source/LibraryApplet.cpp и не привязывается к форме цикла/if.

### HomeNX SMI interactive IPC transport — 16 November 2026

HomeNX's audited projects/menu/src/smi_commands.hpp uses appletPushInteractiveOutData();
the daemon reads appletHolderPopInteractiveOutData() and waits on the lazily acquired
PopInteractiveOutDataEvent. Transwitch previously used ordinary appletPushOutData()
and polled appletHolderPopOutData() every 16 ms.

Changes:
- Protocol.hpp adds a thread-safe transport switch. A valid v2 InitialState enables
  interactive command storage; missing/legacy state retains the normal output queue.
- The system applet consumes InitialState before it sends MenuReady, so MenuReady and
  the first heartbeat cannot race ahead of transport negotiation.
- LibraryApplet exposes interactive pop and lazily gets PopInteractiveOutDataEvent.
  On acquisition failure it returns null; a handle-zero event is never awaited.
- Backend drains interactive commands first and then the legacy normal queue, so a
  new qlaunch backend can still accept a previously built v1 menu binary.
- Backend main loop waits on the command-ready event and AM applet-message event, with
  a bounded 16 ms timer fallback for liveness deadlines, app state and recovery chord.
- InitialState and backend-to-menu notifications stay on the original normal input
  queue in this commit; only menu-to-daemon command messages change transport.
- Static guard checks both ends of the protocol and the event wait.

Compatibility:
- New UI + new backend: interactive v2 path.
- New UI + old backend: no v2 InitialState => normal output queue.
- Old UI + new backend: backend drains both interactive and normal queues.

CI for this commit: queued. Physical HOME/command handoff test: NOT RUN.

### IPC transport capability marker — preserve compatibility with previous v2 backends — 16 November 2026

- Previous Transwitch backend releases already used protocol format Version=2 but
  still read commands from normal applet output storage. Therefore checking only the
  InitialState format version would incorrectly switch a new frontend to interactive
  storage when paired with an older v2 backend.
- Added a backend feature/build marker: new backend sets InitialState.backend_version
  to 0x000201; frontend opts into interactive transport only when both InitialState
  format is v2+ and backend_version >= 0x000201. Existing v2 backend marker 0x000200
  continues using normal output storage.
- This is a transport capability marker, not a binary protocol-layout change; the
  Header Version stays 2 and legacy command parsing remains unchanged.


## Frontend visual refresh — 17 November 2026

Scope: refine the existing native SDL2 frontend without changing the backend protocol,
app discovery, launch flow, workspace persistence format, or input hit targets.

Changes:
- Reworked the default Frosted Glass palette into a cooler graphite / blue-glass
  surface with clearer contrast and a consistent blue accent. Existing saved theme
  indices remain valid; other theme presets are preserved.
- Unified cards and dialogs around a restrained shadow, a real hairline outline,
  and a subtle top highlight instead of the previous full-surface white tint.
- Refined the top toolbar with a compact DeskNX mark, clearer title hierarchy,
  quieter tool controls, and a more balanced clock / battery cluster.
- Added an active workspace heading and item count above the grid, improving
  orientation without changing tile coordinates or touch targets.
- Refined full-bleed game / homebrew covers with small type chips, graduated
  translucent title overlays, and a shadow behind title text for legibility.
- Improved selected-tile emphasis and clarified the empty-workspace call to action.
- Refined the lower workspace dock: selected desktop is clearer, inactive tabs
  are quieter, and the active tab receives a small indicator.

Files:
- source/main.cpp — theme palette, shared card surface, cover treatment, home
  header, workspace heading / empty state, workspace dock.
- docs/AGENT_CHANGELOG.md — this record.

Validation:
- Source transformation checks: passed (all expected replacement blocks matched once).
- Cross-compile / host tests: not yet run for this commit.
- Physical Nintendo Switch visual / input test: NOT RUN.
- No claim is made that this visual refresh has been tested on hardware.
