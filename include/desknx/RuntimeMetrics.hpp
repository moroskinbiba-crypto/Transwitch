#pragma once
// Bounded rolling performance samples. Fixed storage avoids per-frame
// allocations; summaries are calculated only when diagnostics are emitted.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace desknx::runtime {
enum class Metric : std::size_t {
    FrameTime = 0, RenderTime, PresentWait, InputLatency, WorkspaceSwitch, Count
};

class RollingMetrics {
public:
    static constexpr std::size_t Capacity = 180;
    struct Summary {
        double averageMs{0.0};
        double p95Ms{0.0};
        double maximumMs{0.0};
        std::size_t samples{0};
    };

    void record(Metric metric, double milliseconds) {
        const std::size_t index = static_cast<std::size_t>(metric);
        if (index >= windows_.size() || !std::isfinite(milliseconds) ||
            milliseconds < 0.0) return;
        Window& window = windows_[index];
        window.values[window.next] = milliseconds;
        window.next = (window.next + 1) % Capacity;
        if (window.count < Capacity) ++window.count;
    }

    Summary summarize(Metric metric) const {
        const std::size_t index = static_cast<std::size_t>(metric);
        if (index >= windows_.size()) return {};
        const Window& window = windows_[index];
        Summary result{};
        result.samples = window.count;
        if (window.count == 0) return result;

        std::array<double, Capacity> ordered{};
        double total = 0.0;
        for (std::size_t i = 0; i < window.count; ++i) {
            ordered[i] = window.values[i];
            total += window.values[i];
        }
        std::sort(ordered.begin(), ordered.begin() +
                  static_cast<std::ptrdiff_t>(window.count));
        result.averageMs = total / static_cast<double>(window.count);
        const std::size_t p95Index = (window.count * 95 + 99) / 100 - 1;
        result.p95Ms = ordered[p95Index];
        result.maximumMs = ordered[window.count - 1];
        return result;
    }

private:
    struct Window {
        std::array<double, Capacity> values{};
        std::size_t count{0};
        std::size_t next{0};
    };
    std::array<Window, static_cast<std::size_t>(Metric::Count)> windows_{};
};
} // namespace desknx::runtime
