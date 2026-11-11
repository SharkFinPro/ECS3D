#ifndef DOCKLAYOUTFIT_H
#define DOCKLAYOUTFIT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
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

// The Scene Status toolbar is content-sized only as a leaf of its own in a vertical stack (a split along y).
// Tabbed with other panels, or docked in a side column, it is an ordinary node. Node is ImGuiDockNode, or any
// type with the same members.
template <typename Node>
[[nodiscard]] bool isContentSizedToolbarNode(const Node* node, const bool holdsOnlyTheToolbar)
{
  return holdsOnlyTheToolbar && node->IsLeafNode() && node->ParentNode != nullptr && node->ParentNode->SplitAxis == 1;
}

// The one visible child of a split whose sibling is closed; null when both or neither are visible.
template <typename Node>
[[nodiscard]] Node* visibleOnlyChild(const Node* node)
{
  Node* first = node->ChildNodes[0];
  Node* second = node->ChildNodes[1];

  if (first->IsVisible == second->IsVisible)
  {
    return nullptr;
  }

  return first->IsVisible ? first : second;
}

// A side node is a visible child whose sibling holds the central node. ImGui sizes it from its own SizeRef and
// gives the sibling the remainder, which is why it keeps its pixels when the window shrinks.
template <typename Node, typename Excluded>
[[nodiscard]] bool isDockSideNode(Node* side, const Node* sibling, const Excluded& excluded)
{
  return sibling->HasCentralNodeChild && !side->HasCentralNodeChild && !excluded(side);
}

template <typename Node, typename Excluded>
void collectDockSides(Node* node, std::vector<Node*>& nodes, std::vector<DockSideSize>& sides,
                      const Excluded& excluded);

template <typename Node, typename Excluded>
void collectDockSide(Node* side, const Node* sibling, const int axis, std::vector<Node*>& nodes,
                     std::vector<DockSideSize>& sides, const Excluded& excluded)
{
  if (isDockSideNode(side, sibling, excluded))
  {
    nodes.push_back(side);
    sides.push_back(DockSideSize{static_cast<std::uint32_t>(side->ID), axis, side->SizeRef[axis]});
  }

  collectDockSides(side, nodes, sides, excluded);
}

// Collects the visible side nodes of a dock tree. A split with one visible child (its sibling closed) passes the
// walk on to that child. excluded(node) keeps a content-sized node out of the proportions.
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

  if (!(first->IsVisible && second->IsVisible))
  {
    if (Node* only = visibleOnlyChild(node))
    {
      collectDockSides(only, nodes, sides, excluded);
    }

    return;
  }

  const int axis = static_cast<int>(node->SplitAxis);

  collectDockSide(first, second, axis, nodes, sides, excluded);
  collectDockSide(second, first, axis, nodes, sides, excluded);
}

// Every node of the tree, hidden or not, so a closed panel's node can be told from one that is gone.
template <typename Node>
void collectAllDockNodes(Node* node, std::vector<Node*>& all)
{
  all.push_back(node);

  if (!node->IsLeafNode())
  {
    collectAllDockNodes(node->ChildNodes[0], all);
    collectAllDockNodes(node->ChildNodes[1], all);
  }
}

// The fraction of the dock space each side node occupies along its split axis. The fractions, not the node
// sizes, are what a resize preserves: a size the engine clamped for a small window must not become the
// remembered size. A closed panel keeps its fraction while its node exists, so it reopens at the right size.
class DockProportions
{
public:
  // Replaces everything remembered with these nodes.
  void remember(const std::vector<DockSideSize>& nodes, const float extentX, const float extentY)
  {
    rememberChanges(nodes, {}, {}, extentX, extentY);
  }

  // Keeps the fraction of every node that is remembered and did not move since `before` (by more than
  // dockEditTolerance), takes the rest from `nodes`, keeps closed nodes whose id is in `existing`, and forgets
  // the others. A click that changes nothing therefore keeps the proportions, however far the engine has
  // clamped the sizes meanwhile.
  void rememberChanges(const std::vector<DockSideSize>& nodes, const std::vector<DockSideSize>& before,
                       const std::vector<std::uint32_t>& existing, const float extentX, const float extentY)
  {
    if (extentX <= 0.0f || extentY <= 0.0f)
    {
      return;
    }

    std::map<std::uint32_t, Entry> entries;

    for (const auto& node : nodes)
    {
      addEntry(entries, node, before, node.axis == 0 ? extentX : extentY);
    }

    for (const auto& [id, entry] : m_entries)
    {
      if (std::ranges::find(existing, id) != existing.end())
      {
        entries.try_emplace(id, entry);
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

  // Whether every visible node with a size is remembered, so a docking change that adds one is noticed. A node
  // without a size cannot be remembered and does not count.
  [[nodiscard]] bool tracks(const std::vector<DockSideSize>& nodes) const
  {
    return std::ranges::all_of(nodes, [this](const DockSideSize& node)
    {
      const auto entry = m_entries.find(node.id);
      return node.size <= 0.0f || (entry != m_entries.end() && entry->second.axis == node.axis);
    });
  }

private:
  struct Entry
  {
    int axis = 0;
    float fraction = 0.0f;
  };

  std::map<std::uint32_t, Entry> m_entries;

  void addEntry(std::map<std::uint32_t, Entry>& entries, const DockSideSize& node,
                const std::vector<DockSideSize>& before, const float extent) const
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
      entries[node.id] = Entry{node.axis, node.size / extent};
    }
  }
};

enum class DockLayoutStep
{
  none,
  remember,
  rememberAndRescale,
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
      return trackDrag();
    }

    const bool sizeChanged = width != m_width || height != m_height;
    const bool ended = m_dragging;

    m_width = width;
    m_height = height;
    m_dragging = false;

    return ended ? DockLayoutStep::endDrag : afterDrag(sizeChanged, layoutChanged);
  }

private:
  float m_width = 0.0f;
  float m_height = 0.0f;
  bool m_hasSize = false;
  bool m_dragging = false;

  [[nodiscard]] DockLayoutStep trackDrag() noexcept
  {
    const bool began = !m_dragging;
    m_dragging = true;

    return began ? DockLayoutStep::beginDrag : DockLayoutStep::none;
  }

  [[nodiscard]] DockLayoutStep afterDrag(const bool sizeChanged, const bool layoutChanged) noexcept
  {
    if (!m_hasSize)
    {
      m_hasSize = true;
      return DockLayoutStep::remember;
    }

    if (layoutChanged)
    {
      return sizeChanged ? DockLayoutStep::rememberAndRescale : DockLayoutStep::remember;
    }

    return sizeChanged ? DockLayoutStep::rescale : DockLayoutStep::none;
  }
};

#endif
