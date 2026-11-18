# DeskNX — сбор диагностических логов

## Где находятся файлы

После запуска нативного DeskNX создаётся каталог:

`sdmc:/switch/DeskNX/`

При использовании экспериментального HOME backend дополнительно появляются журналы в `sdmc:/switch/DeskNX/logs/`.

Основные файлы:

- `diagnostics.log` и `diagnostics.log.1/.2` — запуск нативного приложения, SDL, renderer, сервисы Horizon, каталог, фоновые задачи и запуск игр/NRO.
- `catalogue.log` и `catalogue.log.old` — NS, ApplicationView, NACP, PDM, обнаружение игр, получение названий/иконок и сканирование NRO.

Кэш не сбрасывается при каждом запуске. Он хранится на SD-карте:

- `cache/catalogue.tsv` — список игр, NRO и системных виджетов;
- `cache/game-names.tsv` — разрешённые названия игр по Title ID;
- `cache/icons/` — изображения игр и Homebrew;
- `layout.tsv` / `layout.bak` — раскладки рабочих столов;
- `active-workspace.dat` / `.bak` — выбранный рабочий стол;
- `settings.ini` / `.bak` — тема, фон, шрифт, язык и стиль значков;
- `recent.tsv` / `.bak` — история запусков.

Ожидаемые новые события: `persistent_cache_state`, `catalogue_seed`, `game_artwork`, `game_metadata`, `layout loaded/saved`, `async_save_finished`, `keyboard search_create/search_complete`, `gallery launch_begin/return` и `media picker_scan_complete`.
- `logs/daemon.log`, `daemon.prev.log` — запуск отдельного экспериментального system backend.
- `logs/ecs.log` — операции ECS для экспериментального backend.

Нативный NRO не открывает WebKit и не запускает browser applet. В логах самостоятельного приложения больше не должно быть событий `web_offline_create`, `web_show_begin` или `web_show_end`.

## Как собрать логи

1. Запустите DeskNX из Homebrew Menu в Application Mode (через title override, когда это требуется конфигурацией консоли).
2. Воспроизведите проблему один раз: дождитесь интерфейса, откройте библиотеку или запустите игру, если ошибка связана с запуском.
3. После зависания подождите несколько секунд. Если интерфейс не восстановился, перезагрузите консоль.
4. Скопируйте папку `switch/DeskNX` с SD-карты.
5. Пришлите все существующие `diagnostics.log*` и `catalogue.log*`.

Для аварийного завершения приложите соответствующий файл из `atmosphere/crash_reports/` и точное время сбоя. Если Atmosphère показала Result code, сфотографируйте весь экран.

## Какие этапы фиксируются

- ранний вход в native `main()`, тип applet/application и монтирование SD;
- инициализация SDL, создание окна и renderer, VSync;
- версия HOS и результаты setsys/NS/account;
- количество записей в каталоге, чтение ApplicationView/NACP и поиск NRO;
- состояние Nextendo feed и результат обновления счётчиков;
- запуск игры/NRO и Result-коды Horizon API;
- редкие задержки кадров и завершение фоновых потоков.

Один процесс DeskNX использует внутренние SDL worker threads: каталог, декодирование иконок, сетевой feed и PDM-запрос по требованию. Эти потоки не являются отдельными NRO или Atmosphère sysmodule.

## Что намеренно не записывается

- пароль, Nintendo Account UID и другие учётные данные;
- содержимое сохранений и пользовательских файлов;
- нажатия кнопок и координаты касаний;
- содержимое файлов игр и Homebrew.

В журнале запуска игры может присутствовать её Title ID для диагностики конкретного отказа.

## Ограничения

Диагностический журнал не является дампом памяти и не может гарантированно сохранить причину kernel panic, мгновенного отключения питания или аварии до открытия SD. Если после обновлённой сборки нет `diagnostics.log`, важны Atmosphère crash report и точный код ошибки. Не запускайте много дополнительных циклов после первого воспроизведения сбоя.
