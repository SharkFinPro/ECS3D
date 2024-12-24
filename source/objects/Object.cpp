#include "Object.h"
#include "components/Component.h"
#include <utility>
#include <imgui.h>

#include "../ECS3D.h"
#include "components/LightRenderer.h"
#include "components/ModelRenderer.h"
#include "components/Player.h"
#include "components/RigidBody.h"
#include "components/Transform.h"
#include "components/collisions/BoxCollider.h"
#include "components/collisions/SphereCollider.h"

constexpr int MAX_CHARACTERS = 30;

Object::Object(std::string name)
  : manager(nullptr), name(std::move(name)), showComponentSelector(false)
{
  name.resize(MAX_CHARACTERS);
}

Object::Object(const std::vector<std::shared_ptr<Component>>& components, std::string name)
  : manager(nullptr), name(std::move(name)), showComponentSelector(false)
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
#include <iostream>
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

  if (ImGui::Button("Add Component"))
  {
    showComponentSelector = true;
  }

  if (showComponentSelector)
  {
    if (ImGui::BeginCombo("##combo", "Select Component"))
    {
      for (const auto& [type, name] : allComponentTypes)
      {
        if (!getComponent(type))
        {
          if (ImGui::Selectable(name.data()))
          {
            switch (type)
            {
              case ComponentType::transform:
                addComponent(std::make_shared<Transform>(glm::vec3(0), glm::vec3(1), glm::vec3(0)));
                break;
              case ComponentType::modelRenderer:
                addComponent(std::make_shared<ModelRenderer>(getManager()->getECS()->getRenderer()));
                break;
              case ComponentType::rigidBody:
                addComponent(std::make_shared<RigidBody>());
                break;
              case ComponentType::collider:
                if (name == "Box Collider")
                {
                  addComponent(std::make_shared<BoxCollider>());
                }
                else
                {
                  addComponent(std::make_shared<SphereCollider>());
                }

                manager->addObjectToCollisions(std::shared_ptr<Object>(this));
                break;
              case ComponentType::player:
                addComponent(std::make_shared<Player>());
                break;
              case ComponentType::lightRenderer:
                addComponent(std::make_shared<LightRenderer>(glm::vec3(0), glm::vec3(0), 0, 0, 0));
                break;
              default: ;
            }
          }
        }
      }

      ImGui::EndCombo();
    }
  }
}

void Object::reset()
{
  for (const auto& [type, component] : components)
  {
    component->reset();
  }
}
