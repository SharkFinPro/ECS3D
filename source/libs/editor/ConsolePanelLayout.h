#ifndef CONSOLEPANELLAYOUT_H
#define CONSOLEPANELLAYOUT_H

#include <algorithm>

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

// The search line: the Filters button, the search field and, when the search field keeps at least minSearchWidth
// beside them, the Copy/Clear/Auto-scroll group. Otherwise the group wraps onto a line of its own and the search
// field takes what the Filters button leaves.
struct ConsoleHeaderLayout {
  float searchWidth = 0.0f;
  bool actionsOnSearchLine = false;
};

[[nodiscard]] inline ConsoleHeaderLayout consoleHeaderLayout(const float availWidth, const float gap,
                                                             const float filtersButtonWidth, const float actionsWidth,
                                                             const float minSearchWidth)
{
  const float withActions = availWidth - filtersButtonWidth - actionsWidth - 2.0f * gap;

  if (withActions >= minSearchWidth)
  {
    return { withActions, true };
  }

  return { std::max(availWidth - filtersButtonWidth - gap, 1.0f), false };
}

#endif
