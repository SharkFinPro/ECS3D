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
  inline constexpr const char* serverHiddenGroupsKey = "editor.profiler.serverHiddenGroups";

  // The editor has the server stream statistics while the panel is open or the corner readout is on.
  [[nodiscard]] inline bool wantsServerStats(const bool panelOpen, const bool readoutOn)
  {
    return panelOpen || readoutOn;
  }

  [[nodiscard]] inline const char* hiddenGroupsKeyFor(const profile::LaneKind kind)
  {
    return kind == profile::LaneKind::server ? serverHiddenGroupsKey : hiddenGroupsKey;
  }

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

  // Bit i of the mask is group i of the lane kind's display groups. Unknown ids are ignored.
  [[nodiscard]] inline uint32_t readHiddenMask(const SettingsStore& settings,
                                               const profile::LaneKind kind = profile::LaneKind::frame)
  {
    const auto ids = settings.get<std::vector<std::string>>(hiddenGroupsKeyFor(kind), {});
    const auto groups = profile::displayGroups(kind);
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

  inline void writeHiddenMask(SettingsStore& settings, const uint32_t mask,
                              const profile::LaneKind kind = profile::LaneKind::frame)
  {
    const auto groups = profile::displayGroups(kind);
    std::vector<std::string> ids;

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      if ((mask & (1u << i)) != 0)
      {
        ids.emplace_back(groups[i].id);
      }
    }

    settings.set(hiddenGroupsKeyFor(kind), ids);
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

  // Whether a lane copies its history this frame: only while paused, once, and when it has something to copy.
  [[nodiscard]] inline bool shouldFreeze(const bool paused, const bool alreadyFrozen, const bool hasSource)
  {
    return paused && !alreadyFrozen && hasSource;
  }

  // The server lane's scale never drops below its budget, so the budget line is never pinned to the top edge:
  // below it, the smallest fixed scale that reaches it (or the budget itself when none does).
  [[nodiscard]] inline double serverScaleMillis(const double scaleMillis, const double budgetMillis)
  {
    if (scaleMillis >= budgetMillis)
    {
      return scaleMillis;
    }

    for (const double fixed : profile::fixedScalesMillis)
    {
      if (fixed >= budgetMillis)
      {
        return fixed;
      }
    }

    return budgetMillis;
  }

  // Which lane the Breakdown table shows: the one whose bar was clicked last.
  enum class BreakdownLane : uint8_t { editor, server };

  // Clicking a bar selects that sample in its lane and pauses both lanes; clicking the selected bar again, or Resume,
  // goes back to live. The two lanes keep separate selections.
  struct Interaction {
    bool paused = false;
    uint64_t selectedSequence = 0;
    uint64_t serverSelectedSequence = 0;
    BreakdownLane breakdownLane = BreakdownLane::editor;

    void clickColumn(const uint64_t sequence)
    {
      click(selectedSequence, BreakdownLane::editor, sequence);
    }

    void clickServerColumn(const uint64_t sequence)
    {
      click(serverSelectedSequence, BreakdownLane::server, sequence);
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
      serverSelectedSequence = 0;
      breakdownLane = BreakdownLane::editor;
    }

    // A closed panel reopens live.
    void close()
    {
      resume();
    }

  private:
    void click(uint64_t& selected, const BreakdownLane lane, const uint64_t sequence)
    {
      if (sequence == 0)
      {
        return;
      }

      if (paused && selected == sequence)
      {
        resume();
        return;
      }

      paused = true;
      selected = sequence;
      breakdownLane = lane;
    }
  };
}

#endif
