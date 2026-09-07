#include "TestScene.h"

#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <stdexcept>

namespace fixtures {
  Scene::Scene(const Components components)
    : componentRegistry(std::make_shared<ComponentRegistry>())
  {
    if (components == Components::registered)
    {
      registerDataComponents(*componentRegistry);
    }

    objectManager = std::make_unique<ObjectManager>(componentRegistry);
  }

  Scene::~Scene() = default;

  Scene::Scene(Scene&& other) noexcept = default;

  Scene& Scene::operator=(Scene&& other) noexcept = default;

  Scene makeScene()
  {
    return {};
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name)
  {
    auto object = std::make_shared<Object>(name);
    scene.objectManager->addObject(object);

    return object;
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name, const glm::vec3& position)
  {
    auto object = addObject(scene, name);
    transformOf(object)->setPosition(position);

    return object;
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name, const glm::vec3& position,
                                    const glm::vec3& scale)
  {
    auto object = addObject(scene, name, position);
    transformOf(object)->setScale(scale);

    return object;
  }

  std::shared_ptr<Object> addChildObject(const Scene& scene, const std::string& name,
                                         const std::shared_ptr<Object>& parent)
  {
    auto object = std::make_shared<Object>(name);
    object->setParent(parent);
    scene.objectManager->addObject(object);

    return object;
  }

  std::shared_ptr<BoxCollider> addBoxCollider(const std::shared_ptr<Object>& object)
  {
    auto collider = std::make_shared<BoxCollider>();
    object->addComponent(collider);

    return collider;
  }

  std::shared_ptr<SphereCollider> addSphereCollider(const std::shared_ptr<Object>& object, const float radius)
  {
    auto collider = std::make_shared<SphereCollider>();
    object->addComponent(collider);
    collider->setRadius(radius);

    return collider;
  }

  std::shared_ptr<RigidBody> addRigidBody(const std::shared_ptr<Object>& object)
  {
    auto body = std::make_shared<RigidBody>();
    object->addComponent(body);

    return body;
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    auto transform = object->getComponent<Transform>(ComponentType::transform);
    if (!transform)
    {
      throw std::runtime_error(object->getName() + " has no transform");
    }

    return transform;
  }

  glm::vec3 positionOf(const std::shared_ptr<Object>& object)
  {
    return transformOf(object)->getPosition();
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected, const float tolerance)
  {
    // Spelled out component by component because PrintToString came back as a hex dump for a glm vector.
    SCOPED_TRACE(::testing::Message()
                 << what
                 << ": expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
                 << ", got (" << actual.x << ", " << actual.y << ", " << actual.z << ")");

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  void expectNear(const glm::vec3& actual, const glm::vec3& expected, const float tolerance)
  {
    expectNear("vector", actual, expected, tolerance);
  }
}
