#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace desknx::ui {

enum class SurfaceId : std::uint8_t {
    EmptyDesktop,
    Library,
    RecentGames,
    SystemMenu,
    LauncherSettings,
    Diagnostics,
    WorkspaceMenu,
    WorkspaceSettings,
    WorkspacePicker,
    MediaPicker,
    PowerMenu,
    UserPicker,
    DeleteConfirmation,
    Favorites,
    WidgetEditor,
    Nextendo,
    SteamGridDb,
    QuickSettings,
};

struct SurfaceEntry {
    SurfaceId id{SurfaceId::EmptyDesktop};
    bool ownsInput{false};
    bool dimsBackground{false};
    std::uint8_t dimAlpha{0};
};

// A small, allocation-free per-frame surface stack. Callers register visible
// surfaces from back to front; the last input-owning surface becomes the sole
// keyboard/controller focus owner for that frame.
class SurfaceManager final {
public:
    static constexpr std::size_t Capacity = 20;

    void beginFrame() noexcept {
        entries_ = {};
        count_ = 0;
        backdropActive_ = false;
        blockingSurfaceActive_ = false;
        dimAlpha_ = 0;
    }

    bool push(SurfaceId id, bool ownsInput, bool dimsBackground,
              std::uint8_t dimAlpha = 174) noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (entries_[i].id == id) {
                entries_[i] = SurfaceEntry{id, ownsInput, dimsBackground,
                                           static_cast<std::uint8_t>(dimsBackground ? dimAlpha : 0)};
                refreshFlags();
                return true;
            }
        }
        if (count_ >= entries_.size()) return false;
        entries_[count_++] = SurfaceEntry{
            id, ownsInput, dimsBackground,
            static_cast<std::uint8_t>(dimsBackground ? dimAlpha : 0)
        };
        refreshFlags();
        return true;
    }

    bool contains(SurfaceId id) const noexcept {
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].id == id) return true;
        return false;
    }

    bool isTop(SurfaceId id) const noexcept {
        const SurfaceEntry* current = top();
        return current && current->id == id;
    }

    const SurfaceEntry* top() const noexcept {
        for (std::size_t i = count_; i > 0; --i)
            if (entries_[i - 1].ownsInput) return &entries_[i - 1];
        return nullptr;
    }

    bool hasBlockingSurface() const noexcept {
        return blockingSurfaceActive_;
    }

    bool backdropActive() const noexcept {
        return backdropActive_;
    }

    std::uint8_t dimAlpha() const noexcept {
        return dimAlpha_;
    }

    std::size_t size() const noexcept {
        return count_;
    }

private:
    void refreshFlags() noexcept {
        backdropActive_ = false;
        blockingSurfaceActive_ = false;
        dimAlpha_ = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const SurfaceEntry& entry = entries_[i];
            blockingSurfaceActive_ = blockingSurfaceActive_ || entry.ownsInput;
            if (entry.dimsBackground) {
                backdropActive_ = true;
                if (entry.dimAlpha > dimAlpha_) dimAlpha_ = entry.dimAlpha;
            }
        }
    }

    std::array<SurfaceEntry, Capacity> entries_{};
    std::size_t count_{0};
    bool backdropActive_{false};
    bool blockingSurfaceActive_{false};
    std::uint8_t dimAlpha_{0};
};

} // namespace desknx::ui
