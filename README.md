# DeskNX

DeskNX — кастомный рабочий стол для Nintendo Switch. В проекте разделяются **standalone Homebrew-приложение** и экспериментальная **интеграция системного HOME через Atmosphère**. Это разные сборки: первая запускается вручную из Homebrew Menu, вторая предназначена для замены qlaunch и требует отдельного системного окружения.

## Что сделано в UI-прототипе

- Рабочие столы, включая создание, переименование и удаление.
- Сетка 6 × 3 с сохраняемыми пустыми ячейками.
- Каталог установленных игр и Homebrew NRO на SD-карте.
- Перетаскивание с контроллера и сенсорного экрана, перенос между столами.
- Размеры плиток 1×1, 2×1, 2×2 и 3×2, без пересечения элементов.
- Часы, заряд батареи, системный виджет и список последних запусков.
- Сохранение раскладки в sdmc:/switch/DeskNX/layout.tsv.
- Native C++/SDL2 UI, сборка .nro через GitHub Actions.

## Сборка standalone NRO

Требуются devkitPro, devkitA64, libnx и SDL2 for Switch.

    make -j2

После успешной сборки откройте Actions → **Build DeskNX NRO and HOME applet**. Артефакт DeskNX-nro — обычное Homebrew-приложение; DeskNX-sdcard содержит его в структуре SD-карты.

## Экспериментальная интеграция HOME

Структура projects/sSystem содержит отдельный qlaunch-backend на основе Atmosphère/libstratosphere, projects/DeskNXMenu собирает библиотечный applet, а projects/hbloader — отдельный target-aware загрузчик Homebrew. В system-mode запуск игры или NRO передаётся backend через версионированный IPC. Backend освобождает меню-слот, обслуживает NRO через ECS, запускает его через hbloader и возвращает DeskNX после завершения.

Standalone-сборка не нуждается в Atmosphère. Чтобы подготовить системные компоненты, нужны **совместимые** libnx, Atmosphère/libstratosphere и AtmosphereLibs:

    make menu-applet
    make system-backend
    make system-package
    make package-system

Основной workflow компилирует standalone NRO и библиотечный applet. Отдельный workflow **Build experimental DeskNX HOME backend** пытается собрать qlaunch backend против выбранных refs libnx и Atmosphère (по умолчанию — Atmosphère 1.12.1 и зафиксированный commit libnx). Даже успешная сборка этих зависимостей **не доказывает** совместимость с прошивкой вашей консоли и не заменяет runtime-тесты на физическом Switch. Системный слой остаётся экспериментальным. В этой версии предусмотрен только applet-mode запуск Homebrew; запуск NRO как полноценного приложения с выделенной игрой-донором намеренно отключён. Не устанавливайте его как повседневную замену HOME на основной консоли.

### Установка системного backend

После успешной сборки откройте Actions → Build experimental DeskNX HOME backend и скачайте DeskNX-system-experimental. **Распакуйте архив на ПК**, в отдельную папку: внутри должны лежать папки SdOut, scripts и docs. Не распаковывайте этот пакет прямо в корень SD-карты — он содержит подготовленные компоненты, а не автоматически включаемую установку.

Только после проверки сборки и на восстанавливаемой консоли запустите из распакованной папки:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk

Если на SD-карте уже есть override qlaunch, добавьте --backup-existing. Скрипт сохраняет старые файлы и пишет install manifest. Для восстановления на ПК:

    bash scripts/uninstall-system.sh --sd-root /path/to/sd-card --acknowledge-risk

Пакет хранит системные файлы внутри SdOut, включая hbloader в switch/DeskNX/bin/hbloader; сам qlaunch override копируется в atmosphere только после явного запуска установщика. Если DeskNX не может запустить ни один из проверяемых HOME applet-слотов и нет запущенной игры, backend также предусматривает аварийное восстановление: удерживать L+R+ZL+ZR+Plus+Minus+D-pad Down три секунды. Оно действует только при наличии точного маркера DeskNX и переименовывает активный override перед запросом перезагрузки. Это дополнительная страховка, а не гарантия восстановления при любой поломке. Не запускайте установщик без рабочего способа получить доступ к SD-карте и восстановить предыдущую конфигурацию Atmosphère. Подробности — в docs/HOME_INTEGRATION.md.

## Технический аудит

Актуальный статический аудит (включая успешную сборку system bundle #128, не заменяющую тесты на физической консоли), исправления, блокирующие риски и порядок дальнейших проверок: [docs/AUDIT_2026-10-30.md](docs/AUDIT_2026-10-29.md). Системный HOME backend остаётся экспериментальным до успешной сборки и проверки на физической консоли.

## Структура

- source/main.cpp — общий UI; standalone и system-applet launch paths переключаются compile-time define.
- projects/common/include/desknx/Protocol.hpp — версионированный IPC между UI и системным процессом.
- projects/DeskNXMenu/ — библиотечный applet и NPDM для запуска через backend.
- projects/hbloader/ — target-aware загрузчик NRO, запускаемый backend через ECS.
- projects/sSystem/ — экспериментальный Atmosphère qlaunch backend, lifecycle приложений и ECS.
- scripts/install-system.sh и scripts/uninstall-system.sh — отдельные установочные скрипты с проверками и резервированием.
- docs/HOME_INTEGRATION.md — схема интеграции, сборка, диагностика и recovery.
- docs/COMPAT.md — матрица проверенных на железе версий прошивки/Atmosphère; всё остальное считается непроверенным.
- THIRD_PARTY.md — заимствования и лицензии.

## Качество и проверка

Мы ориентируемся на инженерные практики sLaunch: разделение UI/backend, IPC с версией и проверкой размеров сообщения, логирование, повторные попытки запуска меню, отдельные Makefile и осознанный rollback. Это **не** означает, что DeskNX уже реализовал весь функционал или уровень тестирования sLaunch. Текущий system-backend — стартовая реализация, требующая компиляции с целевым Atmosphère, ревью lifecycle и проверок на реальном железе.

Standalone исходный код распространяется по лицензии из LICENSE; заимствованные system components и их лицензии описаны в THIRD_PARTY.md.
