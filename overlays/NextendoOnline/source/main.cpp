#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <switch.h>

#include "nextendo_api.hpp"
#include <nextendo/parser.hpp>
#include <nextendo/snapshot.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace {
constexpr const char* kCacheDirectory = "sdmc:/switch/NextendoOnline";
constexpr const char* kCachePath = "sdmc:/switch/NextendoOnline/online-cache.tsv";
constexpr const char* kCacheTempPath = "sdmc:/switch/NextendoOnline/online-cache.tmp";
constexpr std::uint64_t kRefreshIntervalMs = 15000;
constexpr std::size_t kMaxVisibleGames = 18;

std::mutex g_snapshotMutex;
nextendo::PlayerSnapshot g_snapshot;
bool g_hasSnapshot = false;
std::string g_lastError;

std::atomic<bool> g_workerRunning{false};
std::atomic<bool> g_workerFinished{true};
std::atomic<bool> g_refreshRequested{false};
std::atomic<bool> g_refreshing{false};
std::atomic<bool> g_dirty{false};
Thread g_worker{};
// libcurl + TLS and the JSON parser need a generous stack on Horizon.
std::uint8_t g_workerStack[0x40000] __attribute__((aligned(0x1000)));
bool g_workerStarted = false;
bool g_sdMounted = false;
bool g_nsReady = false;
bool g_networkReady = false;
bool g_setReady = false;
bool g_timeReady = false;

std::uint64_t monotonicMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string formatPlayers(unsigned count) {
    const unsigned lastTwo = count % 100;
    const unsigned last = count % 10;
    const char* suffix = " игроков";
    if (lastTwo < 11 || lastTwo > 14) {
        if (last == 1) suffix = " игрок";
        else if (last >= 2 && last <= 4) suffix = " игрока";
    }
    return std::to_string(count) + suffix;
}

std::string formatLastUpdated(std::uint64_t epochSeconds) {
    if (epochSeconds == 0) return "время неизвестно";
    const std::time_t value = static_cast<std::time_t>(epochSeconds);
    std::tm local{};
    if (!localtime_r(&value, &local)) return "время неизвестно";
    char result[32]{};
    if (std::strftime(result, sizeof(result), "%H:%M:%S", &local) == 0)
        return "время неизвестно";
    return result;
}

std::string titleIdText(nextendo::TitleId id) {
    char value[24]{};
    std::snprintf(value, sizeof(value), "%016llX",
                  static_cast<unsigned long long>(id));
    return std::string("ID ") + value;
}

bool loadCache(nextendo::PlayerSnapshot& result) {
    std::ifstream file(kCachePath);
    if (!file) return false;

    std::string line;
    if (!std::getline(file, line) || line.rfind("NXO1\t", 0) != 0) return false;
    char* end = nullptr;
    const unsigned long long fetchedAt = std::strtoull(line.c_str() + 5, &end, 10);
    if (end == line.c_str() + 5 || *end != '\0') return false;

    nextendo::PlayerSnapshot cached;
    cached.fetchedAt = fetchedAt;
    cached.fromCache = true;
    std::size_t lines = 0;
    while (std::getline(file, line) && lines++ < nextendo::MaxTitles) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab != 16) continue;
        nextendo::TitleId id = 0;
        if (!nextendo::parser::parseTitleId(line.substr(0, tab), id)) continue;
        char* countEnd = nullptr;
        const unsigned long count = std::strtoul(line.c_str() + tab + 1, &countEnd, 10);
        if (countEnd == line.c_str() + tab + 1 || *countEnd != '\0' ||
            count > nextendo::MaxPlayers) continue;
        cached.perTitle[id] = static_cast<unsigned>(count);
    }

    std::uint64_t total = 0;
    for (const auto& entry : cached.perTitle) {
        total += entry.second;
        if (total > nextendo::MaxPlayers) return false;
    }
    cached.total = static_cast<unsigned>(total);
    if (cached.perTitle.empty() && cached.fetchedAt == 0) return false;
    result = std::move(cached);
    return true;
}

void saveCache(const nextendo::PlayerSnapshot& snapshot) {
    mkdir("sdmc:/switch", 0777);
    mkdir(kCacheDirectory, 0777);
    std::ofstream file(kCacheTempPath, std::ios::trunc);
    if (!file) return;
    file << "NXO1\t" << snapshot.fetchedAt << '\n';
    for (const auto& entry : snapshot.perTitle) {
        char id[17]{};
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(entry.first));
        file << id << '\t' << entry.second << '\n';
    }
    file.flush();
    if (!file.good()) {
        file.close();
        std::remove(kCacheTempPath);
        return;
    }
    file.close();
    if (std::rename(kCacheTempPath, kCachePath) != 0)
        std::remove(kCacheTempPath);
}

nextendo::PlayerSnapshot copySnapshot(bool& hasSnapshot, std::string& lastError) {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    hasSnapshot = g_hasSnapshot;
    lastError = g_lastError;
    return g_snapshot;
}

