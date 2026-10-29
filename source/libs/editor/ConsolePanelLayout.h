#ifndef CONSOLEPANELLAYOUT_H
#define CONSOLEPANELLAYOUT_H

// Whether an item of itemWidth still fits on a line that already holds usedWidth of content, after a gap, in
// availWidth. The first item on a line always fits, so a toggle wider than the panel is never dropped.
[[nodiscard]] inline bool fitsOnLine(const float usedWidth, const float gap, const float itemWidth,
                                     const float availWidth)
{
  return usedWidth <= 0.0f || usedWidth + gap + itemWidth <= availWidth;
}

#endif
