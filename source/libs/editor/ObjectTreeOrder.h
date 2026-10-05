#ifndef OBJECTTREEORDER_H
#define OBJECTTREEORDER_H

#include "Selection.h"
#include <objects/Object.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <uuid.h>

namespace objectTreeOrder {
  // How the tree orders siblings for display. This never touches the scene: authored is the order
  // ObjectManager/Object already hand out (today, load order - there is no persisted sibling order yet),
  // and alphabetical is a display-only sorted copy built fresh each frame.
  enum class SortMode { authored, alphabetical };

  [[nodiscard]] inline SortMode parseSortMode(const std::string& value)
  {
    return value == "alphabetical" ? SortMode::alphabetical : SortMode::authored;
  }

  [[nodiscard]] inline const char* sortModeToString(const SortMode mode)
  {
    return mode == SortMode::alphabetical ? "alphabetical" : "authored";
  }

  // ASCII-only case fold, same approach AssetBrowserPanel uses for its own name sort/search - no
  // std::locale, so this never depends on the environment.
  [[nodiscard]] inline bool ciNameLess(const std::string& a, const std::string& b)
  {
    return std::ranges::lexicographical_compare(a, b, [](const char x, const char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  }

  // The display order for `objects`, never the scene's own order: authored order is `objects` itself
  // (today that's ObjectManager/Object's own load order, since there is no persisted sibling order yet),
  // untouched and unsorted - so the caller-owned `scratch` is only filled and sorted (a stable sort, so
  // two same-named objects keep their authored relative order) when alphabetical mode actually needs a
  // reordered copy. `scratch` must outlive the reference this returns, so it lives in the caller's own
  // loop scope rather than inside this function.
  [[nodiscard]] inline const std::vector<std::shared_ptr<Object>>& sortedForDisplay(
    const std::vector<std::shared_ptr<Object>>& objects, const SortMode mode,
    std::vector<std::shared_ptr<Object>>& scratch)
  {
    if (mode != SortMode::alphabetical)
    {
      return objects;
    }

    scratch = objects;
    std::ranges::stable_sort(scratch, [](const std::shared_ptr<Object>& a, const std::shared_ptr<Object>& b) {
      return ciNameLess(a->getName(), b->getName());
    });
    return scratch;
  }

  // Shift-click range: the contiguous slice of `visibleOrder` (the tree's current on-screen order) from
  // `anchor` to `clicked`, inclusive and ordered anchor-to-clicked so the caller can add it to the
  // selection with `clicked` landing last (the new primary). Either uuid missing from `visibleOrder` (a
  // stale anchor, or a row hidden behind a collapsed ancestor) falls back to just `clicked`.
  [[nodiscard]] inline std::vector<uuids::uuid> rangeBetween(const std::vector<uuids::uuid>& visibleOrder,
                                                              const uuids::uuid& anchor,
                                                              const uuids::uuid& clicked)
  {
    const auto anchorIt = std::ranges::find(visibleOrder, anchor);
    const auto clickedIt = std::ranges::find(visibleOrder, clicked);
    if (anchorIt == visibleOrder.end() || clickedIt == visibleOrder.end())
    {
      return { clicked };
    }

    std::vector<uuids::uuid> range;
    if (anchorIt <= clickedIt)
    {
      range.assign(anchorIt, clickedIt + 1);
    }
    else
    {
      range.assign(clickedIt, anchorIt + 1);
      std::ranges::reverse(range);
    }
    return range;
  }

  // Which objects a row action (delete, duplicate) acts on: the whole selection when the row is part of
  // a multi-object selection, otherwise just the row.
  [[nodiscard]] inline std::vector<uuids::uuid> actionTargets(const EditorSelection& selection,
                                                               const uuids::uuid& row)
  {
    if (selection.kind() == EditorSelection::Kind::Object && selection.size() > 1 && selection.contains(row))
    {
      return { selection.items().begin(), selection.items().end() };
    }

    return { row };
  }
}

#endif //OBJECTTREEORDER_H
