#ifndef PROFILERCAPTURE_H
#define PROFILERCAPTURE_H

#include "ProfilerServerLane.h"
#include <History.h>
#include <ProfilerView.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A Profiler capture: the samples the panel shows as a CSV, plus a JSON sidecar describing the machine and build.
// The only project content either file holds is the scene name and object count.
struct CaptureLane {
  std::string name;
  profile::LaneKind kind = profile::LaneKind::frame;
  // May be null (no server lane); a null or empty history writes no rows.
  const profile::History* history = nullptr;
  std::vector<profile::ViewNode> nodes;
  // The newest samples to write.
  std::size_t window = 0;
  std::optional<std::size_t> ticksCounter;
  std::optional<std::size_t> droppedStepsCounter;
};

struct CaptureInput {
  std::string app;
  std::string engineVersion;
  std::string buildType;
  std::string platform;
  std::string sceneName;
  std::size_t objectCount = 0;
  std::size_t connectionCount = 0;
  // The panel was paused, so the rows and counts are those of the moment it paused.
  bool paused = false;
  // Empty when there is no server lane input; otherwise the lane state's name, which marks stale data.
  std::string serverState;
  // ISO 8601 UTC; writeCapture fills it from its clock argument.
  std::string capturedAt;
  std::vector<CaptureLane> lanes;
  std::vector<ServerSnapshotRow> snapshots;
};

struct CaptureResult {
  std::filesystem::path csvPath;
  // Empty on success.
  std::string error;

  [[nodiscard]] bool ok() const
  {
    return error.empty();
  }
};

[[nodiscard]] const char* captureBuildType();

[[nodiscard]] const char* captureServerStateName(ServerLaneState state);

// Whether any lane holds a sample to write.
[[nodiscard]] bool captureHasSamples(const CaptureInput& input);

[[nodiscard]] const char* capturePlatform();

// RFC 4180: a field holding a comma, quote, CR or LF is quoted, with its quotes doubled.
[[nodiscard]] std::string csvField(const std::string& text);

// LF line endings. One row per sample, the editor lane before the server lane, oldest first within a lane.
[[nodiscard]] std::string captureCsv(const CaptureInput& input);

[[nodiscard]] nlohmann::json captureSidecar(const CaptureInput& input);

// "<app>-<yyyyMMdd-HHmmss>" in UTC.
[[nodiscard]] std::string captureBaseName(const std::string& app, std::chrono::system_clock::time_point now);

// "yyyy-MM-ddTHH:mm:ssZ" in UTC.
[[nodiscard]] std::string captureTimestamp(std::chrono::system_clock::time_point now);

// Writes <folder>/<base>.csv and <base>.json, creating the folder. Fails with "Nothing to save yet" when no lane has
// samples, and leaves neither file behind when either write fails.
[[nodiscard]] CaptureResult writeCapture(CaptureInput input, const std::filesystem::path& folder,
                                         std::chrono::system_clock::time_point now);

#endif
