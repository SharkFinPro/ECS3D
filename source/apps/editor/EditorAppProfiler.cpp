#include "EditorApp.h"
#include <EditorViewportSettings.h>
#include <NetClient.h>
#include <ProfilerPanel.h>
#include <ProfilerReadout.h>
#include <ProfileScope.h>
#include <Protocol.h>
#include <RemoteStatsSummary.h>
#include <ServerStats.h>
#include <SettingsStore.h>
#include <SnapshotLog.h>
#include <TrafficCounters.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>

namespace {
  ServerLaneState laneState(const net::RemoteStats::State state)
  {
    switch (state)
    {
      case net::RemoteStats::State::waiting: return ServerLaneState::waiting;
      case net::RemoteStats::State::live: return ServerLaneState::live;
      case net::RemoteStats::State::unsupported: return ServerLaneState::unsupported;
      case net::RemoteStats::State::disconnected: return ServerLaneState::disconnected;
      case net::RemoteStats::State::idle: break;
    }

    return ServerLaneState::off;
  }

  ServerTrafficDirection trafficDirection(const net::TrafficDirectionSummary& summary)
  {
    ServerTrafficDirection direction;
    direction.bytesPerSecond = summary.bytesPerSecond;

    for (const auto& entry : summary.types)
    {
      const auto name = net::messageTypeName(static_cast<net::MessageType>(entry.type));
      const double share = summary.bytes == 0 ? 0.0 : static_cast<double>(entry.bytes) / summary.bytes;
      direction.types.push_back({std::string(name), entry.bytes, share});
    }

    return direction;
  }

  uint64_t totalBytes(const net::TrafficTotals& totals)
  {
    uint64_t sum = 0;

    for (const auto bytes : totals.bytes)
    {
      sum += bytes;
    }

    return sum;
  }
}

void EditorApp::updateServerStats()
{
  const bool wanted = profilerPanelState::wantsServerStats(
    m_profilerPanel->isOpen(), editorViewportSettings::readStatsReadout(*m_settings));
  const bool connected = m_netClient->isConnected();

  if (wanted != m_statsSubscribed)
  {
    m_statsSubscribed = wanted;
    m_receiveRate.reset();

    if (connected)
    {
      m_netClient->send(net::packStatsSubscribe(wanted));
    }

    if (wanted)
    {
      m_remoteStats.subscribed();

      if (!connected)
      {
        m_remoteStats.disconnected();
      }
    }
    else
    {
      m_remoteStats.unsubscribed();
    }
  }

  if (!wanted)
  {
    m_profilerPanel->setServerLane(nullptr);
    return;
  }

  fillServerLane();
  m_profilerPanel->setServerLane(&m_serverLane);
}

void EditorApp::fillServerLane()
{
  const auto now = net::RemoteStats::Clock::now();
  const auto window = std::chrono::seconds(m_profilerPanel->windowSeconds());

  m_serverLane.state = laneState(m_remoteStats.state());
  m_serverLane.history = m_remoteStats.history().size() > 0 ? &m_remoteStats.history() : nullptr;
  m_serverLane.localServer = m_options.launchLocalServer;
  m_serverLane.droppedStepsCounter = net::RemoteStats::droppedStepsCounter;
  m_serverLane.ticksCounter = net::RemoteStats::ticksCounter;
  m_serverLane.tickMicros = m_remoteStats.tickDurations().values();

  const auto flags = m_remoteStats.flags();
  m_serverLane.sceneRunning = (flags & net::serverStatsFlags::sceneRunning) != 0;
  m_serverLane.debugBuild = (flags & net::serverStatsFlags::debugBuild) != 0;

  // subscribed() resets the held table to the root alone, so a changed count catches a fresh subscription.
  const std::size_t nodeCount = m_remoteStats.nodeCount();

  if (m_serverLane.nodes.size() != nodeCount || m_serverNodesEpoch != m_remoteStats.nodeTableEpoch())
  {
    m_serverNodesEpoch = m_remoteStats.nodeTableEpoch();
    m_serverLane.nodes.clear();

    for (std::size_t i = 0; i < nodeCount; ++i)
    {
      const auto id = static_cast<uint16_t>(i);
      m_serverLane.nodes.push_back({m_remoteStats.nodeParent(id), m_remoteStats.nodeName(id)});
    }
  }

  const auto traffic = net::summarizeTraffic(m_remoteStats, window);
  m_serverLane.sent = trafficDirection(traffic.sent);
  m_serverLane.received = trafficDirection(traffic.received);

  m_serverLane.snapshots.clear();
  m_serverLane.largestSnapshotBytes = 0;
  const auto& snapshots = m_remoteStats.snapshots();

  for (const auto& entry : snapshots)
  {
    m_serverLane.largestSnapshotBytes = std::max(m_serverLane.largestSnapshotBytes, entry.record.bytes);
  }

  for (auto it = snapshots.rbegin();
       it != snapshots.rend() && m_serverLane.snapshots.size() < profilerServerLane::maxSnapshotRows; ++it)
  {
    const double secondsAgo = std::chrono::duration<double>(now - it->takenAt).count();
    m_serverLane.snapshots.push_back({std::max(secondsAgo, 0.0), it->record.bytes,
                                      std::string(net::snapshotReasonName(it->record.reason)),
                                      it->record.buildMicros / 1000.0, it->record.recipients});
  }

  m_serverLane.snapshotsLastMinute = net::snapshotsWithin(m_remoteStats, now, std::chrono::minutes(1));
  m_serverLane.connections = m_remoteStats.connections();
  m_serverLane.editors = m_remoteStats.editors();
  m_serverLane.editorReceivedBytesPerSecond =
    m_receiveRate.update(totalBytes(m_netClient->traffic().totalIn()), now);
}

void EditorApp::drawStatsReadout(ImDrawList* drawList, const float sceneX, const float sceneY, const float sceneWidth)
{
  if (!editorViewportSettings::readStatsReadout(*m_settings))
  {
    return;
  }

  ECS3D_PROFILE_SCOPE("Profiler");

  const auto lines = profilerReadout::lines(m_profileLane.history(), m_serverLane);

  constexpr float margin = 8.0f;
  constexpr float padding = 6.0f;
  const float lineHeight = ImGui::GetTextLineHeight();

  float textWidth = 0.0f;

  for (const auto& line : lines)
  {
    textWidth = std::max(textWidth, ImGui::CalcTextSize(line.c_str()).x);
  }

  const float right = sceneX + sceneWidth - margin;
  const float top = sceneY + margin;
  const ImVec2 boxMin(right - textWidth - 2.0f * padding, top);
  const ImVec2 boxMax(right, top + lineHeight * static_cast<float>(lines.size()) + 2.0f * padding);

  drawList->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 140), 4.0f);

  float y = top + padding;

  for (const auto& line : lines)
  {
    const float width = ImGui::CalcTextSize(line.c_str()).x;
    drawList->AddText(ImVec2(right - padding - width, y), IM_COL32(235, 235, 235, 255), line.c_str());
    y += lineHeight;
  }
}
