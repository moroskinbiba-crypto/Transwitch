# DeskNX / Transwitch

DeskNX — нативный домашний экран и лаунчер для Nintendo Switch на C++/SDL2. Основное приложение компилируется непосредственно из `source/main.cpp` в самостоятельный NRO. Это не WebKit-обёртка и не браузер с React-интерфейсом.

## Что запускается на консоли

**DeskNX.nro** — одно нативное Homebrew-приложение. Оно само создаёт SDL2-окно, рисует интерфейс, читает ввод, получает каталог установленных игр через libnx/NS, сканирует NRO на SD-карте и запускает выбранные приложения через Horizon API.

В runtime нет WebKit, HTML, JavaScript, React, RomFS с сайтом или отдельного browser applet. В NRO встроены нативный код и NACP-метаданные; UI и ресурсы загружаются из файловой системы SD.

Внутри одного процесса используются ограниченные фоновые потоки для задач, которые не должны блокировать отрисовку: каталог, декодирование иконок, Nextendo presence и PDM-статистика по запросу. Они не являются отдельными приложениями или системными процессами. Сетевой опрос Nextendo отложен после старта и не блокирует открытие рабочего стола.

## Нативные функции

- До 12 независимых рабочих столов с пустыми ячейками и сохранением активного рабочего стола.
- Сетка 6 × 3, размещение, перемещение и изменение размеров плиток.
- Каталог установленных игр и Homebrew NRO.
- Поиск, фильтрация и сортировка библиотеки.
- Иконки и обложки из NACP/SD и пользовательского icon pack.
- Асинхронная очередь декодирования изображений с ограничением памяти и GPU-кэша.
- Виджеты: часы, батарея, системные инструменты, недавние запуски, SD storage и календарь.
- Темы, шрифты с кириллицей, русский интерфейс, обои и индивидуальное оформление рабочих столов.
- Адаптивные макеты шести виджетов для компактных, широких и высоких плиток.
- Три встроенных стиля системных значков и пользовательские наборы игровых обложек.
- Nextendo: реальные онлайн-счётчики по играм; отсутствие данных не показывается как ноль.
- PDM playtime/launch statistics по выбранной игре.
- Сенсорное управление и управление Joy-Con/Pro Controller.
- Атомарное сохранение раскладки и резервные копии настроек.

Подробнее: [docs/CUSTOMIZATION.md](docs/CUSTOMIZATION.md), [docs/FEATURES.md](docs/FEATURES.md), [docs/UX_UI_GUIDELINES.md](docs/UX_UI_GUIDELINES.md), [docs/HOMENX_PORT_STATUS.md](docs/HOMENX_PORT_STATUS.md).

## Сборка самостоятельного NRO

Нужны devkitPro/devkitA64, libnx и Switch portlibs SDL2, SDL2_image, SDL2_ttf и libcurl.

    make -j2

Результат: `DeskNX.nro` (также создаётся совместимая копия `desknx.nro`). В GitHub Actions артефакт называется **DeskNX-native-nro**; пакет SD-карты — **DeskNX-sdcard**.

Основной build workflow также собирает библиотечный HOME applet из того же нативного `source/main.cpp`. Этот артефакт предназначен для экспериментальной интеграции HOME и сам по себе не является готовым к установке пакетом.

## Необязательная системная интеграция HOME

В репозитории остаётся отдельная экспериментальная система для замены qlaunch: `projects/sSystem`, библиотечный HOME applet `projects/DeskNXMenu` и target-aware loader `projects/hbloader`. Она нужна только для сценария замены штатного HOME и не требуется для запуска самостоятельного DeskNX.nro.

Системный backend остаётся экспериментальным. Установка qlaunch override на основную консоль не рекомендуется без проверенного способа отката и аппаратных тестов. Сборка компонентов не подтверждает совместимость с конкретной версией HOS/Atmosphère.

Схема, сборка, recovery и ограничения: [docs/HOME_INTEGRATION.md](docs/HOME_INTEGRATION.md), [docs/COMPAT.md](docs/COMPAT.md).

## Диагностика

Журналы записываются в `sdmc:/switch/DeskNX/`:

- `diagnostics.log*` — native startup, SDL renderer, services, каталог, фоновые задачи и запуск игр/NRO.
- `catalogue.log*` — NS/NACP/PDM и сканирование домашнего каталога.
- `logs/daemon.log*` и `logs/ecs.log*` — только при использовании экспериментального system backend.

После сбоя пришлите все существующие `diagnostics.log*` и `catalogue.log*`. Для фатального завершения дополнительно нужен соответствующий report из `atmosphere/crash_reports/`. Инструкция: [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md).

## Структура

- `source/main.cpp` — единственная production SDL2 UI entry point для NRO и HOME applet.
- `source/catalog.cpp` — NS/NACP catalogue, NRO scanning and persistent SD cache. HOS 21+ compressed NACP title blocks are normalized before resolving names.
- `source/IconCache.hpp` — bounded asynchronous PNG/JPEG icon decoder and render-thread texture cache.
- `source/NextendoPlayers.cpp` — Nextendo online presence feed.
- `projects/DeskNXMenu/` — HOME library applet target, собранный из общего native UI.
- `projects/sSystem/` — необязательный экспериментальный qlaunch backend.
- `projects/hbloader/` — загрузчик для экспериментальной системной интеграции.
- `projects/common/` — IPC и backend state machine.
- `tests/` — host tests для модели, раскладки, IPC и persistence.
- `docs/AGENT_CHANGELOG.md` — журнал изменений и контрольная точка для продолжения разработки.

Лицензии и происхождение адаптированного кода указаны в [THIRD_PARTY.md](THIRD_PARTY.md).
