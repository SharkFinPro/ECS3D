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
#include <nlohmann/json.hpp>
#include <utility>

constexpr int MAX_CHARACTERS = 30;

Object::Object(std::string name)
  : m_name(std::move(name))
{
  m_name.resize(MAX_CHARACTERS);
}

Object::Object(const std::vector<std::shared_ptr<Component>>& components, std::string name)
  : m_name(std::move(name))
{
  for (const auto& component : components)
  {
    addComponent(component);
  }
}

void Object::setParent(const std::shared_ptr<Object>& parent)
{
  m_parent = parent;
}

std::shared_ptr<Object> Object::getParent() const
{
  return m_parent;
}

void Object::addComponent(const std::shared_ptr<Component>& component, const bool setOwner)
{
  if (setOwner)
  {
    component->setOwner(this);
  }

  m_components.emplace(component->getType(), component);
}

void Object::variableUpdate(const float dt)
{
  for (const auto& [componentType, component] : m_components)
  {
    if (component->getOwner() == this)
    {
      component->variableUpdate(dt);
    }
  }
}

void Object::fixedUpdate(const float dt)
{
  for (const auto& [componentType, component] : m_components)
  {
    if (component->getOwner() == this)
    {
      component->fixedUpdate(dt);
    }
  }
}

void Object::setManager(ObjectManager* objectManager)
{
  m_manager = objectManager;
}

ObjectManager* Object::getManager() const
{
  return m_manager;
}

std::string Object::getName() const
{
  return m_name;
}

void Object::setName(const std::string& name)
{
  m_name = name;
}

void Object::displayGui()
{
  ImGui::InputText("Name", m_name.data(), m_name.capacity());

  for (auto it = m_components.begin(); it != m_components.end();)
  {
    auto component = it->second;

    ImGui::PushID(&component);
    component->displayGui();
    ImGui::PopID();

    if (component->markedAsDeleted())
    {
      if (component->getType() == ComponentType::collider)
      {
        m_manager->getCollisionManager()->addObject(shared_from_this());
      }

      component.reset();

      it = m_components.erase(it);
    }
    else
    {
      ++it;
    }
  }

  if (ImGui::Button("Add Component"))
  {
    m_showComponentSelector = true;
  }

  if (m_showComponentSelector)
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
                m_manager->getCollisionManager()->addObject(shared_from_this());
                break;
              case ComponentType::SubComponentType_sphereCollider:
                addComponent(std::make_shared<SphereCollider>());
                m_manager->getCollisionManager()->addObject(shared_from_this());
                break;
              case ComponentType::player:
                addComponent(std::make_shared<Player>());
                break;
              case ComponentType::lightRenderer:
                addComponent(std::make_shared<LightRenderer>(getManager()->getECS()->getRenderer(),
                                                             glm::vec3(0), glm::vec3(0),
                                                             0.0f, 0.0f, 0.0f));
                break;
              default: ;
            }

            m_showComponentSelector = false;
          }
        }
      }

      ImGui::EndCombo();
    }
  }
}

void Object::start() const
{
  for (const auto& [type, component] : m_components)
  {
    component->start();
  }
}

void Object::stop() const
{
  for (const auto& [type, component] : m_components)
  {
    component->stop();
  }
}

nlohmann::json Object::serialize()
{
  nlohmann::json data = {
    { "name", m_name },
    { "components", nlohmann::json::array() }
  };

  for (const auto& [_, component] : m_components)
  {
    data["components"].push_back(component->serialize());
  }

  return data;
}

std::shared_ptr<Component> Object::getComponent(const ComponentType type) const
{
  const auto component = m_components.find(type);

  if (component == m_components.end())
  {
    if (m_parent != nullptr)
    {
      if (type == ComponentType::rigidBody)
      {
        return m_parent->getComponent(type);
      }
    }

    return nullptr;
  }

  return component->second;
}
