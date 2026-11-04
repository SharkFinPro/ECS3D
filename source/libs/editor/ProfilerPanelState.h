#ifndef PROFILERPANELSTATE_H
#define PROFILERPANELSTATE_H

#include <ProfilerView.h>
#include <ProfileStats.h>
#include <SettingsStore.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The Profiler panel's persisted choices and its pause/selection rules, kept free of ImGui so they can be tested.
namespace profilerPanelState {
  inline constexpr std::array<int, 3> windowChoicesSeconds{1, 5, 10};
  inline constexpr int defaultWindowSeconds = 5;

  // 0 means automatic; otherwise one of profile::fixedScalesMillis.
  inline constexpr double autoScale = 0.0;

  inline constexpr const char* windowKey = "editor.profiler.window";
  inline constexpr const char* scaleKey = "editor.profiler.scale";
  inline constexpr const char* hiddenGroupsKey = "editor.profiler.hiddenGroups";

  [[nodiscard]] inline int readWindowSeconds(const SettingsStore& settings)
  {
    const int seconds = settings.get<int>(windowKey, defaultWindowSeconds);

    if (std::ranges::find(windowChoicesSeconds, seconds) == windowChoicesSeconds.end())
    {
      return defaultWindowSeconds;
    }

    return seconds;
  }

  inline void writeWindowSeconds(SettingsStore& settings, const int seconds)
  {
    settings.set(windowKey, seconds);
  }

  [[nodiscard]] inline double readScaleMillis(const SettingsStore& settings)
  {
    const double stored = settings.get<double>(scaleKey, autoScale);

    if (stored == autoScale)
    {
      return autoScale;
    }

    for (const double fixed : profile::fixedScalesMillis)
    {
      if (std::abs(stored - fixed) < 1e-6)
      {
        return fixed;
      }
    }

    return autoScale;
  }

  inline void writeScaleMillis(SettingsStore& settings, const double millis)
  {
    settings.set(scaleKey, millis);
  }

  // Bit i of the mask is group i of the frame lane's display groups. Unknown ids are ignored.
  [[nodiscard]] inline uint32_t readHiddenMask(const SettingsStore& settings)
  {
    const auto ids = settings.get<std::vector<std::string>>(hiddenGroupsKey, {});
    const auto groups = profile::displayGroups(profile::LaneKind::frame);
    uint32_t mask = 0;

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      if (std::ranges::find(ids, std::string(groups[i].id)) != ids.end())
      {
        mask |= 1u << i;
      }
    }

    return mask;
  }

  inline void writeHiddenMask(SettingsStore& settings, const uint32_t mask)
  {
    const auto groups = profile::displayGroups(profile::LaneKind::frame);
    std::vector<std::string> ids;

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      if ((mask & (1u << i)) != 0)
      {
        ids.emplace_back(groups[i].id);
      }
    }

    settings.set(hiddenGroupsKey, ids);
  }

  // The 99th percentile of wall time over the newest `window` samples, which the automatic scale is chosen from.
  [[nodiscard]] inline uint32_t p99WallMicros(const profile::History& history, const std::size_t window)
  {
    const std::size_t count = std::min(window, history.size());
    std::vector<uint32_t> walls;
    walls.reserve(count);

    for (std::size_t i = history.size() - count; i < history.size(); ++i)
    {
      walls.push_back(history.at(i).wallMicros);
    }

    return profile::percentile(std::move(walls), 99.0);
  }

  // Clicking a bar selects that sample and pauses; clicking the selected bar again, or Resume, goes back to live.
  struct Interaction {
    bool paused = false;
    uint64_t selectedSequence = 0;

    void clickColumn(const uint64_t sequence)
    {
      if (sequence == 0)
      {
        return;
      }

      if (paused && selectedSequence == sequence)
      {
        resume();
        return;
      }

      paused = true;
      selectedSequence = sequence;
    }

    void togglePause()
    {
      if (paused)
      {
        resume();
        return;
      }

      paused = true;
    }

    void resume()
    {
      paused = false;
      selectedSequence = 0;
    }

    // A closed panel reopens live.
    void close()
    {
      resume();
    }
  };
}

#endif
