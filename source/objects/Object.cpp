#include "Object.h"
#include "CollisionManager.h"
#include "components/Component.h"
#include "components/LightRenderer.h"
#include "components/ModelRenderer.h"
#include "components/RigidBody.h"
#include "components/Script.h"
#include "components/Transform.h"
#include "components/collisions/BoxCollider.h"
#include "components/collisions/SphereCollider.h"
#include "../ECS3D.h"
#include "../assets/ScriptAsset.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <utility>

Object::Object(std::string name)
  : m_name(std::move(name))
{
  addComponent(std::make_shared<Transform>(glm::vec3(0), glm::vec3(1), glm::vec3(0)));
}

Object::Object(const std::vector<std::shared_ptr<Component>>& components, std::string name)
  : m_name(std::move(name))
{
  for (const auto& component : components)
  {
    addComponent(component);
  }
}

Object::Object(const nlohmann::json& objectData,
               ObjectManager* manager)
  : m_manager(manager)
{
  loadFromJSON(objectData);
}

void Object::setParent(const std::shared_ptr<Object>& parent)
{
  m_parent = parent;
}

std::shared_ptr<Object> Object::getParent() const
{
  return m_parent;
}

void Object::addComponent(const std::shared_ptr<Component>& component,
                          const bool setOwner)
{
  if (component->getType() == ComponentType::script)
  {
    const auto newScriptComponent = std::dynamic_pointer_cast<Script>(component);

    for (const auto& script : m_scripts)
    {
      auto scriptComponent = std::dynamic_pointer_cast<Script>(script);
      if (scriptComponent && scriptComponent->getClassName() == newScriptComponent->getClassName())
      {
        return;
      }
    }

    m_scripts.push_back(component);
  }
  else
  {
    m_components.emplace(component->getType(), component);
  }

  if (setOwner)
  {
    component->setOwner(this);
  }
}

void Object::variableUpdate()
{
  for (const auto& script : m_scripts)
  {
    if (script->getOwner() == this)
    {
      script->variableUpdate();
    }
  }

  for (const auto& [componentType, component] : m_components)
  {
    if (component->getOwner() == this)
    {
      component->variableUpdate();
    }
  }
}

