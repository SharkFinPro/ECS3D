#include "Object.h"
#include "../ECS3D.h"
#include "CollisionManager.h"
#include "components/Component.h"
#include "components/LightRenderer.h"
#include "components/ModelRenderer.h"
#include "components/Player.h"
#include "components/RigidBody.h"
#include "components/Transform.h"
#include "components/collisions/BoxCollider.h"
#include "components/collisions/SphereCollider.h"
#include <imgui.h>
#include <utility>

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

void Object::addComponent(const std::shared_ptr<Component>& component, const bool setOwner)
{
  if (setOwner)
  {
    component->setOwner(this);
  }

  components.emplace(component->getType(), component);
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

  for (auto it = components.begin(); it != components.end();)
  {
    auto component = it->second;

    ImGui::PushID(&component);
    component->displayGui();
    ImGui::PopID();

    if (component->markedAsDeleted())
    {
      if (component->getType() == ComponentType::collider)
      {
        manager->getCollisionManager()->addObject(shared_from_this());
      }

      component.reset();

      it = components.erase(it);
    }
    else
    {
      ++it;
    }
  }

  if (ImGui::Button("Add Component"))
  {
    showComponentSelector = true;
  }

  if (showComponentSelector)
  {
    if (ImGui::BeginCombo("##combo", "Select Component"))
    {
      for (const auto& [type, name] : componentTypeToString)
      {
        const auto parentType = subComponentTypeToParent.find(type);
        if (!getComponent(parentType != subComponentTypeToParent.end() ? parentType->second : type))
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
              case ComponentType::SubComponentType_boxCollider:
                addComponent(std::make_shared<BoxCollider>());
                manager->getCollisionManager()->addObject(shared_from_this());
                break;
              case ComponentType::SubComponentType_sphereCollider:
                addComponent(std::make_shared<SphereCollider>());
                manager->getCollisionManager()->addObject(shared_from_this());
                break;
              case ComponentType::player:
                addComponent(std::make_shared<Player>());
                break;
              case ComponentType::lightRenderer:
                addComponent(std::make_shared<LightRenderer>(glm::vec3(0), glm::vec3(0),
                                                             0.0f, 0.0f, 0.0f));
                break;
              default: ;
            }

            showComponentSelector = false;
          }
        }
      }

      ImGui::EndCombo();
    }
  }
}

void Object::start() const
{
  for (const auto& [type, component] : components)
  {
    component->start();
  }
}

void Object::stop() const
{
  for (const auto& [type, component] : components)
  {
    component->stop();
  }
}

std::shared_ptr<Component> Object::getComponent(const ComponentType type) const
{
  const auto component = components.find(type);

  if (component == components.end())
  {
    if (parent != nullptr)
    {
      if (type == ComponentType::rigidBody)
      {
        return parent->getComponent(type);
      }
    }

    return nullptr;
  }

  return component->second;
}
