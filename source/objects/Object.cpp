#include "Object.h"
#include "components/Component.h"
#include <utility>
#include <imgui.h>

constexpr int MAX_CHARACTERS = 30;

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

void Object::setParent(const std::shared_ptr<Object>& parent)
{
  this->parent = parent;
}

std::shared_ptr<Object> Object::getParent() const
{
  return parent;
}

void Object::setUINode(const std::shared_ptr<ObjectUINode> &uiNode)
{
  this->uiNode = uiNode;
}

std::shared_ptr<ObjectUINode> Object::getUINode() const
{
  return uiNode;
}

void Object::addComponent(const std::shared_ptr<Component>& component, const bool setOwner)
{
  if (setOwner)
  {
    component->setOwner(this);
  }

  components.emplace(component->getType(), component);
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
    if (component->getOwner() == this)
    {
      component->variableUpdate(dt);
    }
  }
}

void Object::fixedUpdate(const float dt)
{
  for (const auto& [componentType, component] : components)
  {
    if (component->getOwner() == this)
    {
      component->fixedUpdate(dt);
    }
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
