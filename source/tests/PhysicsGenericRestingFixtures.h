#ifndef PHYSICSGENERICRESTINGFIXTURES_H
#define PHYSICSGENERICRESTINGFIXTURES_H

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/vec3.hpp>
#include <memory>
#include <utility>

// A body dropped or slid onto a static ground, with the collider of each given as a box or a convex hull.
namespace restingFixtures {
  enum class Shape { box, hull };

  struct Launch {
    glm::vec3 position{ 0, 5, 0 };
    glm::vec3 rotation{ 0 };
    glm::vec3 velocity{ 0 };
    glm::vec3 spin{ 0 };
  };

  inline void attach(const std::shared_ptr<Object>& object, const Shape shape, ConvexHull hull = {})
  {
    if (shape == Shape::box)
    {
      fixtures::addBoxCollider(object);
      return;
    }

    auto collider = std::make_shared<ConvexMeshCollider>();
    if (!hull.vertices.empty())
    {
      EXPECT_TRUE(collider->setHull(std::move(hull)));
    }
    object->addComponent(collider);
  }

  struct Rig {
    fixtures::Scene scene;
    std::shared_ptr<Object> body;
    std::shared_ptr<RigidBody> rigidBody;
    CollisionSystem collisions;

    Rig(const Shape bodyShape, const Shape groundShape, const Launch& launch, ConvexHull hull = {},
        ConvexHull groundHull = {})
    {
      const auto ground = fixtures::addObject(scene, "Ground", { 0, 0, 0 });
      fixtures::transformOf(ground)->setScale(groundHull.vertices.empty() ? glm::vec3(10, 1, 10) : glm::vec3(1));
      attach(ground, groundShape, std::move(groundHull));

      body = addBodyAt(launch, bodyShape, std::move(hull));
    }

    std::shared_ptr<Object> addBodyAt(const Launch& launch, const Shape shape, ConvexHull hull = {})
    {
      const auto object = fixtures::addObject(scene, "Body", launch.position);
      fixtures::transformOf(object)->setRotation(launch.rotation);
      attach(object, shape, std::move(hull));

      rigidBody = physicsFixtures::addBody(object, true);
      rigidBody->setVelocity(launch.velocity);
      rigidBody->setAngularVelocity(launch.spin);

      return object;
    }

    void step(const float dt)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisions.fixedUpdate(*scene.objectManager, dt);
    }
  };
}

#endif //PHYSICSGENERICRESTINGFIXTURES_H
