# DeskNX / Transwitch

DeskNX — кастомный рабочий стол и лаунчер для Nintendo Switch. Проект содержит две
раздельные цели: обычное standalone Homebrew-приложение **DeskNX.nro** и
экспериментальную системную интеграцию HOME через Atmosphère. У них общий native UI,
но разные режимы запуска и разные системные возможности.

## Native UI

Standalone NRO и production HOME library applet используют один интерфейс из
`source/main.cpp` на C++/SDL2. Standalone-сборка не запускает Nintendo Web Applet и
не содержит HTML, React, WebKit или RomFS. В NRO встраиваются NACP-метаданные и иконка;
отрисовка, ввод, каталог приложений и рабочие столы работают внутри native-процесса.

В текущем интерфейсе реализованы:

- Несколько рабочих столов с сохранением активного стола и раскладок.
- Сетка 6 × 3 с сохраняемыми пустыми ячейками.
- Каталог установленных игр и Homebrew NRO на SD-карте.
- Перетаскивание с контроллера и сенсорного экрана, перенос между столами.
- Размеры плиток 1×1, 2×1, 2×2 и 3×2 без пересечения элементов.
- Виджеты: часы, батарея, системные инструменты, недавние запуски, место на SD и календарь.
- Отдельные настройки DeskNX: пять цветовых тем, фоны JPG/PNG, шрифты TTF/OTF/TTC и наборы иконок.
- Пользовательские иконки и обложки по Title ID или имени NRO.
- Диагностика запуска, сканирования каталога и системных запросов.

Подробности персонализации: [docs/CUSTOMIZATION.md](docs/CUSTOMIZATION.md).

## Сборка standalone NRO

Требуются devkitPro/devkitA64, libnx и Switch-порты SDL2, SDL2_image и SDL2_ttf.

```sh
make -j2
```

Сборка использует production UI из `source/main.cpp` и поддерживающие модули
`source/*.cpp` и `projects/DeskNXMenu/source/*.cpp` (prototype `main.cpp`
исключён). Основной артефакт — `DeskNX.nro`; совместимая копия — `desknx.nro`.

GitHub Actions публикует артефакты **DeskNX-nro** и **DeskNX-sdcard**. CI проверяет
NRO0/ASET/NACP и отдельно убеждается, что в standalone NRO нет RomFS. Успешная
компиляция не подтверждает запуск на физической консоли — после сборки нужен
hardware smoke test.

## Экспериментальная интеграция HOME

Структура `projects/sSystem` содержит отдельный qlaunch-backend на основе
Atmosphère/libstratosphere, `projects/DeskNXMenu` собирает библиотечный applet, а
`projects/hbloader` — отдельный target-aware загрузчик Homebrew. В system-mode
запуск игры или NRO передаётся backend через версионированный IPC. Backend
обслуживает ECS, запускает NRO через hbloader и возвращает DeskNX после завершения.

Standalone-сборка не нуждается в Atmosphère. Для системных компонентов нужны
совместимые libnx, Atmosphère/libstratosphere и AtmosphereLibs:

```sh
make menu-applet
make system-backend
make system-package
make package-system
```

Основной workflow собирает native standalone NRO и библиотечный applet. Отдельный
workflow **Build experimental DeskNX HOME backend** пытается собрать qlaunch backend
против выбранных refs libnx и Atmosphère. Даже успешная сборка не доказывает
совместимость с прошивкой консоли и не заменяет runtime-тесты на физическом Switch.
Системный слой остаётся экспериментальным. Не устанавливайте его как повседневную
замену HOME на основной консоли без проверенного восстановления предыдущего qlaunch.

### Установка системного backend

После успешной сборки откройте Actions → **Build experimental DeskNX HOME backend**
и скачайте `DeskNX-system-experimental`. Распакуйте архив на ПК в отдельную папку:
внутри должны лежать `SdOut`, `scripts` и `docs`. Не распаковывайте этот пакет
непосредственно в корень SD-карты.

Только после проверки сборки и на восстанавливаемой консоли запустите:

```sh
bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk
```

Если на SD-карте уже есть qlaunch override, добавьте `--backup-existing`. Для
восстановления на ПК:

```sh
bash scripts/uninstall-system.sh --sd-root /path/to/sd-card --acknowledge-risk
```

Подробности, ограничения и recovery: [docs/HOME_INTEGRATION.md](docs/HOME_INTEGRATION.md).

## Диагностика

Standalone NRO и native HOME applet записывают журналы на SD-карту в
`sdmc:/switch/DeskNX/`:

- `diagnostics.log` и две ротируемые копии — запуск, libnx, SDL/renderer, системные
  сервисы, ввод, ошибки и завершение сессии;
- `catalogue.log` и одна ротируемая копия — обнаружение игр, NS, NACP, PDM, NRO,
  кэш и длительность сканирования.

Системный backend дополнительно создаёт `logs/daemon.log`, `logs/ecs.log` и
резервные журналы. После воспроизведения ошибки приложите существующие
`diagnostics.log*`, `catalogue.log*` и относящиеся к проблеме backend logs.
Папку кэша и сами игры присылать не нужно. Инструкция: [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md).

## Структура

- `source/main.cpp` — единый production native UI для standalone NRO и HOME applet.
- `source/*.cpp` — каталог, persistence, layout, диагностика и вспомогательные модули.
- `projects/DeskNXMenu/source/*.cpp` — supporting UI modules; prototype `main.cpp` исключён из сборок.
- `projects/common/include/desknx/` — IPC, backend state-machine и общие правила.
- `projects/DeskNXMenu/` — библиотечный applet и NPDM для запуска через backend.
- `projects/hbloader/` — target-aware загрузчик NRO.
- `projects/sSystem/` — экспериментальный Atmosphère qlaunch backend.
- `scripts/install-system.sh` и `scripts/uninstall-system.sh` — установщик/восстановление системного пакета.
- `docs/AGENT_CHANGELOG.md` — журнал изменений и точка продолжения работ.

## Проверка и ограничения

CI запускает host-тесты модели, раскладки, IPC, backend state machine, recovery,
touch gestures, workspace adapters и persistence, затем собирает standalone NRO и
HOME library applet. CI не может проверить NS/account/PDM на реальной консоли,
физический touch, sleep/wake или HOME takeover. Не считать успешную компиляцию
подтверждением аппаратной работоспособности.

Standalone исходный код распространяется по лицензии из LICENSE; заимствования и
лицензии системных компонентов описаны в [THIRD_PARTY.md](THIRD_PARTY.md).
