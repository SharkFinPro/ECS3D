#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::transformOf;

  // A thin domino, 0.3 by 2 by 1, so a push high on it has a long lever and a short base to pivot on.
  const glm::vec3 dominoScale{ 0.3f, 2.0f, 1.0f };

  // How far the box's up axis leans from the world's, in degrees.
  float tiltOf(const std::shared_ptr<Object>& object)
  {
    const float up = localUpOf(transformOf(object)->getRotation()).y;

    return glm::degrees(std::acos(std::clamp(up, -1.0f, 1.0f)));
  }

  struct Dominoes {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::vector<std::shared_ptr<Object>> objects;
    std::vector<std::shared_ptr<RigidBody>> bodies;

    // The ground's top is the plane y = 0.
    Dominoes()
    {
      const auto ground = addObject(scene, "Ground", { 3, -0.5f, 0 }, { 40, 1, 10 });
      fixtures::addBoxCollider(ground);
    }

    void addDomino(const float x, const float y)
    {
      const auto domino = addObject(scene, "Domino" + std::to_string(objects.size()), { x, y, 0 }, dominoScale);
      fixtures::addBoxCollider(domino);
      objects.push_back(domino);
      bodies.push_back(addBody(domino, true));
    }

    void integrate()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    }

    void resolve()
    {
      collisions.fixedUpdate(*scene.objectManager, dt);
    }

    void tick()
    {
      integrate();
      resolve();
    }

    void tick(const int count)
    {
      for (int i = 0; i < count; ++i)
      {
        tick();
      }
    }

    // Lands between a tick's integration and its contacts, as a push from a body resolved earlier in the tick does.
    void pushHigh(const size_t index, const glm::vec3& impulse)
    {
      integrate();

      const auto position = transformOf(objects[index])->getPosition() + glm::vec3(0, 0.9f, 0);
      PhysicsSystem::applyImpulse(*bodies[index], *transformOf(objects[index]), impulse, position, dt);

      resolve();
    }
  };
}

TEST(PhysicsTipping, APushHighOnARestingBoxTipsItOverItsFarBottomEdge)
{
  Dominoes dominoes;
  dominoes.addDomino(0, 1);
  dominoes.tick(30);
  ASSERT_LT(tiltOf(dominoes.objects[0]), 0.1f);

  // About three times the push that just tips it, so the margin is wide. Before the spin of a push high on a
  // resting box was kept, the corners it drives into the ground shed it and the box only slid.
  dominoes.pushHigh(0, { 1.0f, 0, 0 });
  dominoes.tick(40);

  EXPECT_GT(tiltOf(dominoes.objects[0]), 45.0f);
  EXPECT_LT(transformOf(dominoes.objects[0])->getPosition().y, 0.5f);
  EXPECT_GT(transformOf(dominoes.objects[0])->getPosition().x, 0.0f);
}

TEST(PhysicsTipping, ASmallPushLeavesARestingBoxFlush)
{
  Dominoes dominoes;
  dominoes.addDomino(0, 1);
  dominoes.tick(30);

  // The spin of this push turns the box less than the flush window in a tick, so it is laid flush as before.
  dominoes.pushHigh(0, { 0.04f, 0, 0 });

  float mostTilted = tiltOf(dominoes.objects[0]);
  for (int i = 0; i < 40; ++i)
  {
    dominoes.tick();
    mostTilted = std::max(mostTilted, tiltOf(dominoes.objects[0]));
  }

  EXPECT_LT(mostTilted, 1.0f);
  EXPECT_NEAR(transformOf(dominoes.objects[0])->getPosition().y, 1.0f, 0.05f);
  EXPECT_GE(transformOf(dominoes.objects[0])->getPosition().x, 0.0f);
}

TEST(PhysicsTipping, APushTooSmallToTipABoxRocksItAndItComesBackFlush)
{
  Dominoes dominoes;
  dominoes.addDomino(0, 1);
  dominoes.tick(30);

  // Turns the box past the flush window in the first tick, but is far under the push that carries its center of
  // mass over the far edge: positive control that the box is tipped by enough of a push and no more.
  dominoes.pushHigh(0, { 0.1f, 0, 0 });

  float mostTilted = 0.0f;
  for (int i = 0; i < 60; ++i)
  {
    dominoes.tick();
    mostTilted = std::max(mostTilted, tiltOf(dominoes.objects[0]));
  }

  EXPECT_GT(mostTilted, 1.0f);
  EXPECT_LT(mostTilted, 20.0f);
  EXPECT_LT(tiltOf(dominoes.objects[0]), 1.0f);
  EXPECT_NEAR(transformOf(dominoes.objects[0])->getPosition().y, 1.0f, 0.05f);
}

TEST(PhysicsTipping, AFallingDominoTipsTheNextOneAndTheRunPropagates)
{
  Dominoes dominoes;

  // The first leans 15 degrees toward the others, past the point where its center of mass is over its edge, so it
  // falls onto the second. Each is 1.2 from the last, with 0.9 between their faces.
  dominoes.addDomino(0, 1.01f);
  transformOf(dominoes.objects[0])->setRotation({ 0, 0, -15 });
  constexpr int count = 5;
  for (int i = 1; i < count; ++i)
  {
    dominoes.addDomino(1.2f * static_cast<float>(i), 1);
  }

  dominoes.tick(600);

  EXPECT_GT(tiltOf(dominoes.objects[count - 1]), 45.0f);
  EXPECT_GT(transformOf(dominoes.objects[count - 1])->getPosition().x, 1.2f * (count - 1));
}
