#include "ObjectManager.h"

ObjectManager::ObjectManager()
  : fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
{}

void ObjectManager::update(const float dt)
{
  fixedUpdate(dt);
  variableUpdate(dt);
}

void ObjectManager::variableUpdate(const float dt)
{
  for (const auto& object : objects)
  {
    // TODO: execute object variableUpdate
  }
}

void ObjectManager::fixedUpdate(const float dt)
{
  timeAccumulator += dt;

  while (timeAccumulator >= fixedUpdateDt)
  {
    for (const auto& object : objects)
    {
      // TODO: execute object fixedUpdate
    }

    timeAccumulator -= fixedUpdateDt;
  }
}
