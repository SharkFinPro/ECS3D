#ifndef PROFILERREADOUT_H
#define PROFILERREADOUT_H

#include "ProfilerServerLane.h"
#include <History.h>
#include <ProfilerView.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The text of the viewport's corner readout, kept free of ImGui so it can be tested.
namespace profilerReadout {
  inline constexpr double windowSeconds = 1.0;

  // One decimal place, no locale; negative and NaN values read as 0.
  [[nodiscard]] inline std::string formatTenths(const double value)
  {
    const auto tenths = static_cast<uint64_t>((value > 0.0 ? value : 0.0) * 10.0 + 0.5);

    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
  }

  [[nodiscard]] inline std::string serverLine(const ServerLaneInput& server)
  {
    switch (server.state)
    {
      case ServerLaneState::live: break;
      case ServerLaneState::unsupported: return "srv n/a";
      case ServerLaneState::disconnected: return "srv disconnected";
      case ServerLaneState::waiting:
      case ServerLaneState::off: return "srv waiting";
    }

    profilerServerLane::TickSummary summary;

    if (server.history != nullptr)
    {
      const std::size_t window = profile::windowSize(*server.history, windowSeconds);
      summary = profilerServerLane::summarizeTicks(
        server.tickMicros,
        profilerServerLane::windowTickCount(*server.history, window, server.ticksCounter, server.tickMicros.size()));
    }

    return "srv " + formatTenths(summary.averageMillis) + " ms  (p95 " + formatTenths(summary.p95Millis) + ")";
  }

  // Frame time and rate over the editor lane's last second, the server's tick time, and the server's send rate.
  [[nodiscard]] inline std::vector<std::string> lines(const profile::History& editorHistory,
                                                      const ServerLaneInput& server)
  {
    const auto frame = profile::summarizeLane(editorHistory, profile::windowSize(editorHistory, windowSeconds));
    const auto fps = static_cast<uint64_t>(frame.samplesPerSecond + 0.5);

    std::vector<std::string> result;
    result.push_back(formatTenths(frame.averageMillis) + " ms  " + std::to_string(fps) + " fps");
    result.push_back(serverLine(server));

    if (server.state == ServerLaneState::live)
    {
      result.push_back("net " + profilerServerLane::formatRate(server.sent.bytesPerSecond));
    }

    return result;
  }
}

#endif
