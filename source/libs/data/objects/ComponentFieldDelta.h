#ifndef COMPONENTFIELDDELTA_H
#define COMPONENTFIELDDELTA_H

#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <vector>

class Object;
class Component;

// Headless (no ImGui) logic backing the Inspector's multi-selection editing: which components are common
// to every selected object, which top-level serialize() fields disagree across them, and how to apply a
// single field's change to one component's own json without touching its other fields. Kept in ECS3DData
// so it can be exercised by the headless test suite, which cannot link the editor's ImGui/renderer code.
namespace componentFieldDelta {
  // Identifies a component "kind" independent of any one object: its type (a collider's subtype where it
  // has one, since Box and Sphere are edited by different widgets and must not be treated as the same
  // kind), plus a script's class name (several script classes can coexist under ComponentType::script, and
  // only same-class instances are the same kind across objects).
  [[nodiscard]] std::string componentSignature(const std::shared_ptr<Component>& component);

  // The component signatures present on every object in `objects`, in the order they first appear on
  // objects[0] (stable so the multi-select view lists them the same way every frame). Empty for an empty
  // `objects`.
  [[nodiscard]] std::vector<std::string> commonComponentSignatures(
    const std::vector<std::shared_ptr<Object>>& objects);

  // The union of every component signature present on any object in `objects` - commonComponentSignatures
  // is a subset of this; the difference is what the "N components not shared..." message counts.
  [[nodiscard]] std::vector<std::string> allComponentSignatures(
    const std::vector<std::shared_ptr<Object>>& objects);

  // The component on `object` matching `signature` (from getComponents() or getScripts()), or nullptr if
  // the object has none matching.
  [[nodiscard]] std::shared_ptr<Component> findComponentBySignature(const std::shared_ptr<Object>& object,
                                                                     const std::string& signature);

  // The top-level keys where `before` and `after` disagree (present in both, compared by value equality).
  // Exact equality is correct here - this detects whether the *stored* values are identical, not whether
  // two computed results are close, so there is no tolerance to apply. A key missing from either side is
  // ignored (serialize() always emits the same key set for a given component kind).
  [[nodiscard]] std::vector<std::string> changedTopLevelKeys(const nlohmann::json& before,
                                                              const nlohmann::json& after);

  // The top-level keys where any of `serializedForms` disagrees with the first entry - the fields the
  // multi-select view shows as mixed instead of silently picking one object's value. Empty or single-
  // element input is never mixed.
  [[nodiscard]] std::vector<std::string> mixedTopLevelKeys(
    const std::vector<nlohmann::json>& serializedForms);

  // `target` with each of `keys` replaced by `source`'s value for that key - a whole-value replacement, so
  // a nested vec3 (stored as an array) is swapped in whole rather than merged element-by-element. Keys
  // outside `keys` (a target object's own differing fields) are left untouched, which is the point: this
  // is how one field's edit is applied to every other selected object without overwriting the rest of
  // their state.
  [[nodiscard]] nlohmann::json applyKeyDelta(const nlohmann::json& target, const nlohmann::json& source,
                                             const std::vector<std::string>& keys);
}

#endif //COMPONENTFIELDDELTA_H
