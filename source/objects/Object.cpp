#include "Object.h"
#include "components/Component.h"
#include <utility>

Object::Object(std::string name)
  : manager(nullptr), name(std::move(name))
{}

Object::Object(const std::vector<std::shared_ptr<Component>>& components, std::string name)
  : manager(nullptr), name(std::move(name))
{
  for (const auto& component : components)
  {
    addComponent(component);
  }
}

void Object::addComponent(std::shared_ptr<Component> component)
{
  component->setOwner(this);
  components.insert({ component->getType(), std::move(component) });
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
  for (const auto& [componentType, component] : components)
  {
    component->variableUpdate(dt);
  }
}

void Object::fixedUpdate(const float dt)
{
  for (const auto& [componentType, component] : components)
  {
    component->fixedUpdate(dt);
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

std::string Object::getName() const
{
  return name;
}

void Object::setName(const std::string& name)
{
  this->name = name;
}
