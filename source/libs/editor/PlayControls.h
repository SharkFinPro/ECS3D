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

  switch (status)
  {
    case SceneStatus::stopped:
      return {.play = true, .pause = false, .stop = false};
    case SceneStatus::running:
      return {.play = false, .pause = true, .stop = true};
    case SceneStatus::paused:
      return {.play = true, .pause = false, .stop = true};
  }

  return {};
}

#endif
