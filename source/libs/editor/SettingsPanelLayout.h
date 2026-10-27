#ifndef SETTINGSPANELLAYOUT_H
#define SETTINGSPANELLAYOUT_H

#include <algorithm>
#include <span>

struct SettingsLayoutSize {
  float x = 0.0f;
  float y = 0.0f;
};

// Em multiples rather than pixels, so the window tracks the font and therefore the DPI scale.
inline constexpr float settingsWindowMinWidthEm = 58.5f;
inline constexpr float settingsWindowHeightEm = 40.0f;

// Keeps a first-use window from opening flush against, or larger than, the editor window.
inline constexpr float settingsWindowViewportFraction = 0.9f;

// The width of buttons laid out on one line with SameLine(): each label plus its frame padding on both
// sides, with one item spacing between neighbors.
[[nodiscard]] inline float buttonRowWidth(const std::span<const float> labelWidths, const float framePaddingX,
                                          const float itemSpacingX)
{
  if (labelWidths.empty())
  {
    return 0.0f;
  }

  float width = itemSpacingX * static_cast<float>(labelWidths.size() - 1);
  for (const float labelWidth : labelWidths)
  {
    width += labelWidth + 2.0f * framePaddingX;
  }

  return width;
}

// A viewport axis that is not positive means the window has not reported its size yet (the first frames
// after it opens), so it imposes no cap rather than collapsing the window to nothing.
[[nodiscard]] inline SettingsLayoutSize settingsWindowDefaultSize(const float contentWidth, const float fontSize,
                                                                  const float viewportWidth,
                                                                  const float viewportHeight)
{
  SettingsLayoutSize size{ std::max(contentWidth, settingsWindowMinWidthEm * fontSize),
                           settingsWindowHeightEm * fontSize };

  if (viewportWidth > 0.0f)
  {
    size.x = std::min(size.x, viewportWidth * settingsWindowViewportFraction);
  }

  if (viewportHeight > 0.0f)
  {
    size.y = std::min(size.y, viewportHeight * settingsWindowViewportFraction);
  }

  return size;
}

#endif
