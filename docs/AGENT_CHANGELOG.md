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

## DeskNX UX, NACP titles and diagnostics — 18 November 2026

- Рабочая ветка: `fix/deskux-polish-russian`. Изменения выполняются отдельными коммитами; в `main` пока не вносились.
- Устранена причина ID-only названий на HOS 21+: HomeNX нормализует raw-DEFLATE NACP title block формата TitlesDataFormat=1 (32 language slots) перед вызовом libnx; Transwitch получил совместимый нормализатор и host test.
- NACP/NRO artwork cache теперь принимает JPEG и PNG. Добавлены агрегированные и покарточные диагностические события по успешности metadata/name/icon resolution.
- Поиск больше не делает синхронный BUSY IPC round-trip перед `swkbdShow`: heartbeat остаётся отдельным потоком. Добавлены измерения `swkbdCreate` и полного времени показа клавиатуры.
- Виджеты получили разные компоновки для 1×1, 2×1/3×1 и 2×2/3×2. Исправлен расчёт количества дней в месяце для календаря.
- Добавлены русский интерфейс (по умолчанию), сохранение языка и стиля значков, три встроенных векторных обработки системных значков (Classic, Tabler-inspired outline, Material-style filled).
- SD-пакет теперь загружает Noto Sans, Open Sans и Noto Sans Mono с лицензиями SIL OFL. Шрифты содержат кириллицу.
- Расширено логирование раскладки, активного рабочего стола, истории, состояния кэша и времени сканирования медиапроводника. Вызов системного Nintendo Album applet измеряется отдельно; сам applet остаётся внешним компонентом Horizon.
- Аппаратная Switch-проверка обязательна. CI/host tests не подтверждают runtime на консоли, время запуска системной галереи, качество шрифтов на экране или фактическое отображение иконок.

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



## Полный аудит UI, каталога и производительности — 17 ноября 2026

### Каталог игр и обложки
- `source/catalog.cpp`: разделены проверки кеша названия и кеша изображения. Наличие корректного имени больше не пропускает чтение NACP, если обложка отсутствует или повреждена.
- Названия-заглушки вида Title ID / `Game <Title ID>` не считаются валидными метаданными и не закрепляются в постоянном кеше; при повторном сканировании выполняется повторная попытка получить название из NACP.
- JPEG-кеш проверяется до использования; повреждённый кеш удаляется и запрашивается заново. Сохранено ленивое декодирование и ограничение загрузки новых текстур на кадр.
- Существующий рендер обложек оставлен без переписывания: плитка заполняется изображением, подпись располагается поверх полупрозрачной плашки.

### Задержки и сохранение рабочих столов
- Переключение рабочего стола с геймпада, вкладки и селектора больше не вызывает синхронную запись сразу при каждом изменении фокуса: сохранение маркера откладывается на 550 мс после последнего переключения.
- Маркер рабочего стола публикуется атомарно и теперь требует один SD commit вместо двух. Предыдущий маркер сохраняется как резервная копия.
- Добавлен снимок последнего сохранённого маркера. Если layout и активный рабочий стол не изменились, повторная запись и commit пропускаются. Это также устраняет ненужные записи маркера при обычном сохранении плиток.
- При ошибке SD сохранение повторяется после паузы; полное сохранение раскладки перед запуском игры и при штатном завершении оставлено синхронным, чтобы не терять изменения.
- PDM playtime-запросы остаются вне навигационного кадра: запрос выполняется отдельным потоком после debounce выбора.

### Главное меню и инструменты
- В нативном HOME-интерфейсе верхняя панель приведена к четырём действиям: Gallery, Settings, Pads и Tools. Остальные действия вынесены в отдельную сетку Tools 3×2: App Library, Recent, Power, Edit Mode, Widgets и Rescan Library.
- Обновлены геометрия карточек, сенсорные hitbox и навигация с контроллера; для Tools добавлена отдельная пиктограмма.
- Убраны видимые заголовки DeskNX / Your Console / Your Space. Вместо брендинга в верхней части отображается текущий профиль. Системный виджет на рабочем столе теперь показывает версию прошивки, а не дублирует инструменты.
- Видимые подписи в React Diagnostics, настройках размещения и mock-каталоге приведены к Transwitch. Внутренние идентификаторы, имена директорий и формат старых конфигов не переименовывались ради совместимости.

