#ifndef DEFAULTDOCKLAYOUT_H
#define DEFAULTDOCKLAYOUT_H

#include <algorithm>

// The default dock layout: side and bottom panels take what their content needs and the scene view keeps the rest.
// Sizes are pixels of the dock space; the base sizes are unscaled and follow the content scale like the panels'
// own contents do.

inline constexpr float defaultDockLeftWidth = 280.0f;
inline constexpr float defaultDockRightWidth = 360.0f;
inline constexpr float defaultDockBottomHeight = 200.0f;

// Both side panels together take at most this share of the width, the bottom panels this share of the height.
inline constexpr float defaultDockSidesFraction = 0.35f;
inline constexpr float defaultDockBottomFraction = 0.22f;

struct DefaultDockSizes
{
  float left = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

// Each split is cut from what the previous one left, so these are relative to the remainder (the order the
// engine splits in: left, right, top, bottom).
struct DefaultDockPercents
{
  float left = 0.0f;
  float right = 0.0f;
  float top = 0.0f;
  float bottom = 0.0f;
};

enum class DockSideRole
{
  left,
  right,
  bottom,
  other
};

[[nodiscard]] inline DefaultDockSizes defaultDockSizes(const float width, const float height,
                                                       const float contentScale) noexcept
{
  DefaultDockSizes sizes;

  if (width <= 0.0f || height <= 0.0f)
  {
    return sizes;
  }

  const float scale = std::max(contentScale, 0.01f);

  sizes.left = defaultDockLeftWidth * scale;
  sizes.right = defaultDockRightWidth * scale;

  const float sidesBudget = defaultDockSidesFraction * width;
  const float sides = sizes.left + sizes.right;

  if (sides > sidesBudget)
  {
    sizes.left *= sidesBudget / sides;
    sizes.right *= sidesBudget / sides;
  }

  sizes.bottom = std::min(defaultDockBottomHeight * scale, defaultDockBottomFraction * height);

  return sizes;
}

[[nodiscard]] inline DefaultDockPercents defaultDockPercents(const DefaultDockSizes& sizes, const float width,
                                                             const float height, const float topHeight) noexcept
{
  DefaultDockPercents percents;

  if (width <= sizes.left || height <= topHeight)
  {
    return percents;
  }

  percents.left = sizes.left / width;
  percents.right = sizes.right / (width - sizes.left);
  percents.top = topHeight / height;
  percents.bottom = sizes.bottom / (height - topHeight);

  return percents;
}

// Which default panel a side node is, from its split axis and where its center sits against the dock space's.
[[nodiscard]] inline DockSideRole dockSideRole(const int axis, const float nodeCenter, const float rootCenter) noexcept
{
  if (axis == 0)
  {
    return nodeCenter < rootCenter ? DockSideRole::left : DockSideRole::right;
  }

  return nodeCenter > rootCenter ? DockSideRole::bottom : DockSideRole::other;
}

// The share of the dock space a default panel takes along its split axis; zero for a node that is not one.
[[nodiscard]] inline float defaultDockFraction(const DockSideRole role, const DefaultDockSizes& sizes,
                                               const float width, const float height) noexcept
{
  if (width <= 0.0f || height <= 0.0f)
  {
    return 0.0f;
  }

  switch (role)
  {
  case DockSideRole::left:
    return sizes.left / width;

  case DockSideRole::right:
    return sizes.right / width;

  case DockSideRole::bottom:
    return sizes.bottom / height;

  case DockSideRole::other:
    break;
  }

  return 0.0f;
}

#endif
