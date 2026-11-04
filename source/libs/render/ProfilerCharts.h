#ifndef PROFILERCHARTS_H
#define PROFILERCHARTS_H

#include <ProfilerView.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// ImGui drawing for the profiler's display model, shared by the editor panel and the client overlay.
namespace profilerCharts {
  // Returns the sequence of the column clicked this frame, or 0 when none was.
  uint64_t drawStackedBars(const std::vector<profile::Column>& columns, std::span<const profile::DisplayGroup> groups,
                           double scaleMillis, std::optional<double> budgetMillis, uint64_t selectedSequence,
                           float height, std::string_view label = "Frame");

  // A click on a swatch toggles that group's bit in hiddenMask.
  void drawLegend(std::span<const profile::DisplayGroup> groups, uint32_t& hiddenMask);

  void drawBreakdownTable(const std::vector<profile::BreakdownRow>& rows);
}

#endif
