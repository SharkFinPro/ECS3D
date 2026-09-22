#include "ComponentFieldDelta.h"
#include "Object.h"
#include "components/Component.h"
#include "components/Script.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace componentFieldDelta {
  std::string componentSignature(const std::shared_ptr<Component>& component)
  {
    auto signature = componentTypeToString.at(
      component->getSubType() != ComponentType::SubComponentType_none ? component->getSubType() : component->getType());

    if (component->getType() == ComponentType::script)
    {
      if (const auto script = std::dynamic_pointer_cast<Script>(component))
      {
        signature += ":" + script->getClassName();
      }
    }

    return signature;
  }

  namespace {
    // Every signature on `object`, in the order getComponents()/getScripts() offers them.
    std::vector<std::string> signaturesOf(const std::shared_ptr<Object>& object)
    {
      std::vector<std::string> signatures;

      for (const auto& [type, component] : object->getComponents())
      {
        signatures.push_back(componentSignature(component));
      }
      for (const auto& script : object->getScripts())
      {
        signatures.push_back(componentSignature(script));
      }

      return signatures;
    }

    // Whether `value` looks like Script::m_fields: an array of json objects each carrying a "name" - a
    // shape check rather than naming the Script component type, so this stays generic to whatever else
    // might serialize a named-entry list the same way. An empty array has no entries to disagree about, so
    // it is treated as an ordinary value instead (falls through to the whole-value path).
    bool isNamedEntryArray(const nlohmann::json& value)
    {
      if (!value.is_array() || value.empty())
      {
        return false;
      }

      return std::ranges::all_of(value, [](const nlohmann::json& entry) {
        return entry.is_object() && entry.contains("name");
      });
    }

    const nlohmann::json* findEntryByName(const nlohmann::json& entries, const std::string& name)
    {
      for (const auto& entry : entries)
      {
        if (entry.contains("name") && entry.at("name") == name)
        {
          return &entry;
        }
      }

      return nullptr;
    }

    // Entry names in `after` whose "value" differs from the same-named entry in `before` (including one
    // `before` has no matching entry for at all).
    std::vector<std::string> changedEntryNames(const nlohmann::json& before, const nlohmann::json& after)
    {
      std::vector<std::string> changed;

      for (const auto& entry : after)
      {
        if (!entry.contains("name"))
        {
          continue;
        }

        const std::string name = entry.at("name");
        const auto* beforeEntry = findEntryByName(before, name);
        const auto afterValue = entry.contains("value") ? entry.at("value") : nlohmann::json();
        const auto beforeValue = beforeEntry && beforeEntry->contains("value")
          ? beforeEntry->at("value") : nlohmann::json();

        if (!beforeEntry || beforeValue != afterValue)
        {
          changed.push_back(name);
        }
      }

      return changed;
    }
  }

  std::vector<std::string> commonComponentSignatures(const std::vector<std::shared_ptr<Object>>& objects)
  {
    if (objects.empty())
    {
      return {};
    }

    const auto first = signaturesOf(objects[0]);

    std::vector<std::string> common;
    for (const auto& signature : first)
    {
      // Skip a duplicate already accepted (an object with two same-class scripts would otherwise offer
      // the signature twice).
      if (std::ranges::find(common, signature) != common.end())
      {
        continue;
      }

      const bool onEveryObject = std::ranges::all_of(objects, [&](const std::shared_ptr<Object>& object) {
        if (object == objects[0])
        {
          return true;
        }
        const auto theirs = signaturesOf(object);
        return std::ranges::find(theirs, signature) != theirs.end();
      });

      if (onEveryObject)
      {
        common.push_back(signature);
      }
    }

    return common;
  }

  std::vector<std::string> allComponentSignatures(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<std::string> all;

    for (const auto& object : objects)
    {
      for (const auto& signature : signaturesOf(object))
      {
        if (std::ranges::find(all, signature) == all.end())
        {
          all.push_back(signature);
        }
      }
    }

    return all;
  }

  std::shared_ptr<Component> findComponentBySignature(const std::shared_ptr<Object>& object,
                                                       const std::string& signature)
  {
    for (const auto& [type, component] : object->getComponents())
    {
      if (componentSignature(component) == signature)
      {
        return component;
      }
    }
    for (const auto& script : object->getScripts())
    {
      if (componentSignature(script) == signature)
      {
        return script;
      }
    }

    return nullptr;
  }

  std::vector<std::string> changedTopLevelKeys(const nlohmann::json& before, const nlohmann::json& after)
  {
    std::vector<std::string> changed;

    if (!before.is_object() || !after.is_object())
    {
      return changed;
    }

    for (const auto& [key, value] : before.items())
    {
      const auto it = after.find(key);
      if (it != after.end() && *it != value)
      {
        changed.push_back(key);
      }
    }

    return changed;
  }

  std::vector<std::string> mixedTopLevelKeys(const std::vector<nlohmann::json>& serializedForms)
  {
    std::vector<std::string> mixed;

    if (serializedForms.size() < 2 || !serializedForms[0].is_object())
    {
      return mixed;
    }

    for (const auto& [key, value] : serializedForms[0].items())
    {
      if (isNamedEntryArray(value))
      {
        for (const auto& entry : value)
        {
          if (!entry.contains("name"))
          {
            continue;
          }

          const std::string name = entry.at("name");
          const auto entryValue = entry.contains("value") ? entry.at("value") : nlohmann::json();

          const bool disagrees = std::any_of(serializedForms.begin() + 1, serializedForms.end(),
            [&](const nlohmann::json& form) {
              const auto it = form.find(key);
              if (it == form.end())
              {
                return true;
              }
              const auto* other = findEntryByName(*it, name);
              const auto otherValue = other && other->contains("value") ? other->at("value") : nlohmann::json();
              return !other || otherValue != entryValue;
            });

          if (disagrees)
          {
            mixed.push_back(key + "." + name);
          }
        }

        continue;
      }

      const bool disagrees = std::any_of(serializedForms.begin() + 1, serializedForms.end(),
        [&](const nlohmann::json& form) {
          const auto it = form.find(key);
          return it == form.end() || *it != value;
        });

      if (disagrees)
      {
        mixed.push_back(key);
      }
    }

    return mixed;
  }

  nlohmann::json applyKeyDelta(const nlohmann::json& target, const nlohmann::json& before,
                               const nlohmann::json& after, const std::vector<std::string>& keys)
  {
    nlohmann::json merged = target;

    for (const auto& key : keys)
    {
      const auto afterIt = after.find(key);
      if (afterIt == after.end())
      {
        continue;
      }

      // A Script-shaped fields array: merge per entry (by name) instead of replacing the whole array, or
      // every other object's untouched fields would be clobbered by the primary's whole field list.
      if (isNamedEntryArray(*afterIt) && merged.contains(key) && merged.at(key).is_array())
      {
        const auto beforeIt = before.find(key);
        const auto changed = changedEntryNames(beforeIt != before.end() ? *beforeIt : nlohmann::json::array(),
                                               *afterIt);

        for (auto& targetEntry : merged.at(key))
        {
          if (!targetEntry.contains("name"))
          {
            continue;
          }

          const std::string name = targetEntry.at("name");
          if (std::ranges::find(changed, name) == changed.end())
          {
            continue;
          }

          // Only the value crosses over; an entry target doesn't have is left alone rather than added.
          if (const auto* sourceEntry = findEntryByName(*afterIt, name);
              sourceEntry && sourceEntry->contains("value"))
          {
            targetEntry["value"] = sourceEntry->at("value");
          }
        }

        continue;
      }

      merged[key] = *afterIt;
    }

    return merged;
  }
}
