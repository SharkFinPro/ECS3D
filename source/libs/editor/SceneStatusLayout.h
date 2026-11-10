#ifndef SCENESTATUSLAYOUT_H
#define SCENESTATUSLAYOUT_H

#include <algorithm>

// Decides where each control group of the Scene Status toolbar goes so the row wraps instead of running
// off the panel. Groups are placed in order with the gap that precedes them.
class ToolbarFlow
{
public:
  explicit ToolbarFlow(const float availableWidth) noexcept
    : m_available(availableWidth)
  {}

  // True when the group continues the current row (the caller then draws a same-line gap); false when it
  // starts a row. A group wider than the whole row still starts a row of its own.
  [[nodiscard]] bool place(const float width, const float gap) noexcept
  {
    if (m_rows == 0)
    {
      m_rows = 1;
      m_used = width;
      return false;
    }

    if (m_used + gap + width <= m_available)
    {
      m_used += gap + width;
      return true;
    }

    ++m_rows;
    m_used = width;
    return false;
  }

  [[nodiscard]] int rows() const noexcept
  {
    return m_rows;
  }

private:
  float m_available;
  float m_used = 0.0f;
  int m_rows = 0;
};

[[nodiscard]] inline float toolbarPanelHeight(const int rows, const float titleBarHeight, const float windowPaddingY,
                                              const float rowHeightWithSpacing) noexcept
{
  return titleBarHeight + 2.0f * windowPaddingY + static_cast<float>(rows) * rowHeightWithSpacing;
}

// One width for a set of buttons, sized so the widest label keeps its frame padding on both sides.
[[nodiscard]] inline float uniformLabelButtonWidth(const float widestTextWidth, const float framePaddingX,
                                                   const float minimumWidth) noexcept
{
  return std::max(minimumWidth, widestTextWidth + 2.0f * framePaddingX);
}

#endif
