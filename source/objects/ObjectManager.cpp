#include "ObjectManager.h"

#include "Object.h"

ObjectManager::ObjectManager()
  : fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
{}

void ObjectManager::update(const float dt)
{
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

void ObjectManager::variableUpdate(const float dt)
{
  for (const auto& object : objects)
  {
    object->variableUpdate(dt);
  }
}

void ObjectManager::fixedUpdate(const float dt)
{
  timeAccumulator += dt;

  while (timeAccumulator >= fixedUpdateDt)
  {
    for (const auto& object : objects)
    {
      object->fixedUpdate(dt);
    }

    timeAccumulator -= fixedUpdateDt;
  }
}