void setSnapshot(const nextendo::PlayerSnapshot& snapshot) {
    {
        std::lock_guard<std::mutex> lock(g_snapshotMutex);
        g_snapshot = snapshot;
        g_hasSnapshot = true;
        g_lastError.clear();
    }
    saveCache(snapshot);
    g_dirty.store(true);
}

void setRefreshError() {
    std::lock_guard<std::mutex> lock(g_snapshotMutex);
    g_lastError = g_hasSnapshot
        ? "Сеть недоступна — оставлены последние данные"
        : "Нет ответа от Nextendo Network";
}

void pollThread(void*) {
    bool hasCached = false;
    std::string error;
    nextendo::PlayerSnapshot cached = copySnapshot(hasCached, error);
    if (hasCached) {
        nextendo::resolveInstalledNames(cached, g_nsReady);
        {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            if (g_hasSnapshot && g_snapshot.fromCache) g_snapshot.titleNames = cached.titleNames;
        }
        g_dirty.store(true);
    }

    std::uint64_t nextRefresh = 0;
    while (g_workerRunning.load()) {
        const std::uint64_t now = monotonicMs();
        const bool manual = g_refreshRequested.exchange(false);
        if (manual || now >= nextRefresh) {
            g_refreshing.store(true);
            g_dirty.store(true);

            nextendo::PlayerSnapshot fresh;
            if (nextendo::fetchSnapshot(fresh, &g_workerRunning) && g_workerRunning.load()) {
                nextendo::resolveInstalledNames(fresh, g_nsReady);
                fresh.fetchedAt = static_cast<std::uint64_t>(std::time(nullptr));
                fresh.fromCache = false;
                setSnapshot(fresh);
            } else if (g_workerRunning.load()) {
                setRefreshError();
            }

            g_refreshing.store(false);
            g_dirty.store(true);
            nextRefresh = monotonicMs() + kRefreshIntervalMs;
        }

        for (int i = 0; i < 8 && g_workerRunning.load(); ++i)
            svcSleepThread(250'000'000LL);
    }
    g_workerFinished.store(true);
    g_dirty.store(true);
}

class OnlineGui final : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        m_frame = new tsl::elm::OverlayFrame("NEXTENDO ONLINE", "Сеть Nextendo • автообновление 15 сек");
        rebuildList(false);
        return m_frame;
    }

    void update() override {
        if (!g_workerRunning.load() && !g_workerFinished.load()) {
            if (m_frame) m_frame->setSubtitle("Безопасное завершение сетевого запроса…");
        }
        if (g_dirty.exchange(false)) rebuildList(true);
        if (!g_workerRunning.load() && g_workerFinished.load() && m_closing)
            tsl::Overlay::get()->close();
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState&,
                     HidAnalogStickState, HidAnalogStickState) override {
        if (keysDown & HidNpadButton_B) {
            m_closing = true;
            g_workerRunning.store(false);
            return true;
        }
        if (keysDown & HidNpadButton_Y) {
            if (!g_refreshing.load()) g_refreshRequested.store(true);
            return true;
        }
        return false;
    }

private:
    void rebuildList(bool dropFocus) {
        if (!m_frame) return;
        if (dropFocus) removeFocus();

        bool hasSnapshot = false;
        std::string error;
        nextendo::PlayerSnapshot snapshot = copySnapshot(hasSnapshot, error);
        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("СЕЙЧАС В СЕТИ"));

        std::string totalValue = hasSnapshot ? formatPlayers(snapshot.total) :
            (g_refreshing.load() ? "подключение…" : "нет данных");
        if (hasSnapshot && snapshot.fromCache) totalValue += " • кэш";
        else if (g_refreshing.load() && hasSnapshot) totalValue += " • обновление";

        auto* total = new tsl::elm::ListItem("Игроков онлайн", totalValue);
        total->setValueColor(tsl::style::color::ColorHighlight);
        total->setClickListener([](u64 keys) {
            if ((keys & HidNpadButton_A) != 0 && !g_refreshing.load()) {
                g_refreshRequested.store(true);
                return true;
            }
            return false;
        });
        list->addItem(total);

        std::string subtitle = "Nextendo Network • обновление каждые 15 сек";
        if (hasSnapshot) subtitle = std::string(snapshot.fromCache ? "Кэш от " : "Обновлено ") +
                                    formatLastUpdated(snapshot.fetchedAt);
        else if (!error.empty()) subtitle = error;
        if (g_refreshing.load()) subtitle += " • обновление…";
        m_frame->setSubtitle(subtitle);

        list->addItem(new tsl::elm::CategoryHeader("ИГРЫ С АКТИВНЫМИ ИГРОКАМИ"));
        std::vector<std::pair<nextendo::TitleId, unsigned>> rows;
        if (hasSnapshot) {
            rows.reserve(snapshot.perTitle.size());
            for (const auto& entry : snapshot.perTitle)
                if (entry.second > 0) rows.push_back(entry);
        }
        std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
            if (left.second != right.second) return left.second > right.second;
            return left.first < right.first;
        });

        std::size_t shown = 0;
        for (const auto& entry : rows) {
            if (shown++ >= kMaxVisibleGames) break;
            const auto found = snapshot.titleNames.find(entry.first);
            const std::string name = found != snapshot.titleNames.end()
                ? found->second : titleIdText(entry.first);
            auto* item = new tsl::elm::ListItem(name, std::to_string(entry.second));
            item->setClickListener([](u64) { return true; });
            list->addItem(item);
        }
        if (rows.empty()) {
            const std::string message = hasSnapshot
                ? "Сейчас нет активных игровых сессий"
                : "Ожидание данных Nextendo…";
            auto* empty = new tsl::elm::ListItem(message, "");
            empty->setClickListener([](u64) { return true; });
            list->addItem(empty);
        }

        list->addItem(new tsl::elm::CategoryHeader("УПРАВЛЕНИЕ"));
        auto* refresh = new tsl::elm::ListItem("Обновить сейчас", "Y");
        refresh->setClickListener([](u64 keys) {
            if ((keys & HidNpadButton_A) != 0 && !g_refreshing.load()) {
                g_refreshRequested.store(true);
                return true;
            }
            return false;
        });
        list->addItem(refresh);

        m_frame->setContent(list);
    }

    tsl::elm::OverlayFrame* m_frame{nullptr};
    bool m_closing{false};
};

