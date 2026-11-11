#ifndef DOCKLAYOUTFIT_H
#define DOCKLAYOUTFIT_H

#include <algorithm>
#include <cmath>
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

// A panel's own floor never takes more than this share of the window width (side panels) or height (bottom
// panels). Two side floors therefore leave the central node at least half of the width.
inline constexpr float dockSideFloorFraction = 0.25f;
inline constexpr float dockBottomFloorFraction = 0.3f;

// Moves smaller than this many pixels are a click, not an edit of the layout.
inline constexpr float dockEditTolerance = 1.0f;

// A panel's own height or width floor, lowered so it never takes more than maxFraction of the window. Unscaled
// pixels in and out; a window without a usable extent keeps the base floor.
[[nodiscard]] inline float dockFloorForExtent(const float baseFloor, const float extent,
                                              const float maxFraction) noexcept
{
  if (extent <= 0.0f)
  {
    return baseFloor;
  }

  return std::min(baseFloor, extent * maxFraction);
}

// Collects the side nodes of a dock tree: a visible child whose visible sibling holds the central node. ImGui
// sizes such a node from its own SizeRef and gives the sibling the remainder. A split with one visible child
// (its sibling closed) passes the walk on to that child. Node is ImGuiDockNode, or any type with the same
// members; excluded(node) keeps a content-sized node, such as the Scene Status toolbar, out of the proportions.
template <typename Node, typename Excluded>
void collectDockSides(Node* node, std::vector<Node*>& nodes, std::vector<DockSideSize>& sides,
                      const Excluded& excluded)
{
  if (node->IsLeafNode())
  {
    return;
  }

  Node* first = node->ChildNodes[0];
  Node* second = node->ChildNodes[1];

  if (!first->IsVisible || !second->IsVisible)
  {
    if (first->IsVisible || second->IsVisible)
    {
      collectDockSides(first->IsVisible ? first : second, nodes, sides, excluded);
    }

    return;
  }

  const int axis = static_cast<int>(node->SplitAxis);

  for (Node* side : {first, second})
  {
    const Node* sibling = side == first ? second : first;

    if (sibling->HasCentralNodeChild && !side->HasCentralNodeChild && !excluded(side))
    {
      nodes.push_back(side);
      sides.push_back(DockSideSize{static_cast<std::uint32_t>(side->ID), axis, side->SizeRef[axis]});
    }

    collectDockSides(side, nodes, sides, excluded);
  }
}

// The fraction of the dock space each side node occupies along its split axis. The fractions, not the node
// sizes, are what a resize preserves: a size the engine clamped for a small window must not become the
// remembered size.
class DockProportions
{
public:
  // Replaces everything remembered with these nodes.
  void remember(const std::vector<DockSideSize>& nodes, const float extentX, const float extentY)
  {
    rememberChanges(nodes, {}, extentX, extentY);
  }

  // Keeps the fraction of every node that is remembered and did not move since `before` (by more than
  // dockEditTolerance), takes the rest from `nodes`, and forgets nodes that are gone. A click that changes
  // nothing therefore keeps the proportions, however far the engine has clamped the sizes meanwhile.
  void rememberChanges(const std::vector<DockSideSize>& nodes, const std::vector<DockSideSize>& before,
                       const float extentX, const float extentY)
  {
    if (extentX <= 0.0f || extentY <= 0.0f)
    {
      return;
    }

    std::map<std::uint32_t, Entry> entries;

    for (const auto& node : nodes)
    {
      const auto remembered = m_entries.find(node.id);
      const auto earlier = std::ranges::find(before, node.id, &DockSideSize::id);
      const bool unchanged = earlier != before.end() && std::fabs(earlier->size - node.size) <= dockEditTolerance;

      if (remembered != m_entries.end() && remembered->second.axis == node.axis && unchanged)
      {
        entries[node.id] = remembered->second;
      }
      else if (node.size > 0.0f)
      {
        entries[node.id] = Entry{node.axis, node.size / (node.axis == 0 ? extentX : extentY)};
      }
    }

    m_entries = std::move(entries);
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
  rescale,
  beginDrag,
  endDrag
};

// Decides each frame whether to leave the dock layout alone, remember its proportions, rescale it to a new
// window size, or bracket a drag of the layout by the user. Only an actual size change rescales.
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
      const bool began = !m_dragging;
      m_dragging = true;
      return began ? DockLayoutStep::beginDrag : DockLayoutStep::none;
    }

    const bool sizeChanged = width != m_width || height != m_height;
    const bool ended = m_dragging;

    m_width = width;
    m_height = height;
    m_dragging = false;

    if (ended)
    {
      return DockLayoutStep::endDrag;
    }

    if (layoutChanged || !m_hasSize)
    {
      m_hasSize = true;
      return DockLayoutStep::remember;
    }

    return sizeChanged ? DockLayoutStep::rescale : DockLayoutStep::none;
  }

private:
  float m_width = 0.0f;
  float m_height = 0.0f;
  bool m_hasSize = false;
  bool m_dragging = false;
};

#endif
