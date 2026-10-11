#ifndef PLAYCONTROLS_H
#define PLAYCONTROLS_H

#include <scenes/SceneManager.h>

struct PlayControlsAvailability
{
  bool play = false;
  bool pause = false;
  bool stop = false;
};

// Play doubles as resume while paused, since the server's start op resumes a paused scene.
[[nodiscard]] constexpr PlayControlsAvailability playControlsAvailability(const SceneStatus status,
                                                                          const bool serverEditable) noexcept
{
  if (!serverEditable)
  {
    return {};
  }

  return {
    .play = status != SceneStatus::running,
    .pause = status == SceneStatus::running,
    .stop = status != SceneStatus::stopped
  };
}

[[nodiscard]] constexpr const char* playControlLabel(const SceneStatus status) noexcept
{
  return status == SceneStatus::paused ? "Resume" : "Play";
}

#endif
