#ifndef CONSOLEPANELLAYOUT_H
#define CONSOLEPANELLAYOUT_H

struct LinePlacement {
  bool sameLine = false;
  float usedWidth = 0.0f;
};

// Places an item of itemWidth on a line that already holds usedWidth of content: after a gap on the same line
// when it fits in availWidth, otherwise at the start of a new one. The first item on a line always fits, so a
// toggle wider than the panel is never dropped.
[[nodiscard]] inline LinePlacement placeOnLine(const float usedWidth, const float gap, const float itemWidth,
                                               const float availWidth)
{
  if (usedWidth <= 0.0f)
  {
    return { false, itemWidth };
  }

  if (usedWidth + gap + itemWidth <= availWidth)
  {
    return { true, usedWidth + gap + itemWidth };
  }

  return { false, itemWidth };
}

#endif
