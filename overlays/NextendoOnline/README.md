# Nextendo Online — Tesla overlay for Transwitch

Отдельный Tesla/Ultrahand `.ovl`-оверлей для просмотра онлайна Nextendo Network. Он собирается как самостоятельный модуль и **не подключается к UI, процессу или сборке DeskNX/Transwitch**.

## Что показывает

- Общее количество игроков по публичной статистике Nextendo.
- Список игр с активными игроками, по убыванию онлайна.
- Названия установленных игр из их NACP; для неизвестных игр показывает Title ID.
- Автоматическое обновление каждые 15 секунд и ручное обновление.
- Кэш последнего успешного ответа на SD-карте; при ошибке сети старые данные не заменяются нулями.
- Ограничение размера ответа, проверка TLS и общий лимит времени сетевого опроса.

## Управление

- **A** на строке «Игроков онлайн» — обновить.
- **Y** — обновить из любой позиции.
- **B** — безопасно закрыть оверлей после остановки сетевого потока.
- Стики / D-Pad — прокрутка списка.

## Установка

Скопировать `nextendo-online.ovl` в:

```text
sdmc:/switch/.overlays/nextendo-online.ovl
```

Нужен Tesla-совместимый overlay loader (например, nx-ovlloader / Ultrahand Overlay).

Кэш хранится отдельно от DeskNX:

```text
sdmc:/switch/NextendoOnline/online-cache.tsv
```

## Сборка

Нужны devkitPro/devkitA64, libnx, Switch portlibs (включая libcurl) и libtesla с Switch-совместимыми патчами из используемой Tesla-реализации. CI автоматически получает её из Doutea/Nextendo-status-ovl и применяет небольшой GCC 15 compatibility patch из scripts/patch-libtesla-gcc15.py.

```bash
git clone --depth 1 https://github.com/Doutea/Nextendo-status-ovl.git /tmp/nextendo-status-ovl
mkdir -p libs
cp -R /tmp/nextendo-status-ovl/libs/libtesla libs/libtesla
python3 scripts/patch-libtesla-gcc15.py libs/libtesla/include/tesla.hpp
make -j2
cp nextendo-online.nro nextendo-online.ovl
```

Workflow `Build Nextendo Online Overlay` собирает `.ovl` независимо от основного DeskNX workflow и публикует артефакт `NextendoOnline-ovl`.

## Данные Nextendo

Используются публичные endpoint-ы, которые уже опрашивает Transwitch: `/api/online-counts`, `/service-catalog/v1/online-counts` и отдельные `/game-stats/*` feeds. Для названий игр оверлей читает локальные NACP только у игр, присутствующих в полученной статистике.

## Лицензия

Оверлей использует libtesla (GPL-2.0-or-later). Код этого отдельного overlay target распространяется на условиях GPL-2.0-or-later. Лицензия не меняет лицензию основного DeskNX-кода. Полный текст GPL-2.0 включается в CI-артефакт из `libs/libtesla/LICENSE`.
