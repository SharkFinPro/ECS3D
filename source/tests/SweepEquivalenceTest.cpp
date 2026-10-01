#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/Collider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::positionOf;
  using fixtures::Scene;

  constexpr float dt = 0.02f;

  struct SweepScene {
    Scene scene = makeScene();
    std::vector<std::shared_ptr<Object>> objects;
  };

  // A fixed generator, so every run and both scenes of the determinism test see the same layout.
  struct Lcg {
    uint32_t state = 12345u;

    float next(const float low, const float high)
    {
      state = state * 1664525u + 1013904223u;
      return low + (high - low) * static_cast<float>((state >> 8) & 0xFFFFu) / 65535.0f;
    }
  };

  std::shared_ptr<Object> addBody(SweepScene& sweep, const std::string& name, const glm::vec3& position,
                                  const bool dynamic)
  {
    auto object = addObject(sweep.scene, name, position);
    fixtures::addBoxCollider(object);

    if (dynamic)
    {
      fixtures::addRigidBody(object);
    }

    sweep.objects.push_back(object);
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

  // A few dozen bodies: a spread of random ones, clusters that share an x (equal minX), a trigger, a
  // layer-filtered pair and a parent with its child.
  void populate(SweepScene& sweep)
  {
    Lcg random;

    for (int i = 0; i < 24; ++i)
    {
      addBody(sweep, "Spread" + std::to_string(i), { random.next(-14, 14), random.next(0, 3), random.next(-6, 6) },
              i % 3 != 0);
    }

    for (int i = 0; i < 8; ++i)
    {
      addBody(sweep, "Column" + std::to_string(i), { 4.0f, 0.5f * static_cast<float>(i), 0.0f }, i % 2 == 0);
    }

    for (int i = 0; i < 4; ++i)
    {
      addBody(sweep, "Row" + std::to_string(i), { -9.0f, 0.0f, 1.5f * static_cast<float>(i) }, i != 1);
    }

    const auto trigger = addBody(sweep, "Trigger", { 4.5f, 1.0f, 0.5f }, true);
    colliderOf(trigger)->setIsTrigger(true);

    const auto layerA = addBody(sweep, "LayerA", { 12.0f, 8.0f, 0.0f }, true);
    const auto layerB = addBody(sweep, "LayerB", { 12.5f, 8.0f, 0.0f }, true);
    const auto layerPeer = addBody(sweep, "LayerPeer", { 12.0f, 8.5f, 0.0f }, false);
    colliderOf(layerA)->setLayer(1);
    colliderOf(layerA)->setMask(1u << 1);
    colliderOf(layerB)->setLayer(2);
    colliderOf(layerB)->setMask(1u << 2);
    colliderOf(layerPeer)->setMask(0xFFFFFFFFu);

    const auto parent = addBody(sweep, "Parent", { -3.0f, 10.0f, 0.0f }, true);
    const auto child = fixtures::addChildObject(sweep.scene, "Child", parent);
    fixtures::addBoxCollider(child);
    fixtures::addRigidBody(child);
    sweep.objects.push_back(child);
  }

  // The full scan: every pair with a dynamic side, through the same filters the sweep applies.
  std::vector<CollisionPair> referencePairs(const SweepScene& sweep)
  {
    std::vector<CollisionPair> pairs;
    const auto& objects = sweep.objects;

    for (size_t i = 0; i < objects.size(); ++i)
    {
      for (size_t j = i + 1; j < objects.size(); ++j)
      {
        const auto& a = objects[i];
        const auto& b = objects[j];
        const bool aDynamic = rigidBodyOf(a) != nullptr;
        const bool bDynamic = rigidBodyOf(b) != nullptr;

        if (!aDynamic && !bDynamic)
        {
          continue;
        }

        if (a->getParent() == b || b->getParent() == a)
        {
          continue;
        }

        const auto colliderA = colliderOf(a);
        const auto colliderB = colliderOf(b);

        const bool layersMatch = (colliderA->getMask() & (1u << colliderB->getLayer())) != 0u &&
                                 (colliderB->getMask() & (1u << colliderA->getLayer())) != 0u;
        if (!layersMatch)
        {
          continue;
        }

        const auto boxA = colliderA->getBoundingBox();
        const auto boxB = colliderB->getBoundingBox();
        if (boxA.maxX < boxB.minX || boxA.minX > boxB.maxX ||
            boxA.maxY < boxB.minY || boxA.minY > boxB.maxY ||
            boxA.maxZ < boxB.minZ || boxA.minZ > boxB.maxZ)
        {
          continue;
        }

        const bool hit = aDynamic ? collisions::intersects(*colliderA, *colliderB)
                                  : collisions::intersects(*colliderB, *colliderA);
        if (hit)
        {
          pairs.push_back(CollisionPair::make(a->getUUID(), b->getUUID()));
        }
      }
    }

    std::ranges::sort(pairs);
    return pairs;
  }

  std::vector<CollisionPair> reportedPairs(const CollisionSystem& collisionSystem)
  {
    std::vector<CollisionPair> pairs;
    std::ranges::set_union(collisionSystem.getCollisionEnters(), collisionSystem.getCollisionStays(),
                           std::back_inserter(pairs));
    return pairs;
  }

  bool dynamicDynamicPair(const SweepScene& sweep, const CollisionPair& pair)
  {
    bool both = true;
    for (const auto& object : sweep.objects)
    {
      if ((object->getUUID() == pair.a || object->getUUID() == pair.b) && !rigidBodyOf(object))
      {
        both = false;
      }
    }

    return both;
  }
}

