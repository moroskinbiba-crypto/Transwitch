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
   - projects/DeskNXMenu/source/main.cpp — отдельный native C++ UI library applet для system HOME.
   - source/main.cpp — большой legacy/experimental native UI translation unit. Он НЕ собирается текущими release Makefile: Makefile.switch собирает src-switch, а projects/DeskNXMenu/Makefile исключает source/main.cpp. Не считать его код поставляемой функциональностью.
2. Shared C++ файлы source/*.cpp (кроме source/main.cpp) собираются в HOME applet через projects/DeskNXMenu/Makefile. Standalone WebKit shell и C++ HOME applet — разные интерфейсные реализации.
3. В репозитории несколько persistence слоёв:
   - реальный HOME applet: projects/DeskNXMenu/source/LayoutStore.*, путь sdmc:/switch/DeskNX/layout.tsv;
   - legacy/experimental source/main.cpp: layout.tsv, отдельный active-workspace marker, settings/history;
   - source/PersistenceStore.*: JSON-хранилище с корнем sdmc:/01000000/00001000; host round-trip test проходит, но production UI call sites в текущих release Makefile paths не обнаружены. Не переключать production на него без миграции и integration tests.
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
- README ошибочно описывал source/main.cpp как общий UI standalone/system applet. По реальным Makefile это не так: standalone собирает src-switch, а HOME applet использует projects/DeskNXMenu/source/main.cpp.
- В проекте несколько persistence реализаций. Форматы и пути нельзя объединять без трассировки production call sites и миграционных тестов.

### Первый этап — repo hygiene и точность документации

В этом commit:
- очищен .gitignore;
- удалены отслеживаемые .vite cache и устаревший ZIP;
- добавлен scripts/check-repo-hygiene.sh;
- проверка hygiene включена в три Actions workflow;
- README и HOME integration docs исправлены, чтобы не заявлять, будто legacy source/main.cpp входит в релиз;
- добавлен этот журнал работ.

Проверки после commit вынесены в отдельную запись ниже.

### Следующие задачи (приоритет)

1. P0 — проаудировать фактический HOME applet LayoutStore: atomic save, recovery/backup, schema validation, active desktop restore, migration; добавить host tests на production serializer.
2. P0 — покрыть tests реальный lifecycle LibraryApplet::StopMenu, ECS registration/unregister, heartbeat, event/timeout handling и возврат к игре.
3. P1 — проверить installer/uninstaller при прерывании на каждом шаге, повторное выполнение и безопасное обновление без потери резервной копии.
4. P1 — профилировать каталог/artwork pipeline по времени и памяти; проверить lazy decode и texture eviction.
5. P1 — проверить native HOME UI: focus, grid navigation, drag/drop, touch hitboxes, пустые ячейки, увеличенные плитки, persist рабочего стола.
6. P2 — закрепить toolchain refs вместо плавающего devkitpro/devkita64:latest, не ломая target compatibility.
7. Hardware smoke test: cold boot, HOME open, launch title, HOME from game, launch NRO, HOME from NRO, repeated loops, sleep/wake, recovery. Записать HOS/Atmosphère/libnx/build ID и результат.

## Формат следующих записей

Для каждого завершённого этапа записывать дату, commit SHA, список файлов, причину, тесты/CI со ссылками, открытые риски и hardware checks (passed / not run). Предыдущие записи не удалять.
