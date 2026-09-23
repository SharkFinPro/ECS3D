#include "ComponentOpsBindings.h"
#include "BindingContext.h"
#include <ComponentRegistry.h>
#include <objects/Object.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <memory>
#include <string>
#include <utility>

namespace {
  // Returned strings point into this buffer, valid until the next ComponentOpsBindings call on the same
  // thread - the same convention as WorldBindings/ModelRendererBindings.
  thread_local std::string s_returnBuffer;

  const char* store(std::string value)
  {
    s_returnBuffer = std::move(value);
    return s_returnBuffer.c_str();
  }

  std::shared_ptr<Object> findObject(const char* uuid)
  {
    const auto objectManager = BindingContext::getObjectManager();
    if (!objectManager || !uuid)
    {
      return nullptr;
    }

    const auto parsed = uuids::uuid::from_string(std::string(uuid));
    if (!parsed.has_value())
    {
      return nullptr;
    }

    return objectManager->getObjectByUUID(parsed.value());
  }

  // Finds the component (if any) on object's own component map by registry key, matching against the
  // packed type (so a collider resolves by its shape, "Box"/"Sphere", not the shared "Collider" slot).
  // Never the RigidBody-inherits-from-parent fallback Object::getComponent applies for read access -
  // add/remove/has all reason about what this object itself owns.
  std::shared_ptr<Component> findOwnComponent(const Object& object, const std::string& registryKey)
  {
    for (const auto& [type, component] : object.getComponents())
    {
      const auto keyIt = componentTypeToRegistryKey.find(component->getPackedType());
      if (keyIt != componentTypeToRegistryKey.end() && keyIt->second == registryKey)
      {
        return component;
      }
    }

    return nullptr;
  }
}

ComponentOpsBindings ComponentOpsBindingsProvider::getBindings()
{
  return ComponentOpsBindings {
    .hasComponent = &bindHasComponent,
    .addComponent = &bindAddComponent,
    .removeComponent = &bindRemoveComponent,
    .getComponentTypes = &bindGetComponentTypes
  };
}

bool ComponentOpsBindingsProvider::bindHasComponent(const char* uuid, const char* componentType)
{
  const auto object = findObject(uuid);
  if (!object || !componentType)
  {
    return false;
  }

  return findOwnComponent(*object, componentType) != nullptr;
}

bool ComponentOpsBindingsProvider::bindAddComponent(const char* uuid, const char* componentType)
{
  const auto object = findObject(uuid);
  const auto objectManager = BindingContext::getObjectManager();
  if (!object || !objectManager || !componentType)
  {
    return false;
  }

  // The object is on its way out (removeObject was already called this tick) - deleteObjectsMarkedForDeletion
  // will drop it after this tick's structural broadcast, so a change made to it now would never reach a
  // client and is wasted work at best.
  if (objectManager->isMarkedForDeletion(object))
  {
    return false;
  }

  const std::string key(componentType);

  // Script needs a class name this API does not take (see the header), and Transform is structural -
  // every other system assumes an object has exactly one, so a second one is refused rather than added.
  if (key == "Script" || key == "Transform")
  {
    return false;
  }

  const auto component = objectManager->getComponentRegistry()->create(key);
  if (!component)
  {
    // Unknown name.
    return false;
  }

  // The unique-per-type slot Object::addComponent enforces is keyed by getType() (the parent type - both
  // collider shapes share the same slot), so that is what decides "already present", not getPackedType().
  if (object->getComponents().contains(component->getType()))
  {
    return false;
  }

  object->addComponent(component);

  // Structural: not covered by the per-tick state delta or a targeted component-edit broadcast, so flag
  // the object for the app to resync after the tick, the same as the editor's sceneEdit addComponent op.
  BindingContext::recordStructuralComponentChange(object->getUUID());

  return true;
}

bool ComponentOpsBindingsProvider::bindRemoveComponent(const char* uuid, const char* componentType)
{
  const auto object = findObject(uuid);
  const auto objectManager = BindingContext::getObjectManager();
  if (!object || !objectManager || !componentType)
  {
    return false;
  }

  if (objectManager->isMarkedForDeletion(object))
  {
    return false;
  }

  const std::string key(componentType);

  // Neither is ever found by findOwnComponent below (Script isn't in the component map; Transform would
  // match) - refused explicitly so the reason is clear rather than falling out of a lookup that happens
  // to miss.
  if (key == "Script" || key == "Transform")
  {
    return false;
  }

  const auto component = findOwnComponent(*object, key);
  if (!component)
  {
    return false;
  }

  object->removeComponent(component);

  BindingContext::recordStructuralComponentChange(object->getUUID());

  return true;
}

const char* ComponentOpsBindingsProvider::bindGetComponentTypes(const char* uuid)
{
  const auto object = findObject(uuid);
  if (!object)
  {
    return store("");
  }

  // Registry keys never contain commas, so a comma-delimited list marshals back cleanly (see World.cs).
  std::string result;
  for (const auto& [type, component] : object->getComponents())
  {
    const auto keyIt = componentTypeToRegistryKey.find(component->getPackedType());
    if (keyIt == componentTypeToRegistryKey.end())
    {
      continue;
    }

    if (!result.empty())
    {
      result += ',';
    }
    result += keyIt->second;
  }

  return store(result);
}
