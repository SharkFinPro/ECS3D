#include "ConsolePanel.h"
#include "ConsolePanelLayout.h"
#include "ConsoleView.h"
#include "EditorTheme.h"
#include "GuiComponents.h"
#include <RingBufferSink.h>
#include <imgui.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace {
  constexpr const char* windowName = "Console";

  // The color a row is drawn in, by level - error/warn stand out, debug/trace recede.
  const ImVec4& colorForLevel(const LogLevel level)
  {
    switch (level)
    {
      case LogLevel::error:
        return theme::danger;
      case LogLevel::warn:
        return theme::scriptAmber;
      case LogLevel::debug:
      case LogLevel::trace:
        return theme::t3;
      case LogLevel::info:
      default:
        return theme::t1;
    }
  }
}

ConsolePanel::ConsolePanel(std::shared_ptr<RingBufferSink> sink)
  : m_sink(std::move(sink))
{
}

void ConsolePanel::open()
{
  m_open = true;
  m_focusRequested = true;
}

bool ConsolePanel::isOpen() const
{
  return m_open;
}

void ConsolePanel::setOpen(const bool open)
{
  m_open = open;
}

void ConsolePanel::refreshCacheIfNeeded()
{
  const auto currentSequence = m_sink->sequence();
  const bool sequenceChanged = currentSequence != m_lastSeenSequence;

  if (!sequenceChanged && !m_cacheDirty)
  {
    m_sawNewEntries = false;
    return;
  }

  m_sawNewEntries = sequenceChanged;
  m_lastSeenSequence = currentSequence;
  m_cacheDirty = false;

  const auto entries = m_sink->snapshot();
  auto view = buildConsoleView(entries, currentSequence, m_clearedBefore, m_filter);
  m_levelCounts = view.levelCounts;
  m_filtered = std::move(view.rows);
}

void ConsolePanel::displaySearchRow()
{
  constexpr float gap = 8.0f;
  constexpr float minSearchWidth = 140.0f;

  const ImGuiStyle& style = ImGui::GetStyle();
  const float filtersButtonWidth = ImGui::CalcTextSize("Filters").x + 2.0f * style.FramePadding.x;
  const float actionsWidth = ImGui::CalcTextSize("Copy").x + ImGui::CalcTextSize("Clear").x +
                             4.0f * style.FramePadding.x + gc::accentCheckboxCompactWidth("Auto-scroll") +
                             2.0f * style.ItemSpacing.x;
  const auto layout = consoleHeaderLayout(ImGui::GetContentRegionAvail().x, gap, filtersButtonWidth, actionsWidth,
                                          minSearchWidth);

  ImGui::PushStyleColor(ImGuiCol_Button, m_showFilters ? theme::accent : style.Colors[ImGuiCol_Button]);
  ImGui::PushStyleColor(ImGuiCol_Text, m_showFilters ? theme::onAcc : style.Colors[ImGuiCol_Text]);
  if (ImGui::Button("Filters"))
  {
    m_showFilters = !m_showFilters;
  }
  ImGui::PopStyleColor(2);

  ImGui::SameLine(0.0f, gap);

  if (gc::searchField("##ConsoleSearch", m_search, sizeof(m_search), "Search log", layout.searchWidth))
  {
    m_filter.search = m_search;
    m_cacheDirty = true;
  }

  if (layout.actionsOnSearchLine)
  {
    ImGui::SameLine(0.0f, gap);
  }

  displayActions();
}

void ConsolePanel::displayActions()
{
  if (ImGui::Button("Copy"))
  {
    std::string text;
    for (const auto& entry : m_filtered)
    {
      text += formatEntry(entry);
      text += "\n";
    }

    ImGui::SetClipboardText(text.c_str());
  }

  ImGui::SameLine();

  if (ImGui::Button("Clear"))
  {
    m_clearedBefore = m_sink->sequence();
    m_cacheDirty = true;
  }

  ImGui::SameLine();

  gc::accentCheckboxCompact("Auto-scroll", &m_autoScroll);
}

void ConsolePanel::displayFilterToggles()
{
  const float availWidth = ImGui::GetContentRegionAvail().x;
  float usedWidth = 0.0f;

  // Levels then categories flow as one run, wrapping at the panel's edge, with a wider gap between the groups.
  const auto toggle = [&](const char* label, bool& value, const float gap)
  {
    const auto placement = placeOnLine(usedWidth, gap, gc::accentCheckboxCompactWidth(label), availWidth);
    if (placement.sameLine)
    {
      ImGui::SameLine(0.0f, gap);
    }
    usedWidth = placement.usedWidth;

    bool selected = value;
    if (gc::accentCheckboxCompact(label, &selected))
    {
      value = selected;
      m_cacheDirty = true;
    }
  };

  static constexpr std::array<LogLevel, levelCount> levels = {
    LogLevel::trace, LogLevel::debug, LogLevel::info, LogLevel::warn, LogLevel::error
  };

  for (const auto level : levels)
  {
    const auto index = static_cast<std::size_t>(level);

    char label[32];
    std::snprintf(label, sizeof(label), "%s (%d)", toString(level).data(), m_levelCounts[index]);

    toggle(label, m_filter.levels[index], 12.0f);
  }

  static constexpr std::array<LogCategory, categoryCount> categories = {
    LogCategory::engine, LogCategory::assets, LogCategory::physics, LogCategory::net,
    LogCategory::script, LogCategory::editor, LogCategory::server, LogCategory::client
  };

  for (const auto category : categories)
  {
    const bool first = category == categories.front();

    toggle(toString(category).data(), m_filter.categories[static_cast<std::size_t>(category)], first ? 28.0f : 12.0f);
  }
}

void ConsolePanel::displayRows()
{
  ImGui::BeginChild("ConsoleRows", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(m_filtered.size()));
  while (clipper.Step())
  {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
    {
      const auto& entry = m_filtered[static_cast<std::size_t>(i)];
      const auto formatted = formatEntry(entry);

      ImGui::PushStyleColor(ImGuiCol_Text, colorForLevel(entry.level));
      ImGui::TextUnformatted(formatted.c_str());
      ImGui::PopStyleColor();
    }
  }
  clipper.End();

  if (m_autoScroll && m_sawNewEntries)
  {
    ImGui::SetScrollHereY(1.0f);
  }

  ImGui::EndChild();
}

void ConsolePanel::displayGui()
{
  if (!m_open)
  {
    return;
  }

  if (m_focusRequested)
  {
    ImGui::SetNextWindowFocus();
    m_focusRequested = false;
  }

  bool stayOpen = true;
  if (ImGui::Begin(windowName, &stayOpen))
  {
    displaySearchRow();

    if (m_showFilters)
    {
      displayFilterToggles();
    }

    refreshCacheIfNeeded();

    displayRows();
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}
