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

  nlohmann::json applyKeyDelta(const nlohmann::json& target, const nlohmann::json& source,
                               const std::vector<std::string>& keys)
  {
    nlohmann::json merged = target;

    for (const auto& key : keys)
    {
      if (const auto it = source.find(key); it != source.end())
      {
        merged[key] = *it;
      }
    }

    return merged;
  }
}
