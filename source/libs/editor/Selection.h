#ifndef SELECTION_H
#define SELECTION_H

#include <algorithm>
#include <optional>
#include <span>
#include <vector>
#include <uuid.h>

// The editor's selection, shared across the panels that read or write it (object tree, viewport
// picking, the asset browser, and the Inspector). A selection is one kind at a time: a set of scene
// objects, a set of registry assets, or nothing - selecting/adding under a different kind replaces
// whatever was held under the other kind. Items are kept in the order they were added; the primary is
// the most recently added/selected item, and is what the single-item helpers (objectUUID/assetUUID)
// return, so existing single-selection callers see identical behavior.
class EditorSelection {
public:
  enum class Kind { None, Object, Asset };

  [[nodiscard]] Kind kind() const { return m_kind; }

  void selectObject(const uuids::uuid& uuid) { replace(Kind::Object, uuid); }

  void selectAsset(const uuids::uuid& uuid) { replace(Kind::Asset, uuid); }

  void clear() { m_kind = Kind::None; m_items.clear(); }

  // Adds uuid to the selection, becoming the new primary. An already-present uuid moves to the back
  // (primary) rather than duplicating. Adding under a different kind than the one currently held
  // replaces the selection instead, keeping the one-kind invariant.
  void addObject(const uuids::uuid& uuid) { add(Kind::Object, uuid); }

  void addAsset(const uuids::uuid& uuid) { add(Kind::Asset, uuid); }

  // Removes uuid from the selection, if present. Removing the primary falls back to the previous item;
  // removing the last remaining item clears the kind too.
  void remove(const uuids::uuid& uuid)
  {
    const auto it = std::ranges::find(m_items, uuid);
    if (it == m_items.end())
    {
      return;
    }

    m_items.erase(it);
    if (m_items.empty())
    {
      m_kind = Kind::None;
    }
  }

  void toggleObject(const uuids::uuid& uuid) { toggle(Kind::Object, uuid); }

  void toggleAsset(const uuids::uuid& uuid) { toggle(Kind::Asset, uuid); }

  [[nodiscard]] bool contains(const uuids::uuid& uuid) const
  {
    return std::ranges::find(m_items, uuid) != m_items.end();
  }

  [[nodiscard]] size_t size() const { return m_items.size(); }

  [[nodiscard]] bool empty() const { return m_items.empty(); }

  [[nodiscard]] std::span<const uuids::uuid> items() const { return m_items; }

  // The primary uuid when the selection is of that kind, else nullopt - so callers can branch on the
  // optional without also checking kind().
  [[nodiscard]] std::optional<uuids::uuid> objectUUID() const
  {
    return m_kind == Kind::Object && !m_items.empty() ? std::optional(m_items.back()) : std::nullopt;
  }

  [[nodiscard]] std::optional<uuids::uuid> assetUUID() const
  {
    return m_kind == Kind::Asset && !m_items.empty() ? std::optional(m_items.back()) : std::nullopt;
  }

private:
  void replace(const Kind kind, const uuids::uuid& uuid)
  {
    m_kind = kind;
    m_items.assign(1, uuid);
  }

  void add(const Kind kind, const uuids::uuid& uuid)
  {
    if (m_kind != kind)
    {
      replace(kind, uuid);
      return;
    }

    const auto it = std::ranges::find(m_items, uuid);
    if (it != m_items.end())
    {
      m_items.erase(it);
    }
    m_items.push_back(uuid);
  }

  void toggle(const Kind kind, const uuids::uuid& uuid)
  {
    if (m_kind == kind && contains(uuid))
    {
      remove(uuid);
    }
    else
    {
      add(kind, uuid);
    }
  }

  Kind m_kind = Kind::None;

  // Ordered by add time; back() is the primary (most recently selected/added) item.
  std::vector<uuids::uuid> m_items;
};

#endif //SELECTION_H
