#include "Object.h"

Object::Object(const std::vector<std::shared_ptr<Component>>& components)
{
  for (const auto& component : components)
  {
    addComponent(component);
  }
}

void Object::addComponent(std::shared_ptr<Component> component)
{
  // TODO: Insert component into components
}

std::shared_ptr<Component> Object::getComponent(const ComponentType type) const
{
  if (const auto component = components.find(type); component != components.end())
  {
    return component->second;
  }

  return nullptr;
}

void Object::variableUpdate(const float dt)
{
  for (const auto& component : components)
  {
    // TODO: Execute component variableUpdate
  }
}

void Object::fixedUpdate(const float dt)
{
  for (const auto& component : components)
  {
    // TODO: Execute component fixedUpdate
  }
}

void Object::setManager(ObjectManager* objectManager)
{
  manager = objectManager;
}

ObjectManager* Object::getManager() const
{
  return manager;
}
