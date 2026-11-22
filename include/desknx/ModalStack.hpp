#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace desknx::ui {

enum class ModalLayer : std::uint8_t {
    None = 0,
    AppLibrary,
    WorkspaceMenu,
    WorkspaceSettings,
    WorkspacePicker,
    DeleteConfirmation,
    Recent,
    SystemTools,
    LauncherSettings,
    MediaPicker,
    Power,
    UserPicker
};

struct ModalFlags {
    bool appLibrary{false};
    bool workspaceMenu{false};
    bool workspaceSettings{false};
    bool workspacePicker{false};
    bool deleteConfirmation{false};
    bool recent{false};
    bool systemTools{false};
    bool launcherSettings{false};
    bool mediaPicker{false};
    bool power{false};
    bool userPicker{false};
};

// The order is bottom-to-top and deliberately mirrors drawScreen().
// Parents may remain visible below child surfaces, but only top() owns input.
// Keeping z-order here prevents keyboard and touch dispatch from drifting apart.
class ModalStack {
public:
    static constexpr std::size_t Capacity = 11;

    static ModalStack build(const ModalFlags& flags) noexcept {
        ModalStack stack;
        if (flags.appLibrary) stack.push(ModalLayer::AppLibrary);
        if (flags.workspaceMenu) stack.push(ModalLayer::WorkspaceMenu);
        if (flags.workspaceSettings) stack.push(ModalLayer::WorkspaceSettings);
        if (flags.workspacePicker) stack.push(ModalLayer::WorkspacePicker);
        if (flags.deleteConfirmation) stack.push(ModalLayer::DeleteConfirmation);
        if (flags.recent) stack.push(ModalLayer::Recent);
        if (flags.systemTools) stack.push(ModalLayer::SystemTools);
        if (flags.launcherSettings) stack.push(ModalLayer::LauncherSettings);
        if (flags.mediaPicker) stack.push(ModalLayer::MediaPicker);
        if (flags.power) stack.push(ModalLayer::Power);
        if (flags.userPicker) stack.push(ModalLayer::UserPicker);
        return stack;
    }

    ModalLayer top() const noexcept {
        return size_ == 0 ? ModalLayer::None : layers_[size_ - 1];
    }

    bool contains(ModalLayer layer) const noexcept {
        for (std::size_t i = 0; i < size_; ++i)
            if (layers_[i] == layer) return true;
        return false;
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    ModalLayer at(std::size_t index) const noexcept {
        return index < size_ ? layers_[index] : ModalLayer::None;
    }

private:
    void push(ModalLayer layer) noexcept {
        if (size_ < layers_.size()) layers_[size_++] = layer;
    }

    std::array<ModalLayer, Capacity> layers_{};
    std::size_t size_{0};
};

} // namespace desknx::ui
