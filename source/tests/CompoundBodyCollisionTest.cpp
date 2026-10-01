#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "CollisionSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <ostream>
#include <vector>
#include <uuid.h>

namespace {
  using fixtures::expectNear;
  using fixtures::makeScene;
  using fixtures::positionOf;

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