### Профили и запуск игр
- Добавлен нативный список пользователей консоли с отображением nickname, выбранного профиля и переключением с тачскрина или контроллера.
- Для HOME applet используется `AccountServiceType_System`, для standalone-контекста — `AccountServiceType_Application`. Сервис и список пользователей инициализируются один раз, а после ошибки повторная попытка делается при открытии селектора, не каждый кадр.
- В системном режиме выбор отправляется через существующую IPC-команду `SelectUser`; UID становится текущим для последующих запусков.
- Перед запуском читается NACP `startup_user_account`: если приложение самостоятельно управляет выбором пользователя, передаётся `LaunchFlagNoPreuser`; в остальных случаях используется выбранный UID. Повторный выбор игрока перед каждым запуском не вызывается.

### Галерея обоев и оформление
- Нативный выбор фона теперь умеет переключаться между SD media library и Nintendo Album (`sdmc:/Nintendo/Album`). В корне медиатеки и альбома есть явный переключатель; X на контроллере и соответствующая сенсорная кнопка переключают источник.
- Разрешены только JPG/JPEG/PNG из двух указанных корней; пути с `..` и прочие каталоги не принимаются. Шрифты и пользовательские icon packs остаются в media library.
- В React customizer добавлен выбор локального изображения, проверка размера и типа, уменьшение до 1600×900, JPEG-сжатие и превью перед применением.
- В нативном интерфейсе расширена палитра до 12 акцентных цветов и добавлены темы Emerald, Rose и Arctic. В web-настройках доступны девять тем. Диапазон акцентов синхронизирован между моделью, сериализаторами и парсерами C++/TypeScript.
- В `tests/model_test.cpp` добавлена проверка сериализации/чтения верхней границы палитры (акцент и цвет плитки 11).

### Архитектурная граница
- Реальный каталог NS, запуск игр и выбор системного профиля находятся в нативном HOME library applet + system backend. Использована совместимая с текущим IPC/libnx реализация; HomeNX служил референсом поведения.
- Standalone NRO по-прежнему является офлайн WebKit-хостом React UI. В `src/services/switchHardwareService.ts` явно указано, что hardware bridge отсутствует и его вызовы используют mock fallback. Поэтому React-профили и браузерная галерея локальны и сами по себе не являются мостом к системным аккаунтам/NS. Для реального системного управления нужен HOME applet и совместимый system backend.
- Нативная галерея обоев поддерживает изображения, не видео. Nintendo Album должен быть доступен на SD-карте; наличие конкретной папки зависит от того, где консоль хранит Album.

### Повторная проверка
- CI на commit `4db11f84`: Build DeskNX и Build Nintendo Switch NRO — success; пройдены npm/Vite build, tile contract, host model/layout/catalog tests, IPC/protocol/state-machine tests, recovery tests, gesture tests, HomeNX workspace adapters, standalone NRO и HOME library applet build.
- CI на commit `ba671203`: Build DeskNX и Build Nintendo Switch NRO — success.
- CI на commit `51d29937`: Build DeskNX и Build Nintendo Switch NRO — success. Это включает однократный SD commit маркера рабочего стола.
- После дополнительного пропуска неизменившегося маркера и очистки видимых mock/UI-строк запущены новые сборки для актуального HEAD; их итог фиксируется отдельно после завершения workflow.
- Проверка на физической Nintendo Switch не выполнялась в рамках этого изменения. Успешная CI-компиляция не подтверждает runtime-поведение на конкретной прошивке/CFW.


## Диагностика вылета при запуске игры — 18 ноября 2026