TEST(SweepEquivalence, ReportsTheSamePairsAsAFullScanEveryTick)
{
  SweepScene sweep;
  populate(sweep);

  CollisionSystem collisionSystem;
  size_t dynamicDynamic = 0;
  size_t dynamicStatic = 0;

  for (int tick = 0; tick < 40; ++tick)
  {
    PhysicsSystem::fixedUpdate(*sweep.scene.objectManager, dt);

    const auto expected = referencePairs(sweep);
    collisionSystem.fixedUpdate(*sweep.scene.objectManager, dt);

    EXPECT_EQ(reportedPairs(collisionSystem), expected) << "tick " << tick;

    for (const auto& pair : expected)
    {
      (dynamicDynamicPair(sweep, pair) ? dynamicDynamic : dynamicStatic)++;
    }
  }

  EXPECT_GT(dynamicDynamic, 0u);
  EXPECT_GT(dynamicStatic, 0u);
}

TEST(SweepEquivalence, SteppingTwoIdenticalScenesGivesBitIdenticalTransforms)
{
  SweepScene first;
  SweepScene second;
  populate(first);
  populate(second);

  CollisionSystem firstSystem;
  CollisionSystem secondSystem;

  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*first.scene.objectManager, dt);
    firstSystem.fixedUpdate(*first.scene.objectManager, dt);

    PhysicsSystem::fixedUpdate(*second.scene.objectManager, dt);
    secondSystem.fixedUpdate(*second.scene.objectManager, dt);
  }

  ASSERT_EQ(first.objects.size(), second.objects.size());
  for (size_t i = 0; i < first.objects.size(); ++i)
  {
    const auto a = fixtures::transformOf(first.objects[i]);
    const auto b = fixtures::transformOf(second.objects[i]);

    EXPECT_EQ(a->getPosition(), b->getPosition()) << first.objects[i]->getName();
    EXPECT_EQ(a->getRotation(), b->getRotation()) << first.objects[i]->getName();
  }
}

TEST(SweepEquivalence, ADynamicBodyStartingRightOfAStaticColliderStillCollidesWithIt)
{
  SweepScene sweep;
  const auto wall = addBody(sweep, "Wall", { 0, 0, 0 }, false);
  const auto mover = addBody(sweep, "Mover", { 1.0f, 0, 0 }, true);
  colliderOf(wall)->setIsTrigger(true);

  // The static collider sorts first, so only its own forward scan can find the dynamic body behind it.
  ASSERT_LT(colliderOf(wall)->getBoundingBox().minX, colliderOf(mover)->getBoundingBox().minX);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*sweep.scene.objectManager, dt);

  EXPECT_EQ(collisionSystem.getCollisionEnters(),
            std::vector{ CollisionPair::make(wall->getUUID(), mover->getUUID()) });

  // Positive control: the same pair with the dynamic body sorting first.
  fixtures::transformOf(mover)->setPosition({ -1.0f, 0, 0 });
  ASSERT_GT(colliderOf(wall)->getBoundingBox().minX, colliderOf(mover)->getBoundingBox().minX);

  collisionSystem.reset();
  collisionSystem.fixedUpdate(*sweep.scene.objectManager, dt);
  EXPECT_EQ(collisionSystem.getCollisionEnters(),
            std::vector{ CollisionPair::make(wall->getUUID(), mover->getUUID()) });
}

TEST(SweepEquivalence, BothBodiesOfADynamicPairAreRespondedTo)
{
  SweepScene sweep;
  const auto left = addBody(sweep, "Left", { 0, 0, 0 }, true);
  const auto right = addBody(sweep, "Right", { 0.5f, 0, 0 }, true);
  rigidBodyOf(left)->setDoGravity(false);
  rigidBodyOf(right)->setDoGravity(false);

  CollisionSystem collisionSystem;
  collisionSystem.fixedUpdate(*sweep.scene.objectManager, dt);

  EXPECT_EQ(collisionSystem.getCollisionEnters(),
            std::vector{ CollisionPair::make(left->getUUID(), right->getUUID()) });

  // Each side's hit list holds the other, so each is pushed out of the overlap.
  EXPECT_LT(positionOf(left).x, 0.0f);
  EXPECT_GT(positionOf(right).x, 0.5f);
}
