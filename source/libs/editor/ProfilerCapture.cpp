#include "ProfilerCapture.h"
#include <ProjectPaths.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <string_view>
#include <system_error>

namespace {
  constexpr const char* kindName(const profile::LaneKind kind)
  {
    return kind == profile::LaneKind::server ? "server" : "frame";
  }

  struct CivilTime {
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
  };

  // Days since 1970-01-01 to a proleptic Gregorian date, so formatting needs neither the C locale nor a time zone.
  CivilTime civilTime(const std::chrono::system_clock::time_point now)
  {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(now).time_since_epoch().count();
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;

    if (rest < 0)
    {
      rest += 86400;
      --days;
    }

    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthIndex = (5 * dayOfYear + 2) / 153;

    CivilTime time;
    time.day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    time.month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    time.year = static_cast<int>(yearOfEra + era * 400) + (time.month <= 2 ? 1 : 0);
    time.hour = static_cast<unsigned>(rest / 3600);
    time.minute = static_cast<unsigned>(rest % 3600 / 60);
    time.second = static_cast<unsigned>(rest % 60);

    return time;
  }

  std::string digits(const int64_t value, const std::size_t width)
  {
    std::array<char, 24> buffer{};
    const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value).ptr;
    std::string text(buffer.data(), end);

    if (text.size() < width)
    {
      text.insert(0, width - text.size(), '0');
    }

    return text;
  }

  std::string number(const int64_t value)
  {
    return digits(value, 0);
  }

  void appendHeader(std::string& csv)
  {
    csv += "lane,sequence,wallMicros";

    for (const auto kind : {profile::LaneKind::frame, profile::LaneKind::server})
    {
      const std::string prefix = std::string(kindName(kind)) + ".";

      for (const auto& group : profile::displayGroups(kind))
      {
        csv += "," + prefix + std::string(group.id);
      }
    }

    csv += ",ticks,droppedSteps\n";
  }

  void appendLane(std::string& csv, const CaptureLane& lane)
  {
    if (lane.history == nullptr || lane.history->size() == 0)
    {
      return;
    }

    const auto groups = profile::groupOfNodes(lane.kind, lane.nodes);
    const std::size_t count = std::min(lane.window, lane.history->size());
    const std::string name = csvField(lane.name);

    for (std::size_t i = lane.history->size() - count; i < lane.history->size(); ++i)
    {
      const auto& sample = lane.history->at(i);
      const auto micros = profile::groupMicros(sample, lane.nodes, groups, 0);

      csv += name + "," + number(static_cast<int64_t>(sample.sequence)) + "," + number(sample.wallMicros);

      for (const auto kind : {profile::LaneKind::frame, profile::LaneKind::server})
      {
        const std::size_t groupCount = profile::displayGroups(kind).size();

        for (std::size_t g = 0; g < groupCount; ++g)
        {
          csv += ",";

          if (kind == lane.kind)
          {
            csv += number(micros[g]);
          }
        }
      }

      for (const auto& counter : {lane.ticksCounter, lane.droppedStepsCounter})
      {
        csv += ",";

        if (counter && *counter < sample.counters.size())
        {
          csv += number(sample.counters[*counter]);
        }
      }

      csv += "\n";
    }
  }

  bool writeFile(const std::filesystem::path& path, const std::string& content)
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.close();

    return !file.fail();
  }
}

const char* captureBuildType()
{
#ifdef NDEBUG
  return "Release";
#else
  return "Debug";
#endif
}

const char* captureServerStateName(const ServerLaneState state)
{
  switch (state)
  {
    case ServerLaneState::off: return "off";
    case ServerLaneState::waiting: return "waiting";
    case ServerLaneState::live: return "live";
    case ServerLaneState::unsupported: return "unsupported";
    case ServerLaneState::disconnected: return "disconnected";
  }

  return "off";
}

bool captureHasSamples(const CaptureInput& input)
{
  return std::ranges::any_of(input.lanes, [](const CaptureLane& lane) {
    return lane.history != nullptr && lane.history->size() != 0 && lane.window != 0;
  });
}

const char* capturePlatform()
{
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}

std::string csvField(const std::string& text)
{
  if (text.find_first_of(",\"\r\n") == std::string::npos)
  {
    return text;
  }

  std::string quoted = "\"";

  for (const char c : text)
  {
    quoted += c;

    if (c == '"')
    {
      quoted += '"';
    }
  }

  return quoted + "\"";
}

