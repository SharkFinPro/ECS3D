#include "ObjectManager.h"

#include "Object.h"
#include "components/RigidBody.h"
#include "components/collisions/Collider.h"
#include <glm/glm.hpp>
#include <algorithm>

#include "components/LightRenderer.h"
#include "components/ModelRenderer.h"
#include "components/Transform.h"

ObjectManager::ObjectManager()
  : ecs(nullptr), fixedUpdateDt(1.0f / 50.0f), timeAccumulator(0.0f)
{}

void ObjectManager::update(const float dt)
{
  ImGui::Begin("Objects");
  for (const auto& object : objects)
  {
    ImGui::PushID(&object);

    if (ImGui::Selectable("Object", selectedObject == object))
    {
      selectedObject = object;
    }

    ImGui::PopID();
  }
  ImGui::End();

  ImGui::Begin("Light");
  if (selectedObject)
  {
    if (const auto lightRenderer = std::dynamic_pointer_cast<LightRenderer>(selectedObject->getComponent(ComponentType::lightRenderer)))
    {
      lightRenderer->displayGui();
    }
  }
  ImGui::End();

  ImGui::Begin("Transform");
  if (selectedObject)
  {
    const auto transform = std::dynamic_pointer_cast<Transform>(selectedObject->getComponent(ComponentType::transform));

    glm::vec3 position = transform->getPosition();
    glm::vec3 rotation = transform->getRotation();
    glm::vec3 scale = transform->getScale();

    ImGui::PushID(1);
    ImGui::Text("Control Position:");
    ImGui::SliderFloat("x", &position.x, -30.0f, 30.0f);
    ImGui::SliderFloat("y", &position.y, -30.0f, 30.0f);
    ImGui::SliderFloat("z", &position.z, -30.0f, 30.0f);
    ImGui::PopID();

    ImGui::PushID(2);
    ImGui::Text("Control Rotation:");
    ImGui::SliderFloat("x", &rotation.x, 0.0f, 360.0f);
    ImGui::SliderFloat("y", &rotation.y, 0.0f, 360.0f);
    ImGui::SliderFloat("z", &rotation.z, 0.0f, 360.0f);
    ImGui::PopID();

    ImGui::PushID(3);
    ImGui::Text("Control Scale:");
    ImGui::SliderFloat("x", &scale.x, 0.1f, 10.0f);
    ImGui::SliderFloat("y", &scale.y, 0.1f, 10.0f);
    ImGui::SliderFloat("z", &scale.z, 0.1f, 10.0f);
    ImGui::PopID();

    if (ImGui::Button("Reset"))
    {
      transform->reset();

      if (const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(selectedObject->getComponent(ComponentType::rigidBody)))
      {
        rigidBody->setVelocity({ 0, 0, 0 });
      }
    }
    else
    {
      transform->move(position - transform->getPosition());
      transform->setRotation(rotation);
      transform->setScale(scale);
    }
  }
  ImGui::End();

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

void ObjectManager::resetObjects() const
{
  for (const auto& object : objects)
  {
    if (const auto transform = std::dynamic_pointer_cast<Transform>(object->getComponent(ComponentType::transform)))
    {
      transform->reset();
    }

    if (const auto rigidBody = std::dynamic_pointer_cast<RigidBody>(object->getComponent(ComponentType::rigidBody)))
    {
      rigidBody->setVelocity({ 0.0f, 0.0f, 0.0f });
    }
  }
}

void ObjectManager::variableUpdate(const float dt) const
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

void ObjectManager::checkCollisions() const
{
#pragma omp parallel for default(none) num_threads(6)
  for (int i = 0; i < objects.size(); i++)
  {
    const auto object = objects[i];

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
                                   std::vector<std::shared_ptr<Object>>& collidedObjects) const
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
                                     const std::vector<std::shared_ptr<Object>>& collidedObjects)
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
