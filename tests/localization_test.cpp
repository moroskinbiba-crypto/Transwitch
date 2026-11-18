// Host test for the UI translation table and its prefix rules.
#include "../include/desknx/Localization.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void expect(const std::string& input, const std::string& expected) {
    const std::string actual = desknx::i18n::translate(input, true);
    if (actual != expected) {
        std::printf("FAIL: translate(\"%s\") = \"%s\", expected \"%s\"\n",
                    input.c_str(), actual.c_str(), expected.c_str());
        ++failures;
    }
}

void testEnglishIsUntouched() {
    const std::string value = "PLAY";
    if (desknx::i18n::translate(value, false) != value) {
        std::printf("FAIL: English mode must return the input unchanged\n");
        ++failures;
    }
}

void testPrefixesKeepTheirPayload() {
    // These regressed: an 8-character prefix compared with length 7 matched
    // the wrong strings and cut a character off the remainder.
    expect("PLAYER: Ada", "ИГРОК: Ada");
    expect("SEARCH: zelda", "ПОИСК: zelda");
    expect("THEME 3/8", "ТЕМА 3/8");
    expect("FIRMWARE 18.1.0", "ПРОШИВКА 18.1.0");
    expect("Workspace 2", "Рабочий стол 2");
    expect("FILTER: GAMES", "ФИЛЬТР: ИГРЫ");
    expect("SORT: NAME", "СОРТИРОВКА: ИМЯ");
}

void testCardViewStringsAreTranslated() {
    expect("PLAY", "ИГРАТЬ");
    expect("OPEN", "ОТКРЫТЬ");
    expect("ADD APPS", "ДОБАВИТЬ");
    expect("HOME LAUNCHER", "ДОМАШНИЙ ЭКРАН");
    expect("CARD VIEW", "ВИТРИНА");
    expect("GAME", "ИГРА");
    expect("WIDGETS", "ВИДЖЕТЫ");
}

void testUnknownTextPassesThrough() {
    expect("Hollow Knight", "Hollow Knight");
    expect("", "");
    // Short strings must not read past the end when prefix rules are checked.
    expect("P", "P");
    expect("FILTER:", "FILTER:");
}

} // namespace

int main() {
    testEnglishIsUntouched();
    testPrefixesKeepTheirPayload();
    testCardViewStringsAreTranslated();
    testUnknownTextPassesThrough();
    if (failures != 0) {
        std::printf("localization test: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("localization test: all checks passed\n");
    return EXIT_SUCCESS;
}
