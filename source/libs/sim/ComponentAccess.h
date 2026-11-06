#ifndef COMPONENTACCESS_H
#define COMPONENTACCESS_H

#include <objects/Object.h>
#include <objects/components/Component.h>
#include <objects/components/RigidBody.h>

// Raw-pointer component lookups for the physics hot paths. Object::getComponent returns a refcounted pointer
// through a dynamic cast; these read the component map directly and are valid while the object lives. T must be
// the class the ComponentType names, as the callers pass it.
namespace componentAccess {
  template <typename T>
  [[nodiscard]] T* own(const Object& object, const ComponentType type)
  {
    const auto& components = object.getComponents();
    const auto found = components.find(type);

    return found == components.end() ? nullptr : static_cast<T*>(found->second.get());
  }

  // The rigid body on the object or, as Object::getComponent does, on the nearest ancestor that has one.
  [[nodiscard]] inline RigidBody* bodyOf(const Object& object)
  {
    if (auto* body = own<RigidBody>(object, ComponentType::rigidBody))
    {
      return body;
    }

    for (auto parent = object.getParent(); parent; parent = parent->getParent())
    {
      if (auto* body = own<RigidBody>(*parent, ComponentType::rigidBody))
      {
        return body;
      }
    }

    return nullptr;
  }
}

#endif //COMPONENTACCESS_H
