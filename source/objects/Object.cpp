#include "Object.h"
#include "components/Component.h"
#include <utility>
#include <imgui.h>

Object::Object(std::string name)
  : manager(nullptr), name(std::move(name))
{
  name.resize(MAX_CHARACTERS);
}

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
  const auto component = components.find(type);

  return component != components.end() ? component->second : nullptr;
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

void Object::displayGui()
{
  ImGui::InputText("Name", name.data(), name.capacity());

  if (ImGui::Button("Reset Components"))
  {
    reset();
  }

  for (const auto& [type, component] : components)
  {
    ImGui::PushID(&component);
    component->displayGui();
    ImGui::PopID();
  }
}

void Object::reset()
{
  for (const auto& [type, component] : components)
  {
    component->reset();
  }
}
