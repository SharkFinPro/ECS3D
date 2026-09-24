#ifndef MIXEDFIELDS_H
#define MIXEDFIELDS_H

#include <string>
#include <unordered_set>

// The serialize() top-level keys (see ComponentFieldDelta.h) that disagree across a multi-selection, so a
// GuiHandler can show a distinct mixed state on just the fields that actually differ instead of silently
// showing the primary object's value. Default-constructed (empty) for the single-selection path, where
// nothing is mixed.
class MixedFields {
public:
  MixedFields() = default;
  explicit MixedFields(std::unordered_set<std::string> keys) : m_keys(std::move(keys)) {}

  [[nodiscard]] bool contains(const std::string& key) const { return m_keys.contains(key); }

private:
  std::unordered_set<std::string> m_keys;
};

#endif //MIXEDFIELDS_H
