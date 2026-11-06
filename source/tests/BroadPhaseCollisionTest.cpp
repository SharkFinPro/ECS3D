#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "broadphase/BroadPhase.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::Scene;

  constexpr float dt = 0.02f;

  struct Lcg {
    uint32_t state = 4242u;

    float next(const float low, const float high)
    {
      state = state * 1664525u + 1013904223u;
      return low + (high - low) * static_cast<float>((state >> 8) & 0xFFFFu) / 65535.0f;
    }
  };

  // Triggers by default, so a contact is reported without a response moving either side.
  std::shared_ptr<Object> addBox(const Scene& scene, const std::string& name, const glm::vec3& position,
                                 const bool dynamic, const bool trigger = true)
  {
    auto object = addObject(scene, name, position);
    fixtures::addBoxCollider(object)->setIsTrigger(trigger);

    if (dynamic)
    {
      fixtures::addRigidBody(object);
    }

    return object;
  }

  std::shared_ptr<Collider> colliderOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Collider>(ComponentType::collider);
  }

  std::shared_ptr<RigidBody> rigidBodyOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<RigidBody>(ComponentType::rigidBody);
  }

  bool contains(const std::vector<CollisionPair>& pairs, const std::shared_ptr<Object>& a,
                const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(pairs, CollisionPair::make(a->getUUID(), b->getUUID())) != pairs.end();
  }

  std::vector<CollisionPair> reportedPairs(const CollisionSystem& collisionSystem)
  {
    std::vector<CollisionPair> pairs;
    std::ranges::set_union(collisionSystem.getCollisionEnters(), collisionSystem.getCollisionStays(),
                           std::back_inserter(pairs));
    return pairs;
  }

  void destroyObject(const Scene& scene, const std::shared_ptr<Object>& object)
  {
    scene.objectManager->removeObject(object);
    scene.objectManager->deleteObjectsMarkedForDeletion();
  }

  bool boxesOverlap(const BoundingBox& a, const BoundingBox& b)
  {
    return a.maxX >= b.minX && a.minX <= b.maxX && a.maxY >= b.minY && a.minY <= b.maxY &&
           a.maxZ >= b.minZ && a.minZ <= b.maxZ;
  }

  bool layersMatch(const Collider& a, const Collider& b)
  {
    return (a.getMask() & (1u << b.getLayer())) != 0u && (b.getMask() & (1u << a.getLayer())) != 0u;
  }

  // Every dynamic body against every other collider in the scene, each answer taken on its own and unioned:
  // the pair set any correct broad phase must lead to, however it finds its candidates.
  std::vector<CollisionPair> referencePairs(const Scene& scene)
  {
    std::vector<std::shared_ptr<Object>> colliders;
    for (const auto& object : scene.objectManager->getAllObjects())
    {
      if (colliderOf(object))
      {
        colliders.push_back(object);
      }
    }

    std::vector<CollisionPair> pairs;
    for (const auto& body : colliders)
    {
      const auto bodyRigid = rigidBodyOf(body);
      if (!bodyRigid)
      {
        continue;
      }

      for (const auto& other : colliders)
      {
        if (other == body || bodyRigid == rigidBodyOf(other) || body->isAncestorOf(other) ||
            other->isAncestorOf(body))
        {
          continue;
        }

        auto& collider = *colliderOf(body);
        auto& otherCollider = *colliderOf(other);
        if (layersMatch(collider, otherCollider) &&
            boxesOverlap(collider.getBoundingBox(), otherCollider.getBoundingBox()) &&
            collisions::intersects(collider, otherCollider))
        {
          pairs.push_back(CollisionPair::make(body->getUUID(), other->getUUID()));
        }
      }
    }

    std::ranges::sort(pairs);
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    return pairs;
  }

  // A pile of solid boxes and spheres over a ground, some starting in contact. Columns share an x, so contact
  // order has to fall back to the tie rule.
  void populatePile(const Scene& scene, std::vector<std::shared_ptr<Object>>& objects)
  {
    objects.push_back(addBox(scene, "Ground", { 0, -1.0f, 0 }, false, false));
    fixtures::transformOf(objects.back())->setScale({ 12.0f, 1.0f, 12.0f });

    Lcg random;
    for (int i = 0; i < 36; ++i)
    {
      const float x = static_cast<float>(i % 6) * 1.9f - 5.0f;
      const glm::vec3 position{ x, 0.9f + static_cast<float>(i / 6) * 1.8f, random.next(-1.0f, 1.0f) };
      if (i % 5 == 0)
      {
        auto ball = addObject(scene, "Ball" + std::to_string(i), position);
        fixtures::addSphereCollider(ball, 0.9f);
        fixtures::addRigidBody(ball);
        objects.push_back(ball);
      }
      else
      {
        objects.push_back(addBox(scene, "Box" + std::to_string(i), position, true, false));
        fixtures::transformOf(objects.back())->setScale(glm::vec3(0.9f));
      }
    }
  }
}

