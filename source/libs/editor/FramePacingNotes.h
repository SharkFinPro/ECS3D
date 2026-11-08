#ifndef FRAMEPACINGNOTES_H
#define FRAMEPACINGNOTES_H

// Which explanations the Display section shows under the frame pacing controls.
struct FramePacingNotes {
  // Vsync off was requested but the display only offers modes that wait for the vertical blank.
  bool vsyncOffUnavailable = false;

  // A cap is set while vsync is on, so a cap above the refresh rate has no effect.
  bool capAboveRefreshIgnored = false;
};

// presentWaitsForVblank is whether the mode the swapchain actually presents with is fifo or fifoRelaxed.
[[nodiscard]] inline FramePacingNotes framePacingNotes(const bool vsyncRequested, const bool presentWaitsForVblank,
                                                       const int maxFps)
{
  return {
    .vsyncOffUnavailable = !vsyncRequested && presentWaitsForVblank,
    .capAboveRefreshIgnored = vsyncRequested && maxFps > 0
  };
}

#endif  // FRAMEPACINGNOTES_H
