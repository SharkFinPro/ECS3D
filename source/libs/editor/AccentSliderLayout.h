#ifndef ACCENTSLIDERLAYOUT_H
#define ACCENTSLIDERLAYOUT_H

#include <algorithm>

// Horizontal geometry of an inspector slider row: the track runs from `trackStart` to `trackEnd`, then a gap
// holds the thumb's glow ring, then the value column ends at `valueRight`.
struct SliderLayout
{
  float trackStart = 0.0f;
  float trackEnd = 0.0f;
  float valueRight = 0.0f;

  [[nodiscard]] float trackLength() const
  {
    return trackEnd - trackStart;
  }
};

// Gap between the track end and the value column. The thumb's glow ring has radius 9 and is centered on
// trackEnd at the maximum, so the gap clears it with room to spare.
inline constexpr float sliderValueGap = 14.0f;

// A row narrower than the value column and gap gets a zero-length track rather than a negative one.
[[nodiscard]] inline SliderLayout sliderLayout(const float rowX, const float rowWidth, const float valueColumnWidth,
                                               const float gap = sliderValueGap)
{
  SliderLayout layout;
  layout.trackStart = rowX;
  layout.valueRight = rowX + std::max(0.0f, rowWidth);
  layout.trackEnd = std::max(layout.trackStart, layout.valueRight - valueColumnWidth - gap);
  return layout;
}

// Maps a mouse x to a value in [min, max]. Positions beyond either end of the track clamp, so dragging over
// the number keeps the maximum. A zero-length track yields min.
[[nodiscard]] inline float sliderValueAt(const float mouseX, const SliderLayout& layout, const float min,
                                         const float max)
{
  const float length = layout.trackLength();
  if (length <= 0.0f)
  {
    return min;
  }

  const float t = std::clamp((mouseX - layout.trackStart) / length, 0.0f, 1.0f);
  return min + (max - min) * t;
}

#endif