TEST(BroadPhaseCollision, ProxiesPersistAndNothingIsQueriedWhileNothingMoves)
{
  const auto scene = makeScene();
  const auto ground = addBox(scene, "Ground", { 0, -1.0f, 0 }, false);
  const auto body = addBox(scene, "Body", { 0, 0.5f, 0 }, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);
  EXPECT_EQ(collisionSystem.getBroadPhase().getLastMovedCount(), 0u);
  EXPECT_TRUE(contains(collisionSystem.getCollisionStays(), body, ground));

  // Positive control: moving the body clear re-queries it, and the pair leaves.
  fixtures::transformOf(body)->setPosition({ 0, 20.0f, 0 });
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_EQ(collisionSystem.getBroadPhase().getLastMovedCount(), 1u);
  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), body, ground));
}

TEST(BroadPhaseCollision, ASpawnedBodyIsFoundAndADestroyedOneLeaves)
{
  const auto scene = makeScene();
  const auto ground = addBox(scene, "Ground", { 0, -1.0f, 0 }, false);
  const auto first = addBox(scene, "First", { 0, 0.5f, 0 }, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), first, ground));

  const auto second = addBox(scene, "Second", { 0.5f, 0.5f, 0 }, true);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), second, ground));
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), first, second));
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 3u);

  destroyObject(scene, first);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), first, ground));
  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), first, second));
  EXPECT_TRUE(contains(collisionSystem.getCollisionStays(), second, ground));
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);
}

TEST(BroadPhaseCollision, GainingARigidBodyMakesAStaticColliderCollide)
{
  const auto scene = makeScene();
  const auto ground = addBox(scene, "Ground", { 0, -1.0f, 0 }, false);
  const auto crate = addBox(scene, "Crate", { 0, 0.5f, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(reportedPairs(collisionSystem).empty());

  const auto body = fixtures::addRigidBody(crate);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), crate, ground));
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);

  crate->removeComponent(body);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), crate, ground));
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);
}

TEST(BroadPhaseCollision, AReplacedColliderKeepsCollidingAsItsNewShape)
{
  const auto scene = makeScene();
  const auto ground = addBox(scene, "Ground", { 0, -1.0f, 0 }, false);
  const auto body = addBox(scene, "Body", { 0, 0.5f, 0 }, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(contains(collisionSystem.getCollisionEnters(), body, ground));

  body->removeComponent(colliderOf(body));
  auto sphere = fixtures::addSphereCollider(body, 0.25f);
  sphere->setIsTrigger(true);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  // The small sphere at y = 0.5 clears the ground's top at y = 0, which the old box reached down past.
  EXPECT_TRUE(contains(collisionSystem.getCollisionExits(), body, ground));
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);

  sphere->setRadius(1.0f);
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), body, ground));
}

TEST(BroadPhaseCollision, AStaticColliderMovedOntoARestingBodyCollides)
{
  const auto scene = makeScene();
  const auto body = addBox(scene, "Body", { 0, 0, 0 }, true);
  const auto platform = addBox(scene, "Platform", { 30, 0, 0 }, false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_TRUE(reportedPairs(collisionSystem).empty());

  fixtures::transformOf(platform)->setPosition({ 1.5f, 0, 0 });
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), body, platform));
}

TEST(BroadPhaseCollision, ABodyWhoseBoundsOverflowPairsWithNothingAndBreaksNothing)
{
  const auto scene = makeScene();
  const auto lost = addBox(scene, "Lost", { 0, 0, 0 }, true);
  const auto first = addBox(scene, "First", { 5, 0, 0 }, true);
  const auto second = addBox(scene, "Second", { 6, 0, 0 }, true);

  // A transform refuses a non-finite value, but finite ones this large still overflow the bounds.
  fixtures::transformOf(lost)->setPosition({ 3.0e38f, 0, 0 });
  fixtures::transformOf(lost)->setScale({ 1.0e38f, 1, 1 });
  ASSERT_FALSE(std::isfinite(colliderOf(lost)->getBoundingBox().maxX));

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_EQ(reportedPairs(collisionSystem), std::vector{ CollisionPair::make(first->getUUID(), second->getUUID()) });
  EXPECT_TRUE(collisionSystem.getBroadPhase().getDynamicTree().isValid());

  fixtures::transformOf(lost)->setScale({ 1, 1, 1 });
  fixtures::transformOf(lost)->setPosition({ 4.0f, 0, 0 });
  collisionSystem.fixedUpdate(*scene.objectManager, dt);

  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), lost, first));
}

