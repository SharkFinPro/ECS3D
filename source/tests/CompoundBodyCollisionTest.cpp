#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <ostream>
#include <vector>
#include <uuid.h>

namespace {
  using fixtures::expectNear;
  using fixtures::makeScene;
  using fixtures::positionOf;
  using fixtures::transformOf;

  constexpr float dt = 1.0f / 50.0f;

  bool contains(const std::vector<CollisionPair>& pairs, const std::shared_ptr<Object>& a,
                const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(pairs, CollisionPair::make(a->getUUID(), b->getUUID())) != pairs.end();
  }

  // A child with a unit box collider at a local position under the given parent.
  std::shared_ptr<Object> addPart(const fixtures::Scene& scene, const std::shared_ptr<Object>& parent,
                                  const glm::vec3& localPosition)
  {
    auto part = addChildObject(scene, "Part", parent);
    fixtures::transformOf(part)->setPosition(localPosition);
    fixtures::addBoxCollider(part);

    return part;
  }
}

TEST(CompoundBodyCollision, SiblingCollidersOfOneBodyDoNotCollideWithEachOther)
{
  const auto scene = makeScene();
  const auto body = addObject(scene, "Body", { 0, 0, 0 });
  fixtures::addRigidBody(body);

  const auto first = addPart(scene, body, { 0, 0, 0 });
  const auto second = addPart(scene, body, { 1, 0, 0 });

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  EXPECT_TRUE(collisionSystem.getCollisionStays().empty());
  expectNear("body position", positionOf(body), { 0, 0, 0 });
  expectNear("body velocity", body->getComponent<RigidBody>(ComponentType::rigidBody)->getVelocity(), { 0, 0, 0 });

  // Giving one part a body of its own makes the same overlap a real contact, so the silence above is
  // about the shared body and not about the boxes missing each other.
  fixtures::addRigidBody(second);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), first, second));
}

TEST(CompoundBodyCollision, AnAncestorsColliderDoesNotCollideWithADeepDescendant)
{
  const auto scene = makeScene();
  const auto grandparent = addObject(scene, "Grandparent", { 0, 0, 0 });
  fixtures::addRigidBody(grandparent);
  fixtures::addBoxCollider(grandparent);

  const auto middle = addChildObject(scene, "Middle", grandparent);
  const auto grandchild = addPart(scene, middle, { 1, 0, 0 });

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(collisionSystem.getCollisionEnters().empty());
  expectNear("body position", positionOf(grandparent), { 0, 0, 0 });

  // A static box overlapping the grandchild is a real contact for the same body.
  const auto wall = addObject(scene, "Wall", { 2, 0, 0 });
  fixtures::addBoxCollider(wall);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), grandchild, wall));
}

TEST(CompoundBodyCollision, ASeparateBodyStillCollidesAndIsPushedApart)
{
  const auto scene = makeScene();
  const auto mover = addObject(scene, "Mover", { 0, 0, 0 });
  fixtures::addRigidBody(mover);
  fixtures::addBoxCollider(mover);

  const auto wall = addObject(scene, "Wall", { 1, 0, 0 });
  fixtures::addBoxCollider(wall);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), mover, wall));
  EXPECT_LT(positionOf(mover).x, 0.0f);
}

TEST(CompoundBodyCollision, AChildColliderOfOneBodyStillCollidesWithAnotherBody)
{
  const auto scene = makeScene();
  const auto body = addObject(scene, "Body", { 0, 0, 0 });
  fixtures::addRigidBody(body);
  const auto part = addPart(scene, body, { 0, 0, 0 });

  const auto wall = addObject(scene, "Wall", { 1, 0, 0 });
  fixtures::addBoxCollider(wall);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), part, wall));
  EXPECT_LT(positionOf(body).x, 0.0f);
}

namespace {
  enum class Shape { none, box, sphere };

  std::shared_ptr<Collider> addShape(const std::shared_ptr<Object>& object, const Shape shape)
  {
    switch (shape)
    {
      case Shape::box:
        return fixtures::addBoxCollider(object);
      case Shape::sphere:
        return fixtures::addSphereCollider(object, 1.0f);
      case Shape::none:
        break;
    }

    return nullptr;
  }

  // A unit ball sliding along a wide static ground, as its own body or as the child collider of a body whose
  // owner has the given shape. Returns where it is and how fast it moves ten seconds later.
  std::pair<glm::vec3, glm::vec3> compoundBallAfterRolling(const Shape ownerShape, const float friction)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, 0, 0 }, { 50, 1, 50 });
    fixtures::addBoxCollider(ground);

    const auto owner = addObject(scene, "Owner", { 0, 2, 0 });
    const auto body = physicsFixtures::addBody(owner, true);
    addShape(owner, ownerShape);
    body->setFriction(friction);
    body->setVelocity({ 3.0f * physicsFixtures::dt, 0, 0 });

    const auto part = addChildObject(scene, "Part", owner);
    addShape(part, Shape::sphere);

    CollisionSystem collisionSystem;
    for (int tick = 0; tick < 100; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, physicsFixtures::dt);
      collisionSystem.fixedUpdate(*scene.objectManager, physicsFixtures::dt);
    }

    return { positionOf(owner), body->getVelocity() };
  }
}

TEST(CompoundBodyContact, ASphereChildOfAnOwnerWithoutOneRollsToRest)
{
  // Rolling resistance belongs to the sphere making the contact, not to whatever the owner carries.
  // Stopping takes it about 6.4 units; without rolling resistance it coasts about 15. The bound sits
  // between the two.
  const auto [stoppedPosition, stoppedVelocity] = compoundBallAfterRolling(Shape::none, 0.5f);
  EXPECT_LT(stoppedPosition.x, 10.0f);
  EXPECT_LT(glm::length(stoppedVelocity), 1e-3f);

  // Positive control: frictionless, the same ball slides the whole way at the speed it started with.
  const auto [slidPosition, slidVelocity] = compoundBallAfterRolling(Shape::none, 0.0f);
  EXPECT_GT(slidPosition.x, 25.0f);
  EXPECT_GT(slidVelocity.x, 0.9f * 3.0f * physicsFixtures::dt);
}