void Object::fixedUpdate(const float dt)
{
  for (const auto& script : m_scripts)
  {
    if (script->getOwner() == this)
    {
      script->fixedUpdate(dt);
    }
  }

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

  if (m_uuid.is_nil())
  {
    m_uuid = m_manager->getECS()->createUUID();
  }
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
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Name");
  ImGui::SameLine();
  ImGui::InputText(("##" + uuids::to_string(m_uuid) + "Name").c_str(), m_name.data(), m_name.capacity());

  for (auto it = m_components.begin(); it != m_components.end();)
  {
    auto& component = it->second;

    ImGui::PushID(component.get());
    component->displayGui();
    ImGui::PopID();

    if (component->markedAsDeleted())
    {
      if (component->getType() == ComponentType::collider)
      {
        m_manager->getCollisionManager()->removeObject(shared_from_this());
      }

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

  displayComponentSelector();

  const float scriptDropZoneStartY = ImGui::GetCursorScreenPos().y;

  ImGui::SeparatorText("Scripts");

  if (m_scripts.empty())
  {
    ImGui::Dummy({ ImGui::GetContentRegionAvail().x, 60.0f });
  }
  else
  {
    for (auto it = m_scripts.begin(); it != m_scripts.end();)
    {
      auto& script = *it;

      ImGui::PushID(script.get());
      script->displayGui();
      ImGui::PopID();

      if (script->markedAsDeleted())
      {
        it = m_scripts.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  displayScriptDragDropArea(scriptDropZoneStartY);
}

void Object::start() const
{
  for (const auto& [type, component] : m_components)
  {
    component->start();
  }

  for (const auto& script : m_scripts)
  {
    script->start();
  }
}

void Object::stop() const
{
  for (const auto& [type, component] : m_components)
  {
    component->stop();
  }

  for (const auto& script : m_scripts)
  {
    script->stop();
  }
}

nlohmann::json Object::serialize()
{
  std::string cleanName = m_name;
  cleanName.erase(std::ranges::find(cleanName, '\0'), cleanName.end());

  nlohmann::json data = {
    { "name", cleanName },
    { "components", nlohmann::json::array() },
    { "scripts", nlohmann::json::array() },
    { "uuid", uuids::to_string(m_uuid) }
  };

  for (const auto& [_, component] : m_components)
  {
    data["components"].push_back(component->serialize());
  }

  for (const auto& script : m_scripts)
  {
    data["scripts"].push_back(script->serialize());
  }

  return data;
}

uuids::uuid Object::getUUID() const
{
  return m_uuid;
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

void Object::loadFromJSON(const nlohmann::json& objectData)
{
  m_uuid = uuids::uuid::from_string(std::string(objectData.at("uuid"))).value();
  m_name = objectData.at("name");

  for (const auto& componentData : objectData["components"])
  {
    const auto component = loadComponentFromJSON(componentData);
    addComponent(component);
    component->loadFromJSON(componentData);
  }

  for (const auto& scriptData : objectData["scripts"])
  {
    const auto script = std::make_shared<Script>(scriptData["className"], m_manager->getECS()->getScriptManager());
    addComponent(script);
    script->loadFromJSON(scriptData);
  }
}

std::shared_ptr<Component> Object::loadComponentFromJSON(const nlohmann::json& componentData) const
{
  std::shared_ptr<Component> component = nullptr;

  if (componentData["type"] == "Collider")
  {
    if (componentData["subType"] == "Box")
    {
      component = std::make_shared<BoxCollider>();
    }
    else if (componentData["subType"] == "Sphere")
    {
      component = std::make_shared<SphereCollider>();
    }
  }
  else if (componentData["type"] == "LightRenderer")
  {
    component = std::make_shared<LightRenderer>(m_manager->getECS()->getRenderer());
  }
  else if (componentData["type"] == "ModelRenderer")
  {
    component = std::make_shared<ModelRenderer>(m_manager->getECS()->getRenderer());
  }
  else if (componentData["type"] == "RigidBody")
  {
    component = std::make_shared<RigidBody>();
  }
  else if (componentData["type"] == "Transform")
  {
    component = std::make_shared<Transform>();
  }

  if (!component)
  {
    throw std::runtime_error("Unknown component type: " + std::string(componentData["type"]));
  }

  return component;
}

void Object::displayComponentSelector()
{
  if (!m_showComponentSelector)
  {
    return;
  }

  if (ImGui::BeginCombo("##combo", "Select Component"))
  {
    for (const auto& [type, name] : componentTypeToString)
    {
      if (type == ComponentType::script)
      {
        continue;
      }

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
            case ComponentType::lightRenderer:
              addComponent(std::make_shared<LightRenderer>(getManager()->getECS()->getRenderer(),
                                                           glm::vec3(0), 0.0f, 0.0f, 0.0f));
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

void Object::displayScriptDragDropArea(const float dropZoneStartY)
{
  if (ImGui::GetDragDropPayload() == nullptr)
  {
    return;
  }

  const ImVec2 windowPos     = ImGui::GetWindowPos();
  const float  windowRight   = windowPos.x + ImGui::GetWindowWidth();
  const float  contentBottom = windowPos.y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;

  ImGui::SetCursorScreenPos({ windowPos.x, dropZoneStartY });
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton(
      "##assetDropZone",
      { windowRight - windowPos.x, contentBottom - dropZoneStartY }
  );

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("asset"))
    {
      auto asset = *static_cast<std::shared_ptr<Asset>*>(payload->Data);
      if (const auto scriptAsset = std::dynamic_pointer_cast<ScriptAsset>(asset))
      {
        addComponent(scriptAsset->createScript());
      }
    }

    ImGui::EndDragDropTarget();
  }
}
