#pragma once

#include <array>
#include <string>
#include <utility>

namespace desknx::i18n {

inline std::string translate(const std::string& value, bool russian) {
    if (!russian || value.empty())
        return value;

    static const std::pair<const char*, const char*> phrases[] = {
        {"GALLERY", "ГАЛЕРЕЯ"}, {"SETTINGS", "НАСТРОЙКИ"},
        {"PADS", "ГЕЙМПАДЫ"}, {"TOOLS", "ИНСТРУМЕНТЫ"},
        {"LAUNCHER SETTINGS", "НАСТРОЙКИ ЛАУНЧЕРА"},
        {"Appearance, widgets and workspace behaviour", "Внешний вид, виджеты и рабочие столы"},
        {"CHOOSE BACKGROUND IMAGE", "ВЫБРАТЬ ОБОИ"},
        {"CHOOSE CUSTOM FONT", "ВЫБРАТЬ ШРИФТ"},
        {"CHOOSE ICON PACK", "ВЫБРАТЬ НАБОР ИКОНОК"},
        {"CHANGE COLOR THEME", "СМЕНИТЬ ЦВЕТОВУЮ ТЕМУ"},
        {"ADD WIDGET TO WORKSPACE", "ДОБАВИТЬ ВИДЖЕТ"},
        {"RESCAN GAME LIBRARY", "ПЕРЕСKАНИРОВАТЬ БИБЛИОТЕКУ"},
        {"WORKSPACE MANAGEMENT", "УПРАВЛЕНИЕ РАБОЧИМИ СТОЛАМИ"},
        {"TOGGLE EDIT / ARRANGE MODE", "РЕЖИМ РЕДАКТИРОВАНИЯ"},
        {"RESET APPEARANCE", "СБРОСИТЬ ОФОРМЛЕНИЕ"},
        {"LANGUAGE: RUSSIAN", "ЯЗЫК: РУССКИЙ"},
        {"LANGUAGE: ENGLISH", "ЯЗЫК: АНГЛИЙСКИЙ"},
        {"ICON STYLE", "СТИЛЬ ИКОНОК"},
        {"Classic", "Классический"}, {"Tabler outline", "Контурный"},
        {"Material filled", "Заливка"},
        {"CHOOSE WALLPAPER", "ВЫБЕРИТЕ ОБОИ"},
        {"CHOOSE DESKTOP BACKGROUND", "ФОН РАБОЧЕГО СТОЛА"},
        {"CHOOSE FONT", "ВЫБЕРИТЕ ШРИФТ"},
        {"CHOOSE ICON PACK", "ВЫБЕРИТЕ НАБОР ИКОНОК"},
        {"NINTENDO ALBUM", "АЛЬБОМ NINTENDO"},
        {"MEDIA LIBRARY", "МЕДИАБИБЛИОТЕКА"},
        {"NO SUPPORTED FILES FOUND", "ПОДХОДЯЩИЕ ФАЙЛЫ НЕ НАЙДЕНЫ"},
        {"CREATE A FOLDER HERE FOR YOUR ICON PACK", "СОЗДАЙТЕ ЗДЕСЬ ПАПКУ НАБОРА ИКОНОК"},
        {"COPY JPG / PNG OR TTF / OTF FILES INTO THIS FOLDER", "СКОПИРУЙТЕ СЮДА JPG / PNG ИЛИ TTF / OTF"},
        {"UP / DOWN SELECT     A APPLY     B CLOSE", "ВВЕРХ / ВНИЗ ВЫБОР     A ПРИМЕНИТЬ     B ЗАКРЫТЬ"},
        {"B  BACK", "B  НАЗАД"}, {"CANCEL", "ОТМЕНА"},
        {"GALLERY UNAVAILABLE", "ГАЛЕРЕЯ НЕДОСТУПНА"},
        {"PADS MENU UNAVAILABLE", "МЕНЮ ГЕЙМПАДА НЕДОСТУПНО"},
        {"APP LIBRARY", "БИБЛИОТЕКА ПРИЛОЖЕНИЙ"},
        {"INSTALLED GAMES AND HOMEBREW", "УСТАНОВЛЕННЫЕ ИГРЫ И HOMEBREW"},
        {"RECENT", "НЕДАВНИЕ"}, {"RECENTLY LAUNCHED APPS", "НЕДАВНО ЗАПУЩЕННЫЕ"},
        {"POWER", "ПИТАНИЕ"}, {"SLEEP, REBOOT OR SHUT DOWN", "СОН, ПЕРЕЗАГРУЗКА И ВЫКЛЮЧЕНИЕ"},
        {"EDIT MODE", "РЕДАКТИРОВАНИЕ"},
        {"MOVE AND RESIZE DESKTOP TILES", "ПЕРЕМЕЩЕНИЕ И ИЗМЕНЕНИЕ РАЗМЕРА ПЛИТОК"},
        {"WIDGETS", "ВИДЖЕТЫ"},
        {"ADD CLOCK, STORAGE AND SYSTEM INFO", "ЧАСЫ, ПАМЯТЬ И СИСТЕМНАЯ ИНФОРМАЦИЯ"},
        {"RESCAN LIBRARY", "ОБНОВИТЬ БИБЛИОТЕКУ"},
        {"REFRESH INSTALLED APPLICATIONS", "ОБНОВИТЬ СПИСОК ПРИЛОЖЕНИЙ"},
        {"SELECT ANY OF YOUR", "ВЫБЕРИТЕ ОДИН ИЗ"},
        {"WORKSPACES", "РАБОЧИХ СТОЛОВ"},
        {"THIS DESKTOP IS EMPTY", "ЭТОТ РАБОЧИЙ СТОЛ ПУСТ"},
        {"Add games, homebrew and widgets to get started.", "Добавьте игры, homebrew и виджеты."},
        {"Y  ADD APPS", "Y  ДОБАВИТЬ"},
        {"+  CREATE ANOTHER DESKTOP", "+  СОЗДАТЬ РАБОЧИЙ СТОЛ"},
        {"CLOCK", "ЧАСЫ"}, {"BATTERY", "БАТАРЕЯ"},
        {"SYSTEM INFO", "СИСТЕМА"}, {"SD CARD STORAGE", "ПАМЯТЬ SD"},
        {"TODAY", "СЕГОДНЯ"}, {"RECENTLY OPENED", "НЕДАВНО ЗАПУСКАЛИСЬ"},
        {"NOTHING HERE YET", "ПОКА НИЧЕГО НЕТ"},
        {"HISTORY ON THIS CONSOLE", "ИСТОРИЯ ЭТОЙ КОНСОЛИ"},
        {"SD STATUS UNAVAILABLE", "SD-КАРТА НЕДОСТУПНА"},
        {"FIRMWARE", "ПРОШИВКА"},
        {"FILTER: ALL", "ФИЛЬТР: ВСЁ"}, {"FILTER: GAMES", "ФИЛЬТР: ИГРЫ"},
        {"FILTER: HOMEBREW", "ФИЛЬТР: HOMEBREW"}, {"FILTER: WIDGETS", "ФИЛЬТР: ВИДЖЕТЫ"},
        {"SORT: NAME", "СОРТИРОВКА: ИМЯ"}, {"SORT: RECENT", "СОРТИРОВКА: НЕДАВНИЕ"},
        {"SORT: SIZE", "СОРТИРОВКА: РАЗМЕР"},
        {"Search games and Homebrew", "Поиск игр и Homebrew"},
        {"SEARCH CLEARED", "ПОИСК ОЧИЩЕН"}, {"SEARCH KEYBOARD UNAVAILABLE", "КЛАВИАТУРА НЕДОСТУПНА"},
        {"LIBRARY INITIALIZING", "ЗАГРУЗКА БИБЛИОТЕКИ"},
        {"LIBRARY READY", "БИБЛИОТЕКА ГОТОВА"},
        {"GAME SERVICE OFFLINE", "СЛУЖБА ИГР НЕДОСТУПНА"},
        {"CATALOGUE LIMIT REACHED - SOME ITEMS HIDDEN", "ДОСТИГНУТ ЛИМИТ КАТАЛОГА — ЧАСТЬ ПРИЛОЖЕНИЙ СКРЫТА"},
        {"STARTING TRANSWITCH...", "ЗАПУСК TRANSWITCH..."},
        {"CATALOGUE LOADING - Y ADD  X EDIT", "ЗАГРУЗКА КАТАЛОГА — Y ДОБАВИТЬ  X ИЗМЕНИТЬ"},
        {"TRANSWITCH READY - CATALOGUE LOADING", "TRANSWITCH ГОТОВ — ЗАГРУЖАЕТСЯ КАТАЛОГ"},
        {"LAUNCHER READY - Y ADD  X EDIT", "ЛАУНЧЕР ГОТОВ — Y ДОБАВИТЬ  X РЕДАКТИРОВАТЬ"},
        {"SELECT PLAYER", "ВЫБЕРИТЕ ИГРОКА"},
        {"CHANGE", "СМЕНИТЬ"}, {"PLAYER", "ИГРОК"},
        {"POWER", "ПИТАНИЕ"}, {"EXECUTE", "ВЫПОЛНИТЬ"},
        {"SLEEP", "СОН"}, {"REBOOT", "ПЕРЕЗАГРУЗКА"}, {"SHUTDOWN", "ВЫКЛЮЧЕНИЕ"},
        {"SELECT AN ACTION FIRST", "СНАЧАЛА ВЫБЕРИТЕ ДЕЙСТВИЕ"},
        {"A  LAUNCH SELECTED     B  CLOSE", "A  ЗАПУСТИТЬ     B  ЗАКРЫТЬ"},
        {"NO RECENT LAUNCHES YET", "НЕТ НЕДАВНИХ ЗАПУСКОВ"},
        {"LAUNCH A GAME OR HOMEBREW APP TO SEE IT HERE", "ЗАПУСТИТЕ ИГРУ ИЛИ HOMEBREW, ЧТОБЫ УВИДЕТЬ ЕЁ ЗДЕСЬ"},
        {"A CONFIRM     B CANCEL", "A ПОДТВЕРДИТЬ     B ОТМЕНА"},
        {"DELETE WORKSPACE?", "УДАЛИТЬ РАБОЧИЙ СТОЛ?"},
        {"DELETE", "УДАЛИТЬ"}, {"OPEN WORKSPACE", "ОТКРЫТЬ РАБОЧИЙ СТОЛ"},
        {"MOVE TILE TO WORKSPACE", "ПЕРЕМЕСТИТЬ ПЛИТКУ"},
        {"SELECT A TILE IN EDIT MODE FIRST", "СНАЧАЛА ВЫБЕРИТЕ ПЛИТКУ В РЕЖИМЕ РЕДАКТИРОВАНИЯ"},
        {"CHOOSE A WIDGET TO ADD", "ВЫБЕРИТЕ ВИДЖЕТ ДЛЯ ДОБАВЛЕНИЯ"},
        {"WALLPAPER APPLIED", "ОБОИ ПРИМЕНЕНЫ"},
        {"CUSTOM FONT APPLIED", "ШРИФТ ПРИМЕНЕН"},
        {"ICON PACK APPLIED", "НАБОР ИКОНОК ПРИМЕНЕН"},
        {"APPEARANCE RESET", "ОФОРМЛЕНИЕ СБРОШЕНО"},
        {"DESKTOP APPEARANCE SAVED", "ОФОРМЛЕНИЕ РАБОЧЕГО СТОЛА СОХРАНЕНО"},
        {"LAYOUT SAVED", "РАСКЛАДКА СОХРАНЕНА"},
        {"RESTORED FROM BACKUP", "ВОССТАНОВЛЕНО ИЗ РЕЗЕРВНОЙ КОПИИ"},
        {"SYSTEM BACKEND NOT CONNECTED", "СИСТЕМНЫЙ BACKEND НЕ ПОДКЛЮЧЕН"},
        {"SYSTEM BACKEND IPC UNAVAILABLE", "СИСТЕМНЫЙ IPC НЕДОСТУПЕН"},
        {"NRO CHAIN-LOAD UNAVAILABLE; USE HBMENU TAKEOVER", "CHAIN-LOAD NRO НЕДОСТУПЕН; ИСПОЛЬЗУЙТЕ HBMENU"},
        {"UNSUPPORTED", "НЕ ПОДДЕРЖИВАЕТСЯ"},
        {"GAME", "ИГРА"}, {"HOMEBREW", "HOMEBREW"},
        {"ALL", "ВСЁ"}, {"GAMES", "ИГРЫ"},
        {"NAME", "ИМЯ"}, {"SIZE", "РАЗМЕР"},
        {"SETTINGS SD COMMIT FAILED", "НЕ УДАЛОСЬ СОХРАНИТЬ НА SD"},
        {"LAYOUT SAVE FAILED", "НЕ УДАЛОСЬ СОХРАНИТЬ РАСКЛАДКУ"},
        {"INVALID TITLE ID", "НЕКОРРЕКТНЫЙ TITLE ID"},
        {"GALLERY", "ГАЛЕРЕЯ"}
    };

    for (const auto& entry : phrases)
        if (value == entry.first)
            return entry.second;

    if (value.compare(0, 8, "FILTER: ") == 0)
        return "ФИЛЬТР: " + translate(value.substr(8), true);
    if (value.compare(0, 6, "SORT: ") == 0)
        return "СОРТИРОВКА: " + translate(value.substr(6), true);
    if (value.compare(0, 7, "PLAYER: ") == 0)
        return "ИГРОК: " + value.substr(8);
    if (value.compare(0, 7, "SEARCH: ") == 0)
        return "ПОИСК: " + value.substr(8);
    if (value.compare(0, 6, "THEME ") == 0)
        return "ТЕМА " + value.substr(6);
    if (value.compare(0, 9, "FIRMWARE ") == 0)
        return "ПРОШИВКА " + value.substr(9);
    return value;
}

} // namespace desknx::i18n