TEST(BroadPhaseCollision, ResetDropsTheBroadPhaseAndTheNextTickRebuildsIt)
{
  const auto scene = makeScene();
  const auto ground = addBox(scene, "Ground", { 0, -1.0f, 0 }, false);
  const auto body = addBox(scene, "Body", { 0, 0.5f, 0 }, true);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  ASSERT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);

  collisionSystem.reset();
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 0u);

  collisionSystem.fixedUpdate(*scene.objectManager, dt);
  EXPECT_EQ(collisionSystem.getBroadPhase().getProxyCount(), 2u);
  EXPECT_TRUE(contains(collisionSystem.getCollisionEnters(), body, ground));
}

TEST(BroadPhaseCollision, ThePairSetMatchesAFullScanThroughSpawnsDestroysAndTeleports)
{
  const auto scene = makeScene();
  std::vector<std::shared_ptr<Object>> objects;
  populatePile(scene, objects);

  // A layer-filtered body and a compound body, so the filters stay in the comparison.
  const auto filtered = addBox(scene, "Filtered", { 0, 0.9f, 3.0f }, true, false);
  colliderOf(filtered)->setLayer(3);
  colliderOf(filtered)->setMask(1u << 3);
  const auto parent = addBox(scene, "Parent", { 3.0f, 0.9f, 3.0f }, true, false);
  const auto child = fixtures::addChildObject(scene, "Child", parent);
  fixtures::addBoxCollider(child);

  CollisionSystem collisionSystem;
  Lcg random;
  size_t reported = 0;

  for (int tick = 0; tick < 90; ++tick)
  {
    if (tick % 15 == 5)
    {
      auto spawned = addBox(scene, "Spawned" + std::to_string(tick), { random.next(-4, 4), 4.0f, 0 }, true, false);
      objects.push_back(spawned);
    }

    if (tick % 20 == 10)
    {
      destroyObject(scene, objects[static_cast<size_t>(tick / 10)]);
      objects.erase(objects.begin() + tick / 10);
    }

    if (tick % 25 == 12)
    {
      fixtures::transformOf(objects.back())->setPosition({ random.next(-4, 4), 1.0f, random.next(-1, 1) });
    }

    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

    const auto expected = referencePairs(scene);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    ASSERT_EQ(reportedPairs(collisionSystem), expected) << "tick " << tick;
    reported += expected.size();
  }

  EXPECT_GT(reported, 500u);
}

TEST(BroadPhaseCollision, SteppingDoesNotDependOnHowProxyIdsWereHandedOut)
{
  // The second scene first creates and destroys a crowd of colliders, so its proxies reuse freed ids in a
  // different order and its pair cache iterates in a different order. Contact resolution must not notice.
  Scene first = makeScene();
  Scene second = makeScene();
  CollisionSystem firstSystem;
  CollisionSystem secondSystem;

  std::vector<std::shared_ptr<Object>> decoys;
  for (int i = 0; i < 30; ++i)
  {
    decoys.push_back(addBox(second, "Decoy" + std::to_string(i), { static_cast<float>(i), 0, 40.0f }, i % 2 == 0));
  }

  secondSystem.fixedUpdate(*second.objectManager, dt);
  for (const auto& decoy : decoys)
  {
    destroyObject(second, decoy);
  }

  secondSystem.fixedUpdate(*second.objectManager, dt);

  std::vector<std::shared_ptr<Object>> firstObjects;
  std::vector<std::shared_ptr<Object>> secondObjects;
  populatePile(first, firstObjects);
  populatePile(second, secondObjects);

  std::vector<glm::vec3> starts;
  for (const auto& object : firstObjects)
  {
    starts.push_back(fixtures::positionOf(object));
  }

  for (int tick = 0; tick < 80; ++tick)
  {
    PhysicsSystem::fixedUpdate(*first.objectManager, dt);
    firstSystem.fixedUpdate(*first.objectManager, dt);

    PhysicsSystem::fixedUpdate(*second.objectManager, dt);
    secondSystem.fixedUpdate(*second.objectManager, dt);
  }

  ASSERT_EQ(firstObjects.size(), secondObjects.size());
  size_t moved = 0;
  for (size_t i = 0; i < firstObjects.size(); ++i)
  {
    const auto a = fixtures::transformOf(firstObjects[i]);
    const auto b = fixtures::transformOf(secondObjects[i]);

    EXPECT_EQ(a->getPosition(), b->getPosition()) << firstObjects[i]->getName();
    EXPECT_EQ(a->getRotation(), b->getRotation()) << firstObjects[i]->getName();
    moved += a->getPosition() != starts[i];
  }

  // The pile did settle and interact, so the comparison covered real contact resolution.
  EXPECT_GT(moved, 10u);
}

