#include "../include/desknx/RuntimeMetrics.hpp"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

using desknx::runtime::Metric;
using desknx::runtime::RollingMetrics;

int main() {
    RollingMetrics metrics;
    assert(metrics.summarize(Metric::FrameTime).samples == 0);

    for (int value = 1; value <= 10; ++value)
        metrics.record(Metric::FrameTime, static_cast<double>(value));
    auto summary = metrics.summarize(Metric::FrameTime);
    assert(summary.samples == 10);
    assert(std::fabs(summary.averageMs - 5.5) < 0.0001);
    assert(summary.p95Ms == 10.0);
    assert(summary.maximumMs == 10.0);

    metrics.record(Metric::RenderTime, -1.0);
    metrics.record(Metric::RenderTime, std::numeric_limits<double>::infinity());
    assert(metrics.summarize(Metric::RenderTime).samples == 0);

    for (std::size_t value = 0; value < RollingMetrics::Capacity + 20; ++value)
        metrics.record(Metric::WorkspaceSwitch, static_cast<double>(value));
    summary = metrics.summarize(Metric::WorkspaceSwitch);
    assert(summary.samples == RollingMetrics::Capacity);
    assert(summary.maximumMs == static_cast<double>(RollingMetrics::Capacity + 19));
    assert(summary.p95Ms <= summary.maximumMs);
    return 0;
}
