#ifndef DEFAULTDOCKLAYOUT_H
#define DEFAULTDOCKLAYOUT_H

#include "DockLayoutFit.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// The default dock layout: side and bottom panels take what their content needs and the scene view keeps the rest.
// Sizes are pixels of the dock space; the base sizes are unscaled and follow the content scale like the panels'
// own contents do.

inline constexpr float defaultDockLeftWidth = 230.0f;
inline constexpr float defaultDockRightWidth = 290.0f;
inline constexpr float defaultDockBottomHeight = 170.0f;

// Both side panels together take at most this share of the width, the bottom panels this share of the height.
inline constexpr float defaultDockSidesFraction = 0.28f;
inline constexpr float defaultDockBottomFraction = 0.17f;

// The panels' own minimum sizes in unscaled pixels, on the one axis each is a floor for. The app applies them and
// the layout tests read them, so the two cannot drift.
inline constexpr float sceneStatusBaseMinimumHeight = 88.0f;
inline constexpr float objectsBaseFloor = 200.0f;
inline constexpr float inspectorBaseFloor = 250.0f;
inline constexpr float sceneScriptsBaseFloor = 250.0f;
inline constexpr float assetsBaseFloor = 150.0f;
inline constexpr float projectErrorsBaseFloor = 80.0f;
inline constexpr float consoleBaseFloor = 100.0f;
inline constexpr float profilerBaseFloor = 100.0f;

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

// What one frame shows of the dock tree: the visible side nodes (roles parallel to sides), every node that exists,
// and the window it sits in.
struct DockFrame
{
  float width = 0.0f;
  float height = 0.0f;
  float contentScale = 1.0f;
  bool settling = false;
  bool dragging = false;
  std::vector<DockSideSize> sides;
  std::vector<DockSideRole> roles;
  std::vector<std::uint32_t> existing;
};

// The sizes to write back to the side nodes this frame; empty when the layout is left alone.
struct DockUpdate
{
  std::vector<DockSideSize> sizes;
};

// Remembers the side proportions across frames and decides what a frame does with them. A layout still at its
// defaults (nothing dragged since they were applied) follows a new window size or content scale to the default
// pixel sizes; once a drag moved or added a node, the remembered fractions are rescaled instead. Only the end of a
// drag clears the defaults, so the engine's own startup rebuilds never do.
class DockLayoutState
{
public:
  void defaultsApplied() noexcept
  {
    m_defaultsUntouched = true;
  }

  [[nodiscard]] bool defaultsUntouched() const noexcept
  {
    return m_defaultsUntouched;
  }

  [[nodiscard]] bool tracks(const std::vector<DockSideSize>& sides) const
  {
    return m_proportions.tracks(sides);
  }

  [[nodiscard]] DockUpdate update(const DockFrame& frame)
  {
    const bool layoutChanged = frame.settling || !m_proportions.tracks(frame.sides);
    const DockLayoutStep step = m_gate.update(frame.width, frame.height, frame.dragging, layoutChanged);
    const bool scaleChanged = m_hasScale && frame.contentScale != m_scale;

    if (frame.width > 0.0f && frame.height > 0.0f)
    {
      m_scale = frame.contentScale;
      m_hasScale = true;
    }

    return DockUpdate{apply(step, scaleChanged, frame)};
  }

private:
  DockProportions m_proportions;
  DockResizeGate m_gate;
  std::vector<DockSideSize> m_dragStart;
  bool m_defaultsUntouched = false;
  float m_scale = 1.0f;
  bool m_hasScale = false;

  void remember(const DockFrame& frame)
  {
    m_proportions.rememberChanges(frame.sides, frame.settling ? std::vector<DockSideSize>{} : frame.sides,
                                  frame.existing, frame.width, frame.height);
  }

  [[nodiscard]] std::vector<DockSideSize> rescale(const DockFrame& frame)
  {
    if (m_defaultsUntouched)
    {
      const DefaultDockSizes sizes = defaultDockSizes(frame.width, frame.height, frame.contentScale);

      for (std::size_t i = 0; i < frame.sides.size() && i < frame.roles.size(); ++i)
      {
        const float fraction = defaultDockFraction(frame.roles[i], sizes, frame.width, frame.height);

        if (fraction > 0.0f)
        {
          m_proportions.setFraction(frame.sides[i].id, frame.sides[i].axis, fraction);
        }
      }
    }

    return m_proportions.fit(frame.width, frame.height);
  }

  [[nodiscard]] std::vector<DockSideSize> apply(const DockLayoutStep step, const bool scaleChanged,
                                                const DockFrame& frame)
  {
    switch (step)
    {
    case DockLayoutStep::remember:
      remember(frame);
      return scaleChanged && m_defaultsUntouched ? rescale(frame) : std::vector<DockSideSize>{};

    case DockLayoutStep::rememberAndRescale:
      remember(frame);

      // During the settle frames the engine is still rebuilding the tree from the percents, so only a layout
      // at its defaults is written back, to the default sizes of the window it has now.
      return !frame.settling || m_defaultsUntouched ? rescale(frame) : std::vector<DockSideSize>{};

    case DockLayoutStep::rescale:
      return rescale(frame);

    case DockLayoutStep::beginDrag:
      m_dragStart = frame.sides;
      return {};

    case DockLayoutStep::endDrag:
      m_proportions.rememberChanges(frame.sides, m_dragStart, frame.existing, frame.width, frame.height);

      if (dockSidesMoved(m_dragStart, frame.sides))
      {
        m_defaultsUntouched = false;
      }
      return {};

    case DockLayoutStep::none:
      return scaleChanged && m_defaultsUntouched ? rescale(frame) : std::vector<DockSideSize>{};
    }

    return {};
  }
};

#endif
