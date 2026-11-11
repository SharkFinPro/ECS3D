#ifndef DOCKLAYOUTFIT_H
#define DOCKLAYOUTFIT_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

// ImGui keeps a docked side node at its absolute size when the host shrinks and hands what is left to the
// central node, so a narrow window squeezes the scene view. These helpers keep each side node at the same
// fraction of the dock space instead, without touching ImGui.

struct DockSideSize
{
  std::uint32_t id = 0;
  int axis = 0;
  float size = 0.0f;
};

// Side panels may take at most this share of an axis together, so the central node keeps the rest.
inline constexpr float dockMinimumCentralFraction = 0.4f;

// A panel's own height or width floor, lowered so it never takes more than maxFraction of the window. Unscaled
// pixels in and out; a window without a usable extent keeps the base floor.
[[nodiscard]] inline float dockFloorForExtent(const float baseFloor, const float extent, const float maxFraction) noexcept
{
  if (extent <= 0.0f)
  {
    return baseFloor;
  }

  return std::min(baseFloor, extent * maxFraction);
}

// The fraction of the dock space each non-central node occupies along its split axis. The fractions, not the
// node sizes, are what a resize preserves: a size clamped for a small window must not become the remembered size.
class DockProportions
{
public:
  void remember(const std::vector<DockSideSize>& nodes, const float extentX, const float extentY)
  {
    if (extentX <= 0.0f || extentY <= 0.0f)
    {
      return;
    }

    m_entries.clear();

    for (const auto& node : nodes)
    {
      const float extent = node.axis == 0 ? extentX : extentY;

      if (node.size > 0.0f)
      {
        m_entries[node.id] = Entry{node.axis, node.size / extent};
      }
    }
  }

  // The sizes that restore the remembered fractions in a dock space of this extent, shrunk together along an
  // axis when they would leave the central node less than minimumCentralFraction. Empty for an unusable extent.
  [[nodiscard]] std::vector<DockSideSize> fit(const float extentX, const float extentY,
                                              const float minimumCentralFraction = dockMinimumCentralFraction) const
  {
    std::vector<DockSideSize> sizes;

    if (extentX <= 0.0f || extentY <= 0.0f)
    {
      return sizes;
    }

    float totals[2] = {0.0f, 0.0f};

    for (const auto& [id, entry] : m_entries)
    {
      totals[entry.axis] += entry.fraction;
    }

    const float budget = std::max(1.0f - minimumCentralFraction, 0.0f);

    for (const auto& [id, entry] : m_entries)
    {
      const float total = totals[entry.axis];
      const float scale = total > budget ? budget / total : 1.0f;
      const float extent = entry.axis == 0 ? extentX : extentY;

      sizes.push_back(DockSideSize{id, entry.axis, entry.fraction * scale * extent});
    }

    return sizes;
  }

  // Whether the remembered nodes are exactly these, so a docking change that adds or removes one is noticed.
  [[nodiscard]] bool tracks(const std::vector<DockSideSize>& nodes) const
  {
    if (nodes.size() != m_entries.size())
    {
      return false;
    }

    return std::ranges::all_of(nodes, [this](const DockSideSize& node)
    {
      const auto entry = m_entries.find(node.id);
      return entry != m_entries.end() && entry->second.axis == node.axis;
    });
  }

private:
  struct Entry
  {
    int axis = 0;
    float fraction = 0.0f;
  };

  std::map<std::uint32_t, Entry> m_entries;
};

enum class DockLayoutStep
{
  none,
  remember,
  rescale
};

// Decides each frame whether to leave the dock layout alone, remember its proportions or rescale it to a new
// window size. Only an actual size change rescales, and a splitter drag only ever ends in a remember.
class DockResizeGate
{
public:
  [[nodiscard]] DockLayoutStep update(const float width, const float height, const bool userDraggingLayout,
                                      const bool layoutChanged) noexcept
  {
    if (width <= 0.0f || height <= 0.0f)
    {
      return DockLayoutStep::none;
    }

    if (userDraggingLayout)
    {
      m_wasDragging = true;
      return DockLayoutStep::none;
    }

    const bool sizeChanged = width != m_width || height != m_height;

    m_width = width;
    m_height = height;

    if (m_wasDragging || layoutChanged || !m_hasSize)
    {
      m_wasDragging = false;
      m_hasSize = true;
      return DockLayoutStep::remember;
    }

    return sizeChanged ? DockLayoutStep::rescale : DockLayoutStep::none;
  }

private:
  float m_width = 0.0f;
  float m_height = 0.0f;
  bool m_hasSize = false;
  bool m_wasDragging = false;
};

#endif
