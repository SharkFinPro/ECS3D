#include "ObjectManager.h"
#include "Object.h"
#include "CollisionManager.h"
#include "ObjectGUIManager.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

ObjectManager::ObjectManager()
  : m_collisionManager(std::make_shared<CollisionManager>()),
    m_objectGUIManager(std::make_shared<ObjectGUIManager>(this))
{}

void ObjectManager::update(const float dt)
{
  displaySceneStatusGui();

  m_objectGUIManager->update();

  deleteObjectsMarkedForDeletion();

  fixedUpdate(dt);
  variableUpdate(dt);

  m_objectGUIManager->displaySelectedObjectGui();
}

void ObjectManager::setECS(ECS3D* ecs)
{
  m_ecs = ecs;
}

ECS3D* ObjectManager::getECS() const
{
  return m_ecs;
}

std::shared_ptr<CollisionManager> ObjectManager::getCollisionManager() const
{
  return m_collisionManager;
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  m_collisionManager->addObject(object);

  m_objects.push_back(object);

  m_objectGUIManager->addObject(object);
}

void ObjectManager::duplicateObject(const std::shared_ptr<Object>& object)
{
  auto objectData = object->serialize();
  objectData.at("name") = std::string(objectData.at("name")) + " - Copy";

  addObject(std::make_shared<Object>(objectData, this));
}

void ObjectManager::startScene()
{
  if (m_sceneStatus == SceneStatus::stopped)
  {
    for (const auto& object : m_objects)
    {
      object->start();
    }
  }

  m_sceneStatus = SceneStatus::running;
}

void ObjectManager::pauseScene()
{
  m_sceneStatus = SceneStatus::paused;
}

void ObjectManager::resetScene()
{
  if (m_sceneStatus == SceneStatus::running || m_sceneStatus == SceneStatus::paused)
  {
    for (const auto& object : m_objects)
    {
      object->stop();
    }
  }

  m_sceneStatus = SceneStatus::stopped;
}

nlohmann::json ObjectManager::serialize() const
{
  nlohmann::json data = {
    { "objects", nlohmann::json::array() }
  };

  for (const auto& object : m_objects)
  {
    data["objects"].push_back(object->serialize());
  }

  return data;
}

void ObjectManager::removeObject(const std::shared_ptr<Object>& object)
{
  m_objectsToRemove.push_back(object);
}

void ObjectManager::variableUpdate(const float dt) const
{
  for (const auto& object : m_objects)
  {
    object->variableUpdate(dt);
  }

#ifdef COLLISION_DEBUG
  m_collisionManager->variableUpdate();
#endif
}

void ObjectManager::fixedUpdate(const float dt)
{
  if (m_sceneStatus != SceneStatus::running)
  {
    return;
  }

  m_timeAccumulator += dt;

  uint8_t steps = 1;
  while (m_timeAccumulator >= m_fixedUpdateDt && steps <= 3)
  {
    ++steps;

    for (const auto& object : m_objects)
    {
      object->fixedUpdate(m_fixedUpdateDt);
    }

    m_collisionManager->update();

    m_timeAccumulator -= m_fixedUpdateDt;
  }
}

void ObjectManager::displaySceneStatusGui()
{
  ImGui::Begin("Scene Status");

  constexpr int sceneStatusButtonWidth = 125;

  if (m_sceneStatus != SceneStatus::running)
  {
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      startScene();
    }
  }

  if (m_sceneStatus == SceneStatus::running)
  {
    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      pauseScene();
    }
  }

  if (m_sceneStatus != SceneStatus::stopped)
  {
    ImGui::SameLine();

    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      resetScene();
    }
  }

  ImGui::End();
}

void ObjectManager::deleteObjectsMarkedForDeletion()
{
  if (m_objectsToRemove.empty())
  {
    return;
  }

  for (const auto& object : m_objectsToRemove)
  {
    std::erase(m_objects, object);
  }

  m_objectsToRemove.clear();
}