### Что показали пользовательские логи
- Нативная SDL-сессия действительно доходит до \`launch.requested\` и \`standalone_ns_init rc=0\`; последняя запись перед завершением — \`game_request_begin\` для Title ID \`010015F005C8E000\`. В этом фрагменте нет \`game_request_end\`, поэтому он не доказывает, что приложение аварийно завершилось: запрос мог передать управление запускаемой игре. Для однозначного вывода нужен результат запуска/следующая запись после возврата.
- В этих же данных есть другой поток записей \`standalone\` с \`romfs_init\`, \`web_offline_create\`, \`web_show_begin/end\` и \`libnx_exit orderly_exit=1\`. Это отдельный WebKit-wrapper (\`src-switch/main.cpp\`), а не SDL launcher (\`source/main.cpp\`). Их нельзя считать одной последовательной сессией.
- Значения \`applet_type=0\` соответствуют \`AppletType_Application\`; libnx разрешает этому типу applet вызывать \`appletRequestLaunchApplication\`. Публичная функция принимает \`AppletStorage*\`, а не \`AccountUid*\`.

### Исправление
- В standalone-ветке \`source/main.cpp\` удалена ошибочная передача \`AccountUid*\` в \`appletRequestLaunchApplication\`. Вызов теперь соответствует libnx: \`appletRequestLaunchApplication(id, nullptr)\`.
- Standalone-ветка больше не закрывает NS-сессию через \`nsExit()\` после одной попытки запуска: эта сессия инициализируется при старте и используется каталогом. При ошибочном запросе каталог не должен оставаться с флагом «NS готов», но уже закрытым сервисом.
- Добавлены отдельные события \`applet_launch_request_begin/end\`, Title ID, applet type, код результата и явная политика пользователя \`system_preselected\`. Выбор UID доступен в интеграции HOME applet/backend; standalone использует предвыбранного системой пользователя.
- Добавлена цель \`make -C projects/DeskNXMenu standalone-check\`: CI дополнительно компилирует C++-источники без \`DESKNX_SYSTEM_APPLET\`, чтобы проверять ветку standalone, которая раньше не компилировалась стандартной сборкой HOME applet.

### Ограничения проверки
- Пользовательские логи относятся к запуску до фиксов debounce/сокращения SD commit и до текущего патча API; они фиксируют реальные задержки при множественных синхронных записях.
- Патч не может подтвердить запуск игры на реальном устройстве. До нового прогона на Switch нельзя считать проблему runtime закрытой. Если новый лог заканчивается на \`applet_launch_request_begin\`, запрос не вернулся; если есть \`applet_launch_request_end\`, код результата укажет, отклонила ли его система.
- Отдельная проверка standalone-ветки CI — это компиляция нативных источников, а не новый install-ready standalone NRO. Стандартный \`DeskNX.nro\` workflow по-прежнему собирает WebKit wrapper.


## Запись работ — 18 ноября 2026: перенос архитектуры HomeNX IconStreamer

Рабочая ветка: `port/homenx-async-icon-streamer`.
База: `16594497ca8586115898b5a274826798ddb1e81c` (main).
Референс HomeNX: `63205f0d2ad855822ed9f3c5c3201d4bd6245505` (master).

### Изменения

Файл: `source/IconCache.hpp`.

- Синхронные `stat()`, проверка заголовка, `IMG_Load()` и декодирование изображения удалены из пути `get()`, вызываемого отрисовкой.
- Добавлена одна фоновая SDL-служба с низким приоритетом для поиска файла, чтения, проверки размеров и декодирования.
- Результат передаётся в ограниченную очередь RGBA-изображений; создание SDL_Texture и освобождение GPU-ресурсов остаются в render thread.
- За один кадр загружается не более одной новой текстуры.
- Очередь запросов ограничена 48 элементами, очередь готовых изображений — двумя результатами; это ограничивает накопление CPU-side RGBA-буферов.
- GPU-кэш ограничен 64 текстурами и расчётным бюджетом 32 MiB, вытеснение выполняется по LRU.
- Смена icon pack увеличивает generation, сбрасывает старые очереди и не блокирует UI ожиданием декодера.
- Сохраняются проверки JPG/PNG, верхний лимит 1024×1024 и 2 MiB на сжатый файл; пользовательские файлы icon pack не удаляются при ошибке.
- При отсутствии или медленной загрузке изображения плитка сразу остаётся интерактивной и показывает прежний placeholder.

Архитектурный референс: HomeNX `projects/menu/src/launcher/IconStreamer.cpp` и `AppListLoader.cpp`. Реализация адаптирована под SDL2 и существующую модель текстур Transwitch; nxui/GPU-классы HomeNX напрямую не подключались.

### Проверки и ограничения

- Изменение внесено в отдельную ветку, main не изменён.
- Ожидается PR CI: host tests, standalone NRO compile, production HOME applet compile и native standalone compile-check.
- На момент записи cross-build и тесты на физической консоли ещё не подтверждены.
- Следующий обязательный тест на Switch: быстрый проход по 40–60 плиткам, повторная навигация, смена icon pack, холодный запуск без сети, длительная работа и возврат из игры.
- Бюджет 32 MiB — консервативный учёт размеров RGBA-текстур, а не точный замер выделений GPU-драйвера. Он требует измерения на железе.
- Очередь ограничивает накопленные результаты, но декодер всё ещё может временно выделять память SDL_image при обработке одного файла.


## Исправление CI — 18 ноября 2026: очистка устаревшего поля IconCache

- После перехода на асинхронную очередь путей поле `resolvedPaths_` больше не существует, но его очистка осталась в `shutdown()`.
- Cross-build HOME applet обнаружил это на этапе компиляции: `source/IconCache.hpp:120:9: error: 'resolvedPaths_' was not declared in this scope`.
- Удалён единственный устаревший вызов. Поведение очередей, worker thread и жизненного цикла текстур не менялось.
- До повторного успешного cross-build эту ветку нельзя считать собирающейся; запуск на Switch после этой правки ещё не подтверждён.


## Проверка пользовательских логов и CI — 18 ноября 2026

### Результат cross-build после фикса
- Коммит фикса: `5ac6eacaae678ca5b770c2f89064a369cef02088`; запись в changelog: `b303741d257d9ed8f62ce529f6acb453807c09eb`.
- PR workflow `Build DeskNX`, run `44252212096`: **success**. Успешно скомпилированы production HOME library applet и native standalone launch path; все host tests и упаковочные шаги прошли.
- Workflow `Build Nintendo Switch NRO`, run `44252212151`: **success**. Это отдельная сборка WebKit wrapper; её успех не проверяет нативный SDL IconCache.
- В первом прогоне HOME applet build падал на оставшемся `resolvedPaths_.clear()` в `IconCache::shutdown()`; ссылка удалена. Ошибка исправлена и повторная сборка прошла.
- Switch runtime и Atmosphère crash stability не подтверждены — cross-build не заменяет тест на реальной консоли.

### Разбор предоставленных логов
- `applet_type=0 is_applet=0` означает, что NRO запущен как Application, не в Album applet mode.
- `web_offline_create rc=0` и первый `web_show_end rc=0`, затем `libnx_exit orderly_exit=1` показывают успешное создание offline WebKit page и штатный возврат из `webConfigShow()` в этой сессии.
- Во второй и третьей сессиях лог заканчивается на `web_show_begin`; без `web_show_end` неизвестно, вернулся ли вызов. Сам по себе этот фрагмент не содержит Atmosphère fatal code/report и не доказывает аварийное завершение.
- Эти метки относятся к `src-switch/main.cpp` — standalone WebKit host. Они не относятся к `source/main.cpp`, SDL renderer или новому worker в `source/IconCache.hpp`.
- Чтобы подтвердить конкретный Atmosphère crash, нужен fatal report / код ошибки из экрана Atmosphère и совпадающие по времени `diagnostics.log*`. Не маркировать runtime-проблему закрытой по одному этому фрагменту.


## Исправление неверного standalone build target — 18 ноября 2026

### Что было неверно
- `make` собирал `src-switch/main.cpp`, который открывал HTML через `webOfflineCreate()` и `webConfigShow()`.
- Полноценный native SDL2 launcher уже находился в `source/main.cpp`, но в standalone target он не попадал. Он использовался HOME library applet и compile-only проверкой.
- Поэтому пользовательский журнал с `web_show_begin/end` был журналом WebKit wrapper, а не native launcher. Это расхождение возникло из-за неправильной точки входа сборки.

### Исправлено в native-only cutover
- Root `Makefile.switch` теперь компилирует production `source/*.cpp` и линкует SDL2, SDL2_image, SDL2_ttf, libcurl и libnx в самостоятельный `DeskNX.nro`.
- Удалён WebKit entry point `src-switch/main.cpp`, React/TypeScript UI, Vite/npm toolchain, JS lockfiles и сгенерированный HTML/JS/CSS RomFS.
- NRO больше не содержит website RomFS; проверка `scripts/check-nro-assets.py` подтверждает NACP и отклоняет RomFS/WebKit markers.
- `build.yml` и `build-nro.yml` больше не устанавливают Node-пакеты и не собирают web client. Главный CI теперь линкует actual native standalone NRO, а не только объектный compile check.
- HOME library applet и экспериментальный qlaunch backend оставлены как отдельный необязательный путь; самостоятельный NRO не запускает их и не нуждается в них.
- Сохранены Nextendo, каталог, PDM и асинхронный IconCache. Их worker threads работают внутри одного DeskNX процесса; это не внешние процессы.

### Проверка
- Ожидается новая CI сборка после этого cutover. Предыдущие green runs относились к WebKit standalone artifact плюс native HOME applet cross-build и не являются проверкой нового native NRO.
- До получения green CI и Switch hardware smoke test не распространять сборку как проверенную. На железе обязательны cold start, каталог, иконки, управление, запуск игры, возврат и лог Atmosphère при возможном сбое.

## Native NRO cutover — CI result, 18 November 2026

The native standalone build has now completed both CI workflows:
- Run 44256681598 — Build native Nintendo Switch NRO: success. make -j2 compiled and linked the SDL2/C++ launcher; NACP validation passed; RomFS and WebKit markers were absent; artifact DeskNX-native-nro uploaded (4,519,591 bytes).
- Run 44256681489 — Build DeskNX: success. All host tests passed; native NRO build passed; native HOME library applet build passed; SD-card package and applet artifact uploads passed.
- Available build artifacts from run 44256681489: DeskNX-native-nro, DeskNX-sdcard, DeskNX-menu-applet. The menu applet artifact is for the separate experimental HOME integration and is not the standalone install target.
- Corrected PR #40 title/body to describe both the native build cutover and HomeNX icon streaming.

### Current state and explicit limitations
- DeskNX.nro is now built from production source/*.cpp, not src-switch/main.cpp.
- The standalone build does not launch a browser/WebKit applet, website, local web server, qlaunch backend or another NRO. It is one native process; bounded SDL worker threads execute catalogue, icon decoding, Nextendo polling and on-demand play-stat work inside that process.
- The optional projects/sSystem qlaunch integration remains separate and experimental. It is neither linked into nor started by the standalone NRO.
- CI proves compilation, linking, static native-NRO checks and host tests. It does not prove that the latest NRO starts successfully on a physical Switch or that the previous Atmosphère crash is resolved. Required hardware checks: cold start, SDL renderer, game catalogue, icon decode, navigation, game launch/return and Atmosphère crash report inspection if it fails.

## Сведение PR и native-only merge — 18 ноября 2026

- PR #40 объединён в main (squash commit `32bc5ad848693bfbb4633e56dfe793c26db37641`).
- В main теперь самостоятельный DeskNX.nro на C++/SDL2 из `source/*.cpp`; WebKit wrapper, React/Vite UI и website RomFS удалены.
- CI runs `44257150856` (native NRO) и `44257150890` (полный build + host tests + HOME library applet) завершились успешно.
- README, FEATURES, DIAGNOSTICS, HOMENX_FRONTEND_PLAN и CLEANUP приведены к фактической native архитектуре.
- Закрыты устаревшие PR #5, #6, #7, #8, #10, #11, #34 и дублирующий native-cutover PR #38. У каждого оставлен комментарий с причиной. PR #40 — единственный интеграционный PR, теперь уже merged.
- Ветки закрытых PR не удалялись; полезные дополнительные изменения из #38 и NPDM audit #7 можно выборочно восстановить и отдельно проверить.
- Атмосферный/runtime crash на физической Switch не считается исправленным до аппаратного smoke test. CI подтверждает build/link/test, не выполнение на консоли.

## Исправления каталога, виджетов и сохранения UI — 18 ноября 2026

- Исправлена совместимость старого DESKNXCAT2 cache с актуальным набором из шести системных виджетов: при чтении кэша недостающие элементы Clock, Battery, System, Recent, Storage и Calendar восстанавливаются по типу, а не по общему числу строк.
- Установленные Title ID теперь публикуются в UI сразу после успешного ns_list/ns_views, до рекурсивного сканирования SD и чтения каждого NACP/иконки. Названия и обложки уточняются постепенно, пакетами по три игры.
- Добавлены контрольные события catalogue_seed и nro_scan_elapsed, чтобы отличать задержку SD-обхода от задержки получения NACP.
- Повторный поиск библиотеки больше не сбрасывает уже показанный каталог и не переинициализирует NS синхронно на UI-потоке. NS refresh и rescan выполняются в потоке каталога.
- Изменения рабочих столов и active-workspace marker теперь записываются через отдельный SDL worker; повторные правки объединяются в последний актуальный snapshot. Синхронный checked save сохранён перед запуском/завершением приложения.
- SD commits сериализованы отдельным mutex, а завершение фоновой записи отражается в диагностике async_save_finished.
- CI проверит native NRO и host tests. Физический тест на Switch всё ещё обязателен: особенно проверить старый catalogue cache, фильтр Widgets, появление 90 игр до завершения metadata scan, drag/resize/add/remove, переключение рабочих столов и перезапуск во время активного сохранения.


## 2026-11-18 — HomeNX plan, implementation tranche 1

- Hardened custom font replacement in `source/FontRenderer.hpp`: rejects unreadable, empty, oversized, and overlong font files; opens a candidate before releasing the active font; preserves the previous font on failure; and treats an empty path as an explicit return to the system shared font.
- Connected the production D-pad focus path in `source/main.cpp` to the existing HomeNX spatial grid adapter. Navigation now moves between tile origins, skips continuation/empty cells, chooses directional neighbours using tile geometry, and remains stationary at an edge. Malformed layouts retain a bounded fallback path.
- Existing workspace persistence format and manual drag/sort behaviour were intentionally left unchanged.
- Validation status: changes are committed to an isolated branch; CI/build and real-console font/controls checks remain required before merge.

- Added focused-tile priority to `IconCache`: the selected game's/homebrew cover is decoded before background tiles, and may evict the lowest-priority not-yet-started request when the bounded request queue is full. Queue and decoded-image memory bounds remain unchanged.

- Applied SDL's low-priority scheduling class to catalogue scanning, artwork-adjacent metadata queries (play statistics), Nextendo polling, and asynchronous layout writes. Heartbeat remains at its existing priority so backend liveness is not deprioritized. This uses the SDL portability layer rather than assuming a Switch-only native thread handle; CPU-core affinity remains deferred until verified against the exact devkitPro SDL/libnx runtime.

- Fixed the experimental system-backend CI workflow: its compressed-NACP host test linked `-lz` but did not install the host zlib development headers. Added the same explicit `zlib1g-dev` setup already used by the regular build workflow.


## 2026-11-18 — Persistence shutdown safety

- Fixed a lifecycle hole in the production NRO: asynchronous layout-save jobs are now drained unconditionally before renderer/SDL shutdown and SD unmount, not only when `layoutLoaded` is already true.
- The final synchronous snapshot is also written when the user changed a first-run workspace while the catalogue scan is still running. An untouched first-run placeholder layout remains deliberately unpersisted so the post-scan Games/Homebrew defaults are still generated.
- Added `ShutdownPersistence.hpp` and a host test covering loaded layouts, first-run untouched/modified layouts, save-blocked recovery, missing SD, and empty workspace sets.
- Added the new test to the regular CI workflow.

- Follow-up review corrected the shutdown-policy call-site argument order so the helper receives the actual `layoutSaveBlocked` flag (rather than its inverse). This keeps writes enabled during normal shutdown and disabled when recovery protection is active.


## 2026-11-18 — UX follow-up: portrait tiles, collision-safe moves and font smoothness

- Added portrait 2×3 as the next enlargement after 3×2; shrinking from 2×3 returns to 3×2. The production layout validator accepts the new footprint.
- Workspace movement now searches forward from the requested cell when the destination is occupied, wrapping through the grid and ignoring the moving tile's previous footprint. Cross-workspace placement uses the same ordered search. The HomeNX workspace provider and its edit regression tests now use the same collision-advance rule. The existing move action and drag hotkeys are now described more clearly in the workspace UI.
- Widget focus no longer fills the entire widget with a blue surface: its neutral background stays unchanged and focus is represented by a double contour. Game-cover labels and key widget headings/metrics are horizontally centered.
- Custom font data is read once into a bounded 16 MiB buffer. Remaining point sizes are opened gradually from memory; draw() no longer reopens the font from SD on first use. New text textures are budgeted per frame and cache keys include font generation/identity, preventing stale glyph reuse after changes.
- Added timing diagnostics around Nintendo's controller-support applet. That applet remains synchronous and Horizon-controlled; the log will show whether the observed delay is in the applet call itself.
- Cache audit: workspaces and the active workspace persist in `layout.tsv` / `active-workspace.dat`. The discovered game/homebrew catalogue is persisted in `cache/catalogue.tsv`, resolved game names in `cache/game-names.tsv`, and artwork in `cache/icons`. Startup warm-loads the saved catalogue while live NS/NRO scanning refreshes it; a stale warm cache remains available when required services are unavailable. The filtered/sorted visible catalogue view itself is RAM-side.
- Validation: host tests and CI must run after commit. Real Switch checks remain necessary for 2×3 resizing, moving across occupied cells/desktops, font-switch frame pacing, and controller-applet timing.

- CI follow-up: updated the HomeNX grid-edit regression test so 2×3 is treated as a supported portrait footprint; 4×1 remains rejected. The earlier failure was an obsolete assertion, not a product-code defect.
