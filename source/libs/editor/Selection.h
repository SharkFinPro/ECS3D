#ifndef SELECTION_H
#define SELECTION_H

#include <optional>
#include <uuid.h>

// The editor's single selection slot, shared across the panels that read or write it (object tree,
// viewport picking, and — later phases — the asset browser and the Inspector). A selection is one
// kind at a time: a scene object, a registry asset, or nothing. Panels branch on kind() to decide
// what to render/highlight; writers use the select* helpers, so selecting one kind implicitly clears
// the other (there is a single selection slot, not one per kind).
class EditorSelection {
public:
  enum class Kind { None, Object, Asset };

  [[nodiscard]] Kind kind() const { return m_kind; }

  void selectObject(const uuids::uuid& uuid) { m_kind = Kind::Object; m_uuid = uuid; }

  void selectAsset(const uuids::uuid& uuid) { m_kind = Kind::Asset; m_uuid = uuid; }

  void clear() { m_kind = Kind::None; m_uuid.reset(); }

  // The selected uuid when the selection is of that kind, else nullopt — so callers can branch on the
  // optional without also checking kind().
  [[nodiscard]] std::optional<uuids::uuid> objectUUID() const
  {
    return m_kind == Kind::Object ? m_uuid : std::nullopt;
  }

  [[nodiscard]] std::optional<uuids::uuid> assetUUID() const
  {
    return m_kind == Kind::Asset ? m_uuid : std::nullopt;
  }

private:
  Kind m_kind = Kind::None;

  // Set whenever m_kind is Object or Asset; the kind() disambiguates which registry it refers to.
  std::optional<uuids::uuid> m_uuid;
};

#endif //SELECTION_H
