#include "ObjectManager.h"

#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>

#include "components/ModelRenderer.h"

ObjectManager::ObjectManager()
  : ecs(nullptr), fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
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

void ObjectManager::addObject(std::shared_ptr<Object> object)
{
  object->setManager(this);
  objects.push_back(std::move(object));
}

void ObjectManager::enableRendering() const
{
  for (const auto& object : objects)
  {
    const auto modelRenderer = std::dynamic_pointer_cast<ModelRenderer>(object->getComponent(ComponentType::modelRenderer));
    if (modelRenderer != nullptr)
    {
      modelRenderer->enableRendering();
    }
  }
}

void ObjectManager::disableRendering()
{
  for (const auto& object : objects)
  {
    const auto modelRenderer = std::dynamic_pointer_cast<ModelRenderer>(object->getComponent(ComponentType::modelRenderer));
    if (modelRenderer != nullptr)
    {
      modelRenderer->disableRendering();
    }
  }
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
      object->fixedUpdate(fixedUpdateDt);
    }

    checkCollisions();

    timeAccumulator -= fixedUpdateDt;
  }
}

void ObjectManager::checkCollisions()
{
  for (const auto& object : objects)
  {
    auto rigidBody = std::dynamic_pointer_cast<RigidBody>(object->getComponent(ComponentType::rigidBody));
    auto collider = std::dynamic_pointer_cast<Collider>(object->getComponent(ComponentType::collider));

    if (!rigidBody || !collider)
    {
      continue;
    }

    std::vector<std::shared_ptr<Object>> collidedObjects;
    findCollisions(object, collider, collidedObjects);

    if (!collidedObjects.empty())
    {
      handleCollisions(rigidBody, collider, collidedObjects);
    }
  }
}

void ObjectManager::findCollisions(const std::shared_ptr<Object>& object,
                                   const std::shared_ptr<Collider>& collider,
                                   std::vector<std::shared_ptr<Object>>& collidedObjects)
{
  for (const auto& other : objects)
  {
    if (other == object)
    {
      continue;
    }

    if (collider->collidesWith(other, nullptr))
    {
      collidedObjects.emplace_back(other);
    }
  }
}

void ObjectManager::handleCollisions(const std::shared_ptr<RigidBody>& rigidBody,
                                     const std::shared_ptr<Collider>& collider,
                                     std::vector<std::shared_ptr<Object>>& collidedObjects)
{
  std::vector<bool> chosenFlags(collidedObjects.size(), false);
  std::vector<float> distances;

  for (const auto& collidedObject : collidedObjects)
  {
    glm::vec3 mtv;
    collider->collidesWith(collidedObject, &mtv);

    distances.push_back(dot(mtv, mtv));
  }

  std::vector<float> sortedDistances = distances;
  std::ranges::sort(sortedDistances, std::greater());

  for (const float sortedDistance : sortedDistances)
  {
    if (sortedDistance == 0)
    {
      break;
    }

    for (size_t j = 0; j < distances.size(); j++)
    {
      if (sortedDistance == distances[j] && !chosenFlags[j])
      {
        chosenFlags[j] = true;

        glm::vec3 mtv;
        if (collider->collidesWith(collidedObjects[j], &mtv))
        {
          rigidBody->handleCollision(mtv, collidedObjects[j]);
        }
      }
    }
  }
}
