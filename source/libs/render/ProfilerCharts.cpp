#include "ProfilerCharts.h"
#include "ProfilerChartLayout.h"
#include <imgui.h>
#include <algorithm>
#include <cstddef>

namespace {
  constexpr ImU32 hitchMarkColor = IM_COL32(255, 255, 255, 120);
  constexpr ImU32 capMarkColor = IM_COL32(255, 80, 80, 255);
  constexpr ImU32 selectionColor = IM_COL32(255, 255, 255, 255);
  constexpr ImU32 budgetColor = IM_COL32(255, 255, 255, 140);

  double millis(const uint32_t micros)
  {
    return micros / 1000.0;
  }

  ImU32 dimmed(const ImU32 color)
  {
    return (color & 0x00FFFFFFu) | (90u << 24);
  }

  void drawColumnTooltip(const profile::Column& column, const std::span<const profile::DisplayGroup> groups)
  {
    ImGui::BeginTooltip();
    ImGui::Text("Frame %llu: %.2f ms", static_cast<unsigned long long>(column.sequence), millis(column.wallMicros));

    if (column.hitch)
    {
      ImGui::TextUnformatted("Hitch");
    }

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      if (column.groupMicros[i] == 0)
      {
        continue;
      }

      const auto& label = groups[i].label;
      ImGui::Text("%.*s: %.2f ms", static_cast<int>(label.size()), label.data(), millis(column.groupMicros[i]));
    }

    ImGui::EndTooltip();
  }
}

namespace profilerCharts {
  uint64_t drawStackedBars(const std::vector<profile::Column>& columns,
                           const std::span<const profile::DisplayGroup> groups, const double scaleMillis,
                           const std::optional<double> budgetMillis, const uint64_t selectedSequence,
                           const float height)
  {
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##profilerBars", ImVec2(width, height));

    const bool hovered = ImGui::IsItemHovered();
    const float mouseX = ImGui::GetIO().MousePos.x - origin.x;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(0, 0, 0, 70));

    const std::size_t count = columns.size();
    std::optional<std::size_t> hoveredColumn;

    for (std::size_t c = 0; c < count; ++c)
    {
      const auto& column = columns[c];
      const auto span = profilerChart::columnSpan(c, count, width);
      const float left = origin.x + span.left;
      const float right = origin.x + span.right;
      const float bottom = origin.y + height;

      uint64_t stacked = 0;
      float previousTop = 0.0f;

      for (std::size_t g = 0; g < groups.size(); ++g)
      {
        stacked += column.groupMicros[g];

        const float top = profilerChart::barHeight(static_cast<double>(stacked), scaleMillis, height);

        if (top > previousTop)
        {
          draw->AddRectFilled(ImVec2(left, bottom - top), ImVec2(right, bottom - previousTop), groups[g].color);
        }

        previousTop = top;
      }

      if (profilerChart::exceedsScale(static_cast<double>(stacked), scaleMillis))
      {
        draw->AddRectFilled(ImVec2(left, origin.y), ImVec2(right, origin.y + 3.0f), capMarkColor);
      }

      if (column.hitch)
      {
        draw->AddRectFilled(ImVec2(left, bottom - 2.0f), ImVec2(right, bottom), hitchMarkColor);
      }

      if (selectedSequence != 0 && column.sequence == selectedSequence)
      {
        draw->AddRect(ImVec2(left, origin.y), ImVec2(right, bottom), selectionColor);
      }

      if (hovered && mouseX >= span.left && mouseX < span.right)
      {
        hoveredColumn = c;
      }
    }

    if (budgetMillis)
    {
      const float y = origin.y + height - profilerChart::barHeight(*budgetMillis * 1000.0, scaleMillis, height);
      draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), budgetColor);
    }

    if (!hoveredColumn)
    {
      return 0;
    }

    drawColumnTooltip(columns[*hoveredColumn], groups);

    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? columns[*hoveredColumn].sequence : 0;
  }

  void drawLegend(const std::span<const profile::DisplayGroup> groups, uint32_t& hiddenMask)
  {
    const float box = ImGui::GetTextLineHeight();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      const uint32_t bit = 1u << i;
      const bool hidden = (hiddenMask & bit) != 0;
      const auto& label = groups[i].label;
      const char* labelBegin = label.data();
      const char* labelEnd = labelBegin + label.size();

      ImGui::PushID(static_cast<int>(i));

      if (i > 0)
      {
        ImGui::SameLine();
      }

      const ImVec2 position = ImGui::GetCursorScreenPos();
      const float textWidth = ImGui::CalcTextSize(labelBegin, labelEnd).x;

      if (ImGui::InvisibleButton("##legend", ImVec2(box + 4.0f + textWidth, box)))
      {
        hiddenMask ^= bit;
      }

      const ImU32 color = hidden ? dimmed(groups[i].color) : groups[i].color;
      draw->AddRectFilled(position, ImVec2(position.x + box, position.y + box), color);

      ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      textColor.w *= hidden ? 0.4f : 1.0f;
      draw->AddText(ImVec2(position.x + box + 4.0f, position.y), ImGui::ColorConvertFloat4ToU32(textColor),
                    labelBegin, labelEnd);

      ImGui::PopID();
    }
  }

  void drawBreakdownTable(const std::vector<profile::BreakdownRow>& rows)
  {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##profilerBreakdown", 6, flags))
    {
      return;
    }

    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Last");
    ImGui::TableSetupColumn("Avg");
    ImGui::TableSetupColumn("p95");
    ImGui::TableSetupColumn("Max");
    ImGui::TableSetupColumn("% of frame");
    ImGui::TableHeadersRow();

    for (const auto& row : rows)
    {
      const float indent = static_cast<float>(row.depth) * 14.0f;

      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::Indent(indent);
      ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
      ImGui::Unindent(indent);

      ImGui::TableNextColumn();
      ImGui::Text("%.2f", millis(row.lastMicros));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", row.averageMicros / 1000.0);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", millis(row.p95Micros));
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", millis(row.maxMicros));
      ImGui::TableNextColumn();
      ImGui::Text("%.0f%%", row.share * 100.0);
    }

    ImGui::EndTable();
  }
}