TEST(BroadPhaseCollision, ContactOrderFollowsPositionNotTheOrderObjectsWereCreatedIn)
{
  // Balls stacked in threes, one resting on two below it placed as mirror images, so the top ball resolves first
  // (it sits highest) with two contacts of bitwise-equal depth: a pair of spheres is solved in closed form, and
  // mirrored offsets give the same distance. Built in opposite orders, the scenes must step identically: ties
  // fall back to min x, never to creation order or proxy ids.
  constexpr int stacks = 4;
  const auto build = [](const bool reversed, std::vector<std::shared_ptr<Object>>& balls) {
    auto scene = makeScene();
    const auto ground = addBox(scene, "Ground", { 14.0f, -1.0f, 0 }, false, false);
    fixtures::transformOf(ground)->setScale({ 20.0f, 1.0f, 4.0f });

    std::vector<glm::vec3> positions;
    for (int stack = 0; stack < stacks; ++stack)
    {
      const float x = static_cast<float>(stack) * 8.0f;
      positions.push_back({ x, 0.875f, 0 });
      positions.push_back({ x + 3.5f, 0.875f, 0 });
      positions.push_back({ x + 1.75f, 1.375f, 0 });
    }

    balls.assign(positions.size(), nullptr);
    for (size_t step = 0; step < positions.size(); ++step)
    {
      const size_t i = reversed ? positions.size() - 1 - step : step;
      auto ball = addObject(scene, "Ball" + std::to_string(i), positions[i]);
      fixtures::addSphereCollider(ball, 1.0f);
      fixtures::addRigidBody(ball);
      balls[i] = ball;
    }

    return scene;
  };

  std::vector<std::shared_ptr<Object>> forwardBalls;
  std::vector<std::shared_ptr<Object>> reversedBalls;
  const auto forward = build(false, forwardBalls);
  const auto reversed = build(true, reversedBalls);

  CollisionSystem forwardSystem;
  CollisionSystem reversedSystem;
  for (int tick = 0; tick < 30; ++tick)
  {
    PhysicsSystem::fixedUpdate(*forward.objectManager, dt);
    forwardSystem.fixedUpdate(*forward.objectManager, dt);

    PhysicsSystem::fixedUpdate(*reversed.objectManager, dt);
    reversedSystem.fixedUpdate(*reversed.objectManager, dt);
  }

  size_t topsMovedSideways = 0;
  size_t topsMovedTowardMinX = 0;
  for (size_t i = 0; i < forwardBalls.size(); ++i)
  {
    const auto a = fixtures::transformOf(forwardBalls[i]);
    const auto b = fixtures::transformOf(reversedBalls[i]);

    EXPECT_EQ(a->getPosition(), b->getPosition()) << "ball " << i;
    EXPECT_EQ(a->getRotation(), b->getRotation()) << "ball " << i;

    if (i % 3 == 2)
    {
      const float center = static_cast<float>(i / 3) * 8.0f + 1.75f;
      topsMovedSideways += a->getPosition().x != center;
      topsMovedTowardMinX += a->getPosition().x < center;
    }
  }

  // Positive control: resolving one tied contact before the other knocks each top ball off center, so the
  // order had something to decide.
  EXPECT_EQ(topsMovedSideways, static_cast<size_t>(stacks));

  // The direction is pinned too: the contact with the lower min x is resolved first.
  EXPECT_EQ(topsMovedTowardMinX, static_cast<size_t>(stacks));
}

TEST(BroadPhaseCollision, AGridLargeEnoughToWarmBoundsInParallelMatchesAFullScan)
{
  const auto scene = makeScene();
  Lcg random;

  // Overlapping neighbors in a grid of statics and bodies: past 256 colliders the bounds warm goes parallel.
  int count = 0;
  for (int row = 0; row < 18; ++row)
  {
    for (int column = 0; column < 18; ++column)
    {
      const glm::vec3 position{ static_cast<float>(column) * 0.95f + random.next(-0.05f, 0.05f),
                                random.next(0.0f, 0.2f), static_cast<float>(row) * 0.95f };
      static_cast<void>(addBox(scene, "Box" + std::to_string(count), position, count % 3 != 0, count % 4 == 1));
      ++count;
    }
  }

  ASSERT_GE(count, 300);

  CollisionSystem collisionSystem;
  size_t reported = 0;
  for (int tick = 0; tick < 4; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

    const auto expected = referencePairs(scene);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);

    ASSERT_EQ(reportedPairs(collisionSystem), expected) << "tick " << tick;
    reported += expected.size();
  }

  EXPECT_GT(reported, 500u);
}