std::string captureCsv(const CaptureInput& input)
{
  std::string csv;
  appendHeader(csv);

  for (const auto kind : {profile::LaneKind::frame, profile::LaneKind::server})
  {
    for (const auto& lane : input.lanes)
    {
      if (lane.kind == kind)
      {
        appendLane(csv, lane);
      }
    }
  }

  return csv;
}

nlohmann::json captureSidecar(const CaptureInput& input)
{
  nlohmann::json lanes = nlohmann::json::array();

  for (const auto& lane : input.lanes)
  {
    nlohmann::json nodes = nlohmann::json::array();

    for (std::size_t i = 0; i < lane.nodes.size(); ++i)
    {
      nodes.push_back({{"id", i}, {"parent", lane.nodes[i].parent}, {"name", lane.nodes[i].name}});
    }

    const std::size_t available = lane.history != nullptr ? lane.history->size() : 0;
    lanes.push_back({{"name", lane.name},
                     {"kind", kindName(lane.kind)},
                     {"samples", std::min(lane.window, available)},
                     {"nodes", nodes}});
  }

  nlohmann::json snapshots = nlohmann::json::array();

  for (const auto& row : input.snapshots)
  {
    snapshots.push_back({{"secondsAgo", row.secondsAgo},
                         {"bytes", row.bytes},
                         {"reason", row.reason},
                         {"buildMillis", row.buildMillis},
                         {"recipients", row.recipients}});
  }

  nlohmann::json sidecar{{"engineVersion", input.engineVersion},
                         {"buildType", input.buildType},
                         {"platform", input.platform},
                         {"app", input.app},
                         {"sceneName", input.sceneName},
                         {"objectCount", input.objectCount},
                         {"connectionCount", input.connectionCount},
                         {"paused", input.paused},
                         {"capturedAt", input.capturedAt},
                         {"lanes", lanes},
                         {"snapshots", snapshots}};

  if (!input.serverState.empty())
  {
    sidecar["serverState"] = input.serverState;
  }

  return sidecar;
}

std::string captureBaseName(const std::string& app, const std::chrono::system_clock::time_point now)
{
  const auto time = civilTime(now);

  return app + "-" + digits(time.year, 4) + digits(time.month, 2) + digits(time.day, 2) + "-" +
         digits(time.hour, 2) + digits(time.minute, 2) + digits(time.second, 2);
}

std::string captureTimestamp(const std::chrono::system_clock::time_point now)
{
  const auto time = civilTime(now);

  return digits(time.year, 4) + "-" + digits(time.month, 2) + "-" + digits(time.day, 2) + "T" +
         digits(time.hour, 2) + ":" + digits(time.minute, 2) + ":" + digits(time.second, 2) + "Z";
}

CaptureResult writeCapture(CaptureInput input, const std::filesystem::path& folder,
                           const std::chrono::system_clock::time_point now)
{
  CaptureResult result;

  if (!captureHasSamples(input))
  {
    result.error = "Nothing to save yet";
    return result;
  }

  std::filesystem::path csvPath;
  std::filesystem::path jsonPath;

  try
  {
    input.capturedAt = captureTimestamp(now);

    std::error_code error;
    std::filesystem::create_directories(folder, error);

    if (error)
    {
      result.error = "could not create " + pathToUtf8(folder) + ": " + error.message();
      return result;
    }

    const std::string base = captureBaseName(input.app, now);
    csvPath = folder / (base + ".csv");
    jsonPath = folder / (base + ".json");

    if (!writeFile(csvPath, captureCsv(input)))
    {
      result.error = "could not write " + pathToUtf8(csvPath);
      return result;
    }

    // Node names and snapshot reasons can come from a remote server, so invalid UTF-8 is replaced, not thrown on.
    const std::string sidecar =
      captureSidecar(input).dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";

    if (!writeFile(jsonPath, sidecar))
    {
      result.error = "could not write " + pathToUtf8(jsonPath);
    }
  }
  catch (const std::exception& exception)
  {
    result.error = std::string("could not save the capture: ") + exception.what();
  }

  if (!result.error.empty())
  {
    std::error_code ignored;
    std::filesystem::remove(csvPath, ignored);

    if (std::filesystem::is_regular_file(jsonPath, ignored))
    {
      std::filesystem::remove(jsonPath, ignored);
    }

    return result;
  }

  result.csvPath = csvPath;
  return result;
}
