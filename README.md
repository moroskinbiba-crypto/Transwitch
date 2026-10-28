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

Структура projects/sSystem содержит отдельный qlaunch-backend на основе Atmosphère/libstratosphere, а projects/DeskNXMenu собирает библиотечный applet. Backend должен получать команды меню через версионированный IPC, управлять запуском приложений и снова открывать меню после выхода из игры.

Standalone-сборка не нуждается в Atmosphère. Чтобы подготовить системные компоненты, нужны **совместимые** libnx, Atmosphère/libstratosphere и AtmosphereLibs:

    make menu-applet
    make system-backend
    make system-package
    make package-system

Основной workflow компилирует standalone NRO и библиотечный applet. Отдельный workflow **Build experimental DeskNX HOME backend** пытается собрать qlaunch backend против upstream libnx master и Atmosphère 1.12.0. Даже успешная сборка этих зависимостей **не доказывает** совместимость с прошивкой вашей консоли и не заменяет runtime-тесты на физическом Switch. Системный слой остаётся экспериментальным. Не устанавливайте его как повседневную замену HOME на основной консоли.

### Установка системного backend

После успешной сборки откройте Actions → Build experimental DeskNX HOME backend и скачайте DeskNX-system-experimental. **Распакуйте архив на ПК**, в отдельную папку: внутри должны лежать папки SdOut, scripts и docs. Не распаковывайте этот пакет прямо в корень SD-карты — он содержит подготовленные компоненты, а не автоматически включаемую установку.

Только после проверки сборки и на восстанавливаемой консоли запустите из распакованной папки:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk

Если на SD-карте уже есть override qlaunch, добавьте --backup-existing. Скрипт сохраняет старые файлы и пишет install manifest. Для восстановления на ПК:

    bash scripts/uninstall-system.sh --sd-root /path/to/sd-card --acknowledge-risk

Пакет хранит системные файлы внутри SdOut; сам qlaunch override копируется в Atmosphere только после явного запуска установщика. Не запускайте установщик без рабочего способа получить доступ к SD-карте и восстановить предыдущую конфигурацию Atmosphère. Подробности — в docs/HOME_INTEGRATION.md.

## Структура

- source/main.cpp — общий UI; standalone и system-applet launch paths переключаются compile-time define.
- projects/common/include/desknx/Protocol.hpp — версионированный IPC между UI и системным процессом.
- projects/DeskNXMenu/ — библиотечный applet и NPDM для запуска через backend.
- projects/sSystem/ — экспериментальный Atmosphère qlaunch backend, lifecycle приложений и ECS.
- scripts/install-system.sh и scripts/uninstall-system.sh — отдельные установочные скрипты с проверками и резервированием.
- docs/HOME_INTEGRATION.md — схема интеграции, сборка, диагностика и recovery.
- THIRD_PARTY.md — заимствования и лицензии.

## Качество и проверка

Мы ориентируемся на инженерные практики sLaunch: разделение UI/backend, IPC с версией и проверкой размеров сообщения, логирование, повторные попытки запуска меню, отдельные Makefile и осознанный rollback. Это **не** означает, что DeskNX уже реализовал весь функционал или уровень тестирования sLaunch. Текущий system-backend — стартовая реализация, требующая компиляции с целевым Atmosphère, ревью lifecycle и проверок на реальном железе.

Standalone исходный код распространяется по лицензии из LICENSE; заимствованные system components и их лицензии описаны в THIRD_PARTY.md.
