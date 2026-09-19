#include "ConsolePanel.h"
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
  const auto count = entries.size();

  // The sink does not tag entries with their write sequence, so the first retained entry's sequence is
  // derived from the current total and the retained count. A write racing this pair of calls can shift it
  // by one; that only risks a just-cleared or just-kept entry landing on the wrong side of the line for a
  // single frame, which is not worth a lock shared with the sink to avoid.
  const std::uint64_t firstSequence = currentSequence >= count
    ? currentSequence - static_cast<std::uint64_t>(count) + 1
    : 1;

  m_levelCounts.fill(0);
  m_filtered.clear();
  m_filtered.reserve(count);

  for (std::size_t i = 0; i < count; ++i)
  {
    const std::uint64_t entrySequence = firstSequence + i;
    if (entrySequence <= m_clearedBefore)
    {
      continue;
    }

    ++m_levelCounts[static_cast<std::size_t>(entries[i].level)];

    if (m_filter.matches(entries[i]))
    {
      m_filtered.push_back(entries[i]);
    }
  }
}

void ConsolePanel::displayFilterRow()
{
  if (gc::searchField("##ConsoleSearch", m_search, sizeof(m_search), "Search log"))
  {
    m_filter.search = m_search;
    m_cacheDirty = true;
  }

  static constexpr std::array<LogLevel, levelCount> levels = {
    LogLevel::trace, LogLevel::debug, LogLevel::info, LogLevel::warn, LogLevel::error
  };

  for (const auto level : levels)
  {
    const auto index = static_cast<std::size_t>(level);
    bool selected = m_filter.levels[index];

    char label[32];
    std::snprintf(label, sizeof(label), "%s (%d)", toString(level).data(), m_levelCounts[index]);

    ImGui::SameLine(0.0f, 16.0f);
    if (gc::accentCheckboxCompact(label, &selected))
    {
      m_filter.levels[index] = selected;
      m_cacheDirty = true;
    }
  }

  static constexpr std::array<LogCategory, categoryCount> categories = {
    LogCategory::engine, LogCategory::assets, LogCategory::physics, LogCategory::net,
    LogCategory::script, LogCategory::editor, LogCategory::server, LogCategory::client
  };

  for (const auto category : categories)
  {
    const auto index = static_cast<std::size_t>(category);
    bool selected = m_filter.categories[index];

    ImGui::SameLine(0.0f, 12.0f);
    if (gc::accentCheckboxCompact(toString(category).data(), &selected))
    {
      m_filter.categories[index] = selected;
      m_cacheDirty = true;
    }
  }
}

void ConsolePanel::displayActionRow()
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

  ImGui::Checkbox("Auto-scroll", &m_autoScroll);
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
    displayFilterRow();

    refreshCacheIfNeeded();

    displayActionRow();

    displayRows();
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}
