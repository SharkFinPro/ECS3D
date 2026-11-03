#include "ProfilerPanel.h"
#include "EditorTheme.h"
#include <Lane.h>
#include <ProfilerCharts.h>
#include <ProfilerView.h>
#include <SettingsStore.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace {
  constexpr const char* windowName = "Profiler";

  constexpr std::array<double, 5> scaleChoices{
    profilerPanelState::autoScale, profile::fixedScalesMillis[0], profile::fixedScalesMillis[1],
    profile::fixedScalesMillis[2], profile::fixedScalesMillis[3]
  };

  std::string scaleLabel(const double millis)
  {
    if (millis == profilerPanelState::autoScale)
    {
      return "Auto";
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g ms", millis);

    return buffer;
  }

  std::string windowLabel(const int seconds)
  {
    return std::to_string(seconds) + " s";
  }
}

ProfilerPanel::ProfilerPanel(const profile::Lane& lane, SettingsStore& settings)
  : m_lane(lane),
    m_settings(settings),
    m_windowSeconds(profilerPanelState::readWindowSeconds(settings)),
    m_scaleMillis(profilerPanelState::readScaleMillis(settings)),
    m_hiddenMask(profilerPanelState::readHiddenMask(settings))
{
}

void ProfilerPanel::open()
{
  m_open = true;
  m_focusRequested = true;
}

bool ProfilerPanel::isOpen() const
{
  return m_open;
}

void ProfilerPanel::setOpen(const bool open)
{
  m_open = open;
}

void ProfilerPanel::toggle()
{
  if (m_open)
  {
    setOpen(false);
  }
  else
  {
    open();
  }
}

void ProfilerPanel::displayHeader()
{
  if (ImGui::Button(m_interaction.paused ? "Resume" : "Pause"))
  {
    m_interaction.togglePause();
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);

  if (ImGui::BeginCombo("Window", windowLabel(m_windowSeconds).c_str()))
  {
    for (const int seconds : profilerPanelState::windowChoicesSeconds)
    {
      if (ImGui::Selectable(windowLabel(seconds).c_str(), seconds == m_windowSeconds))
      {
        m_windowSeconds = seconds;
        profilerPanelState::writeWindowSeconds(m_settings, seconds);
      }
    }

    ImGui::EndCombo();
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);

  if (ImGui::BeginCombo("Scale", scaleLabel(m_scaleMillis).c_str()))
  {
    for (const double millis : scaleChoices)
    {
      if (ImGui::Selectable(scaleLabel(millis).c_str(), millis == m_scaleMillis))
      {
        m_scaleMillis = millis;
        profilerPanelState::writeScaleMillis(m_settings, millis);
      }
    }

    ImGui::EndCombo();
  }

#ifndef NDEBUG
  ImGui::SameLine();
  ImGui::TextColored(theme::scriptAmber, "Debug build");
#endif
}

void ProfilerPanel::displayLane(const profile::History& history)
{
  const auto groups = profile::displayGroups(profile::LaneKind::frame);
  const auto nodes = profile::viewNodes(m_lane.tree());
  const auto groupOfNode = profile::groupOfNodes(profile::LaneKind::frame, nodes);
  const std::size_t window = profile::windowSize(history, m_windowSeconds);
  const auto summary = profile::summarizeLane(history, window);

  ImGui::Text("EDITOR FRAME  avg %.1f ms  p95 %.1f ms  max %.1f ms  %.0f fps   hitches (>2x): %zu",
              summary.averageMillis, summary.p95Millis, summary.maxMillis, summary.samplesPerSecond, summary.hitches);

  const double scaleMillis = m_scaleMillis == profilerPanelState::autoScale
                               ? profile::autoScaleMillis(profilerPanelState::p99WallMicros(history, window))
                               : m_scaleMillis;
  const auto maxColumns = static_cast<std::size_t>(std::max(ImGui::GetContentRegionAvail().x, 1.0f));
  const auto columns = profile::buildColumns(history, window, nodes, groupOfNode, m_hiddenMask, maxColumns);

  const uint64_t clicked = profilerCharts::drawStackedBars(columns, groups, scaleMillis, std::nullopt,
                                                           m_interaction.selectedSequence,
                                                           ImGui::GetFontSize() * 9.0f);
  m_interaction.clickColumn(clicked);

  const uint32_t previousMask = m_hiddenMask;
  profilerCharts::drawLegend(groups, m_hiddenMask);

  if (m_hiddenMask != previousMask)
  {
    profilerPanelState::writeHiddenMask(m_settings, m_hiddenMask);
  }

  const std::string selected = m_interaction.selectedSequence != 0
                                 ? "selected: frame " + std::to_string(m_interaction.selectedSequence)
                                 : "latest";
  const std::string heading = "Breakdown (" + selected + ")###breakdown";

  if (ImGui::CollapsingHeader(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
  {
    profilerCharts::drawBreakdownTable(
      profile::breakdownRows(history, nodes, window, m_interaction.selectedSequence));
  }
}

void ProfilerPanel::displayGui()
{
  if (!m_open)
  {
    return;
  }

  ECS3D_PROFILE_SCOPE("Profiler");

  if (m_focusRequested)
  {
    ImGui::SetNextWindowFocus();
    m_focusRequested = false;
  }

  bool stayOpen = true;
  if (ImGui::Begin(windowName, &stayOpen))
  {
    displayHeader();

    if (m_interaction.paused && !m_frozen)
    {
      m_frozen.emplace(m_lane.history());
    }
    else if (!m_interaction.paused)
    {
      m_frozen.reset();
    }

    const profile::History& history = m_frozen ? *m_frozen : m_lane.history();

    if (history.size() == 0)
    {
      ImGui::TextUnformatted("Waiting for frames");
    }
    else
    {
      displayLane(history);
    }
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}
