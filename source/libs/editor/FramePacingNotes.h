#ifndef FRAMEPACINGNOTES_H
#define FRAMEPACINGNOTES_H

// Which explanations the Display section shows under the frame pacing controls.
struct FramePacingNotes {
  // Vsync off was requested but the display only offers modes that wait for the vertical blank.
  bool vsyncOffUnavailable = false;

  // A cap is set while vsync is on, so a cap above the refresh rate has no effect.
  bool capAboveRefreshIgnored = false;
};

// The surface flags say whether the display offers a non-blocking mode. The engine resolves vsync off to immediate,
// then mailbox, then fifo, so only a surface with neither leaves frames waiting for the vertical blank.
[[nodiscard]] inline FramePacingNotes framePacingNotes(const bool vsyncRequested, const bool surfaceHasImmediate,
                                                       const bool surfaceHasMailbox, const int maxFps)
{
  return {
    .vsyncOffUnavailable = !vsyncRequested && !surfaceHasImmediate && !surfaceHasMailbox,
    .capAboveRefreshIgnored = vsyncRequested && maxFps > 0
  };
}

#endif  // FRAMEPACINGNOTES_H
