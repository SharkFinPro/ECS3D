#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
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

  // A compound body of half-unit boxes (the contact points are on the box's real +x edge) tilted five degrees about z, resting on an edge of its +x side over a wide static box and
  // turning toward flat, with the given shapes on its owner and on its child. The contact is the child's, or the
  // owner's when the child has none.
  glm::vec3 rotationAfterEdgeContact(const Shape ownerShape, const Shape partShape)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, -1.5f, 0 }, { 5, 1, 5 });
    fixtures::addBoxCollider(ground);

    const auto owner = addObject(scene, "Owner", { 0, 0, 0 }, { 0.5f, 0.5f, 0.5f });
    const auto body = physicsFixtures::addBody(owner, false);
    const auto ownerCollider = addShape(owner, ownerShape);
    transformOf(owner)->setRotation({ 0, 0, -5 });
    body->setAngularVelocity({ 0, 0, 200 });

    const auto part = addChildObject(scene, "Part", owner);
    const auto partCollider = partShape == Shape::none ? ownerCollider : addShape(part, partShape);

    const std::array<glm::vec3, 2> edge{ glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f } };
    PhysicsSystem::handleCollision(*body, partCollider, ground, { 0, 0.01f, 0 }, edge, physicsFixtures::dt);

    return transformOf(owner)->getRotation();
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

TEST(CompoundBodyContact, ABoxChildIsLaidFlushWhateverItsOwnerCarries)
{
  // Positive control: the box on the owner itself, as the lone-collider case always worked.
  const auto alone = rotationAfterEdgeContact(Shape::box, Shape::none);
  fixtures::expectNear("owner box", physicsFixtures::localUpOf(alone), { 0, 1, 0 }, 1e-4f);

  fixtures::expectNear("owner none", physicsFixtures::localUpOf(rotationAfterEdgeContact(Shape::none, Shape::box)),
                       { 0, 1, 0 }, 1e-4f);
  fixtures::expectNear("owner sphere",
                       physicsFixtures::localUpOf(rotationAfterEdgeContact(Shape::sphere, Shape::box)),
                       { 0, 1, 0 }, 1e-4f);
}

TEST(CompoundBodyContact, ASphereChildIsNotLaidFlushJustBecauseItsOwnerHoldsABox)
{
  // The same tilt and turn with a box child is laid flush, so the sphere child below is left tilted by
  // its own shape and not by the setup.
  fixtures::expectNear("box child", physicsFixtures::localUpOf(rotationAfterEdgeContact(Shape::box, Shape::box)),
                       { 0, 1, 0 }, 1e-4f);

  const auto tilted = rotationAfterEdgeContact(Shape::box, Shape::sphere);
  EXPECT_NEAR(tilted.z, -5.0f, 1e-3f);
  EXPECT_GT(glm::length(physicsFixtures::localUpOf(tilted) - glm::vec3(0, 1, 0)), 0.05f);
}

TEST(CompoundBodyContact, ASphereChildOfAnOwnerWithoutOneRollsToRest)
{
  // Rolling resistance belongs to the sphere making the contact, not to whatever the owner carries.
  // Stopping takes it about 5 to 6 units; without rolling resistance it coasts about 15. The bound sits
  // between the two.
  const auto [stoppedPosition, stoppedVelocity] = compoundBallAfterRolling(Shape::none, 0.5f);
  EXPECT_LT(stoppedPosition.x, 10.0f);
  EXPECT_LT(glm::length(stoppedVelocity), 1e-3f);

  // Positive control: frictionless, the same ball slides the whole way at the speed it started with.
  const auto [slidPosition, slidVelocity] = compoundBallAfterRolling(Shape::none, 0.0f);
  EXPECT_GT(slidPosition.x, 25.0f);
  EXPECT_GT(slidVelocity.x, 0.9f * 3.0f * physicsFixtures::dt);
}
