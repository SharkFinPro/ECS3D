#include "ObjectManager.h"

ObjectManager::ObjectManager()
{
}

void ObjectManager::update(const float dt)
{
  fixedUpdate(dt);
  variableUpdate(dt);
}

void ObjectManager::variableUpdate(const float dt)
{
}

void ObjectManager::fixedUpdate(const float dt)
{
}
