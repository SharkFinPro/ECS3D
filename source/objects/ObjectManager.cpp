#include "ObjectManager.h"
#include "Object.h"
#include "CollisionManager.h"
#include "ObjectGUIManager.h"
#include <imgui.h>

ObjectManager::ObjectManager()
  : ecs(nullptr), collisionManager(std::make_shared<CollisionManager>()),
    objectGUIManager(std::make_shared<ObjectGUIManager>(this)), sceneStatus(SceneStatus::stopped),
    fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
{}

void ObjectManager::update(const float dt)
{
  displaySceneStatusGui();

  objectGUIManager->update();

  fixedUpdate(dt);
  variableUpdate(dt);
}

void ObjectManager::setECS(ECS3D* ecs)
{
  this->ecs = ecs;
}

ECS3D* ObjectManager::getECS() const
{
  return ecs;
}

std::shared_ptr<CollisionManager> ObjectManager::getCollisionManager() const
{
  return collisionManager;
}

void ObjectManager::addObject(const std::shared_ptr<Object>& object)
{
  object->setManager(this);

  collisionManager->addObject(object);

  objects.push_back(object);

  objectGUIManager->addObject(object);
}

void ObjectManager::startScene()
{
  if (sceneStatus == SceneStatus::stopped)
  {
    for (const auto& object : objects)
    {
      object->start();
    }
  }

  sceneStatus = SceneStatus::running;
}

void ObjectManager::pauseScene()
{
  sceneStatus = SceneStatus::paused;
}

void ObjectManager::resetScene()
{
  if (sceneStatus == SceneStatus::running || sceneStatus == SceneStatus::paused)
  {
    for (const auto& object : objects)
    {
      object->stop();
    }
  }

  sceneStatus = SceneStatus::stopped;
}

void ObjectManager::variableUpdate(const float dt) const
{
  for (const auto& object : objects)
  {
    object->variableUpdate(dt);
  }

#ifdef COLLISION_DEBUG
  collisionManager->variableUpdate();
#endif
}

void ObjectManager::fixedUpdate(const float dt)
{
  if (sceneStatus != SceneStatus::running)
  {
    return;
  }

  timeAccumulator += dt;

  uint8_t steps = 1;
  while (timeAccumulator >= fixedUpdateDt && steps <= 3)
  {
    ++steps;

    for (const auto& object : objects)
    {
      object->fixedUpdate(fixedUpdateDt);
    }

    collisionManager->update();

    timeAccumulator -= fixedUpdateDt;
  }
}

void ObjectManager::displaySceneStatusGui()
{
  ImGui::Begin("Scene Status");

  constexpr int sceneStatusButtonWidth = 125;

  if (sceneStatus != SceneStatus::running)
  {
    if (ImGui::Button("Start", {sceneStatusButtonWidth, 0}))
    {
      startScene();
    }
  }

  if (sceneStatus == SceneStatus::running)
  {
    if (ImGui::Button("Pause", {sceneStatusButtonWidth, 0}))
    {
      pauseScene();
    }
  }

  if (sceneStatus != SceneStatus::stopped)
  {
    ImGui::SameLine();

    if (ImGui::Button("Stop", {sceneStatusButtonWidth, 0}))
    {
      resetScene();
    }
  }

  ImGui::End();
}