class NextendoOnlineOverlay final : public tsl::Overlay {
public:
    void initServices() override {
        g_sdMounted = R_SUCCEEDED(fsdevMountSdmc());
        if (g_sdMounted) {
            mkdir("sdmc:/switch", 0777);
            mkdir(kCacheDirectory, 0777);
        }
        g_timeReady = R_SUCCEEDED(timeInitialize());
        g_nsReady = R_SUCCEEDED(nsInitialize());
        g_setReady = R_SUCCEEDED(setInitialize());
        g_networkReady = nextendo::initializeNetwork();

        nextendo::PlayerSnapshot cached;
        if (g_sdMounted && loadCache(cached)) {
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            g_snapshot = std::move(cached);
            g_hasSnapshot = true;
        }

        if (g_networkReady) {
            g_workerRunning.store(true);
            g_workerFinished.store(false);
            g_refreshRequested.store(true);
            const Result createResult = threadCreate(&g_worker, pollThread, nullptr,
                g_workerStack, sizeof(g_workerStack), 0x2C, -2);
            if (R_SUCCEEDED(createResult)) {
                const Result startResult = threadStart(&g_worker);
                g_workerStarted = R_SUCCEEDED(startResult);
                if (!g_workerStarted) threadClose(&g_worker);
            }
            if (!g_workerStarted) {
                g_workerRunning.store(false);
                g_workerFinished.store(true);
                std::lock_guard<std::mutex> lock(g_snapshotMutex);
                g_lastError = "Не удалось запустить сетевой поток";
            }
        } else {
            g_workerRunning.store(false);
            g_workerFinished.store(true);
            std::lock_guard<std::mutex> lock(g_snapshotMutex);
            g_lastError = "Не удалось инициализировать сетевое подключение";
        }
        g_dirty.store(true);
    }

    void exitServices() override {
        // nx-ovlloader unloads this module after close: never leave a worker
        // mapped in the process while the overlay is being unmapped.
        g_workerRunning.store(false);
        if (g_workerStarted) {
            threadWaitForExit(&g_worker);
            threadClose(&g_worker);
            g_workerStarted = false;
        }
        if (g_networkReady) {
            nextendo::shutdownNetwork();
            g_networkReady = false;
        }
        if (g_nsReady) { nsExit(); g_nsReady = false; }
        if (g_setReady) { setExit(); g_setReady = false; }
        if (g_timeReady) { timeExit(); g_timeReady = false; }
        if (g_sdMounted) { fsdevUnmountDevice("sdmc"); g_sdMounted = false; }
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<OnlineGui>();
    }
};

} // namespace

// libtesla's footer uses this optional C-linkage hook to choose Chinese button
// labels. Read the actual console language instead of leaving an unresolved
// weak callback in the overlay binary.
extern "C" bool nextendoIsChinese() {
    if (!g_setReady) return false;
    u64 languageCode = 0;
    SetLanguage language = SetLanguage_ENUS;
    if (R_FAILED(setGetSystemLanguage(&languageCode))) return false;
    if (R_FAILED(setMakeLanguage(languageCode, &language))) return false;
    return language == SetLanguage_ZHCN ||
           language == SetLanguage_ZHHANS ||
           language == SetLanguage_ZHHANT ||
           language == SetLanguage_ZHTW;
}

int main(int argc, char** argv) {
    return tsl::loop<NextendoOnlineOverlay>(argc, argv);
}
