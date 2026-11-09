#pragma once

#include "core/GridModel.hpp"
#include "desknx/HomeNXWorkspaceAdapter.hpp"

#include <cstddef>
#include <map>
#include <vector>

namespace desknx::homenx {

inline bool toHomeNXWidgetType(EntryKind kind,
                              switchu::widgets::WidgetType& output) {
    switch (kind) {
        case EntryKind::ClockWidget:
            output = switchu::widgets::WidgetType::Clock;
            return true;
        case EntryKind::BatteryWidget:
            output = switchu::widgets::WidgetType::Batteries;
            return true;
        case EntryKind::RecentWidget:
            output = switchu::widgets::WidgetType::RecentlyPlayed;
            return true;
        case EntryKind::CalendarWidget:
            output = switchu::widgets::WidgetType::Calendar;
            return true;
        default:
            return false;
    }
}

// Builds the actual HomeNX GridModel from one validated Transwitch desktop.
// The full catalogue is supplied separately: workspace membership never
// mutates or truncates the daemon's application catalogue.
inline GridModel buildHomeNXGridModel(
    const WorkspaceProjection& workspace,
    const std::vector<AppEntry>& catalogue,
    const std::vector<switchu::widgets::Widget>& widgets) {

    GridModel result;
    std::map<switchu::widgets::WidgetType, std::size_t> widgetOccurrence;

    for (const Cell& cell : workspace.cells) {
        AppEntry output;
        if (cell.role == CellRole::Empty || cell.entryIndex < 0 ||
            static_cast<std::size_t>(cell.entryIndex) >= workspace.entries.size()) {
            output.kind = GridEntryKind::Empty;
            result.addEntry(std::move(output));
            continue;
        }

        if (cell.role == CellRole::Continuation) {
            output.kind = GridEntryKind::WidgetContinuation;
            output.titleId = 0;
            result.addEntry(std::move(output));
            continue;
        }

        const Entry& entry = workspace.entries[static_cast<std::size_t>(cell.entryIndex)];
        if (entry.kind == EntryKind::InstalledTitle) {
            const auto found = std::find_if(
                catalogue.begin(), catalogue.end(),
                [&](const AppEntry& candidate) {
                    return candidate.isApplication() &&
                           candidate.titleId == entry.titleId;
                });
            if (found == catalogue.end()) {
                output.kind = GridEntryKind::Empty;
            } else {
                output = *found;
                if (!entry.title.empty())
                    output.title = entry.title;
                output.widgetColumns = entry.columns;
                output.widgetRows = entry.rows;
            }
            result.addEntry(std::move(output));
            continue;
        }

        switchu::widgets::WidgetType widgetType{};
        if (toHomeNXWidgetType(entry.kind, widgetType)) {
            const std::size_t ordinal = widgetOccurrence[widgetType]++;
            std::size_t seen = 0;
            const switchu::widgets::Widget* selected = nullptr;
            for (const auto& candidate : widgets) {
                if (candidate.type != widgetType)
                    continue;
                if (seen++ == ordinal) {
                    selected = &candidate;
                    break;
                }
            }
            if (!selected) {
                output.kind = GridEntryKind::Empty;
                result.addEntry(std::move(output));
                continue;
            }

            const auto size = switchu::widgets::validatedSize(
                widgetType, {entry.columns, entry.rows}, AppLayoutMode::Grid);
            output.id = "widget:" + std::to_string(selected->id);
            output.title = entry.title.empty()
                ? switchu::widgets::fallbackName(widgetType) : entry.title;
            output.titleId = switchu::widgets::widgetTitleId(selected->id);
            output.kind = GridEntryKind::Widget;
            output.widgetId = selected->id;
            output.widgetType = widgetType;
            output.widgetColumns = size.columns;
            output.widgetRows = size.rows;
            output.widgetAssetRef = selected->assetRef;
            result.addEntry(std::move(output));
            continue;
        }

        // Homebrew NRO launching, folders, Transwitch system shortcuts and the
        // SD storage tile require dedicated runtime adapters. Keep their saved
        // Transwitch cells intact but do not fabricate an installed title ID.
        output.kind = GridEntryKind::Empty;
        result.addEntry(std::move(output));
    }

    return result;
}

} // namespace desknx::homenx
