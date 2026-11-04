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
    m_hiddenMask(profilerPanelState::readHiddenMask(settings)),
    m_serverHiddenMask(profilerPanelState::readHiddenMask(settings, profile::LaneKind::server))
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

  if (!open)
  {
    m_interaction.close();
    m_frozen.reset();
    m_frozenServer.reset();
    m_frozenServerNodes.clear();
  }
}

void ProfilerPanel::setServerLane(const ServerLaneInput* input)
{
  m_serverInput = input;
}

bool ProfilerPanel::wantsServerStats() const
{
  return m_open;
}

int ProfilerPanel::windowSeconds() const
{
  return m_windowSeconds;
}

void ProfilerPanel::freezeServer()
{
  if (m_frozenServer || m_serverInput == nullptr || m_serverInput->history == nullptr)
  {
    return;
  }

  m_frozenServer.emplace(*m_serverInput->history);
  m_frozenServerNodes = m_serverInput->nodes;
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

void ProfilerPanel::displayLane(const profile::History& history, const std::vector<profile::ViewNode>& nodes)
{
  const auto groups = profile::displayGroups(profile::LaneKind::frame);
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

  ImGui::PushID("editorLane");

  const uint64_t clicked = profilerCharts::drawStackedBars(columns, groups, scaleMillis, std::nullopt,
                                                           m_interaction.selectedSequence,
                                                           ImGui::GetFontSize() * 9.0f);
  m_interaction.clickColumn(clicked);

  // Freeze from the history just drawn, so the selected column keeps its merged boundaries.
  if (m_interaction.paused && !m_frozen)
  {
    m_frozen.emplace(m_lane.history());
  }

  const uint32_t previousMask = m_hiddenMask;
  profilerCharts::drawLegend(groups, m_hiddenMask);

  if (m_hiddenMask != previousMask)
  {
    profilerPanelState::writeHiddenMask(m_settings, m_hiddenMask);
  }

  ImGui::PopID();
}

void ProfilerPanel::displayServerLane(const std::vector<profile::ViewNode>& nodes)
{
  const ServerLaneInput& input = *m_serverInput;
  const profile::History* history = m_frozenServer ? &*m_frozenServer : input.history;
  const std::string notice = serverLaneNotice(input);

  ImGui::Separator();

  if (history == nullptr || history->size() == 0)
  {
    ImGui::TextUnformatted("SERVER TICK");

    if (!notice.empty())
    {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", notice.c_str());
    }

    return;
  }

  constexpr double budgetMillis = 20.0;
  const auto groups = profile::displayGroups(profile::LaneKind::server);
  const auto groupOfNode = profile::groupOfNodes(profile::LaneKind::server, nodes);
  const std::size_t window = profile::windowSize(*history, m_windowSeconds);
  const auto summary = profile::summarizeLane(*history, window);

  ImGui::Text("SERVER TICK  (%s server, 50 Hz)  avg %.1f ms  p95 %.1f ms  max %.1f ms  budget %.0f ms",
              input.localServer ? "local" : "remote", summary.averageMillis, summary.p95Millis, summary.maxMillis,
              budgetMillis);

  const double scaleMillis =
    m_scaleMillis == profilerPanelState::autoScale
      ? std::max(profile::autoScaleMillis(profilerPanelState::p99WallMicros(*history, window)),
                 profile::fixedScalesMillis[1])
      : m_scaleMillis;
  const auto maxColumns = static_cast<std::size_t>(std::max(ImGui::GetContentRegionAvail().x, 1.0f));
  const auto columns = profile::buildColumns(*history, window, nodes, groupOfNode, m_serverHiddenMask, maxColumns);

  ImGui::PushID("serverLane");

  const uint64_t clicked = profilerCharts::drawStackedBars(columns, groups, scaleMillis, budgetMillis,
                                                           m_interaction.serverSelectedSequence,
                                                           ImGui::GetFontSize() * 9.0f);
  m_interaction.clickServerColumn(clicked);
  freezeServer();

  const uint32_t previousMask = m_serverHiddenMask;
  profilerCharts::drawLegend(groups, m_serverHiddenMask);

  if (m_serverHiddenMask != previousMask)
  {
    profilerPanelState::writeHiddenMask(m_settings, m_serverHiddenMask, profile::LaneKind::server);
  }

  ImGui::PopID();

  ImGui::Text("dropped steps (last %d s): %lld", m_windowSeconds,
              static_cast<long long>(profile::sumCounter(*history, window, input.droppedStepsCounter)));

  if (!notice.empty())
  {
    ImGui::TextDisabled("%s", notice.c_str());
  }
}

void ProfilerPanel::displayBreakdown(const profile::History& history, const std::vector<profile::ViewNode>& nodes,
                                     const std::vector<profile::ViewNode>& serverNodes)
{
  const profile::History* serverHistory =
    m_frozenServer ? &*m_frozenServer : (m_serverInput != nullptr ? m_serverInput->history : nullptr);
  const bool showServer = m_interaction.breakdownLane == profilerPanelState::BreakdownLane::server &&
                          serverHistory != nullptr && serverHistory->size() > 0;

  const profile::History& shown = showServer ? *serverHistory : history;
  const auto& shownNodes = showServer ? serverNodes : nodes;
  const uint64_t sequence = showServer ? m_interaction.serverSelectedSequence : m_interaction.selectedSequence;

  const std::string selected = sequence != 0
                                 ? std::string("selected: ") + (showServer ? "tick " : "frame ") +
                                     std::to_string(sequence)
                                 : "latest";
  const std::string heading = std::string(showServer ? "Server breakdown (" : "Breakdown (") + selected +
                              ")###breakdown";

  if (ImGui::CollapsingHeader(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
  {
    profilerCharts::drawBreakdownTable(
      profile::breakdownRows(shown, shownNodes, profile::windowSize(shown, m_windowSeconds), sequence));
  }
}

void ProfilerPanel::displayNetwork() const
{
  if (!ImGui::CollapsingHeader("Network"))
  {
    return;
  }

  const ServerLaneInput& input = *m_serverInput;

  ImGui::Text("Sent  %s", profilerServerLane::describeTraffic(input.sent).c_str());
  ImGui::Text("Received  %s", profilerServerLane::describeTraffic(input.received).c_str());
  ImGui::Text("Editor received  %s", profilerServerLane::formatRate(input.editorReceivedBytesPerSecond).c_str());
  ImGui::Text("Snapshots  %s", profilerServerLane::describeSnapshots(input).c_str());

  if (!input.snapshots.empty() &&
      ImGui::BeginTable("snapshots", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
  {
    ImGui::TableSetupColumn("Ago");
    ImGui::TableSetupColumn("Size");
    ImGui::TableSetupColumn("Reason");
    ImGui::TableSetupColumn("Build");
    ImGui::TableSetupColumn("Recipients");
    ImGui::TableHeadersRow();

    for (const auto& row : input.snapshots)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%.0f s", row.secondsAgo);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(profilerServerLane::formatSize(row.bytes).c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.reason.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f ms", row.buildMillis);
      ImGui::TableNextColumn();
      ImGui::Text("%u", static_cast<unsigned>(row.recipients));
    }

    ImGui::EndTable();
  }

  ImGui::Text("Connections: %u (editors: %u)", static_cast<unsigned>(input.connections),
              static_cast<unsigned>(input.editors));
  ImGui::TextDisabled("Byte counts exclude the transport's framing: +4 bytes per message on TCP.");
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

  // A saved layout from before this panel existed opens it floating; FirstUseEver keeps a saved size winning.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.5f, viewport->Size.y * 0.5f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

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

    if (!m_interaction.paused)
    {
      m_frozenServer.reset();
      m_frozenServerNodes.clear();
    }
    else
    {
      freezeServer();
    }

    const profile::History& history = m_frozen ? *m_frozen : m_lane.history();
    const auto nodes = profile::viewNodes(m_lane.tree());

    if (history.size() == 0)
    {
      ImGui::TextUnformatted("Waiting for frames");
    }
    else
    {
      displayLane(history, nodes);
    }

    static const std::vector<profile::ViewNode> noNodes;
    const std::vector<profile::ViewNode>* serverNodes = &noNodes;

    if (m_serverInput != nullptr)
    {
      serverNodes = m_frozenServer ? &m_frozenServerNodes : &m_serverInput->nodes;
      displayServerLane(*serverNodes);
    }

    if (history.size() != 0 || m_serverInput != nullptr)
    {
      displayBreakdown(history, nodes, *serverNodes);
    }

    if (m_serverInput != nullptr)
    {
      displayNetwork();
    }
  }

  ImGui::End();

  if (!stayOpen)
  {
    setOpen(false);
  }
}
