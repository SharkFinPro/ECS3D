#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::transformOf;

  // The server's tick length, besides the suites' usual one.
  constexpr float serverTick = 1.0f / 50.0f;

  // A thin domino, 0.3 by 2 by 1 (a box collider spans twice the scale), so a push high on it has a long lever and
  // a short base to pivot on.
  const glm::vec3 dominoScale{ 0.15f, 1.0f, 0.5f };

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
    float tickLength;

    // The ground's top is the plane y = 0.
    explicit Dominoes(const float step = dt) : tickLength(step)
    {
      const auto ground = addObject(scene, "Ground", { 3, -1, 0 }, { 40, 1, 10 });
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
      PhysicsSystem::fixedUpdate(*scene.objectManager, tickLength);
    }

    void resolve()
    {
      collisions.fixedUpdate(*scene.objectManager, tickLength);
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

    // Rests the first domino and checks it did, so a wrong setup fails here.
    void settle()
    {
      tick(30);
      ASSERT_LT(tiltOf(objects[0]), 0.1f);
      ASSERT_NEAR(transformOf(objects[0])->getPosition().y, 1.0f, 0.05f);
      ASSERT_FALSE(bodies[0]->isFalling());
    }

    // Lands between a tick's integration and its contacts, as a push from a body resolved earlier in the tick does.
    void pushHigh(const size_t index, const glm::vec3& impulse)
    {
      integrate();

      const auto position = transformOf(objects[index])->getPosition() + glm::vec3(0, 0.9f, 0);
      PhysicsSystem::applyImpulse(*bodies[index], *transformOf(objects[index]), impulse, position, tickLength);

      resolve();
    }
  };

  // About three times the push that just tips it, so the margin is wide. Before the spin of a push high on a
  // resting box was kept, the corners it drives into the ground shed it and the box only slid.
  void expectHardPushTips(const float step)
  {
    Dominoes dominoes(step);
    dominoes.addDomino(0, 1);
    dominoes.settle();

    dominoes.pushHigh(0, { 1.0f, 0, 0 });
    dominoes.tick(150);

    EXPECT_GT(tiltOf(dominoes.objects[0]), 45.0f);
    EXPECT_LT(transformOf(dominoes.objects[0])->getPosition().y, 0.5f);
    EXPECT_GT(transformOf(dominoes.objects[0])->getPosition().x, 0.0f);
  }

  // Turns the box past the flush window in the first tick, but is far under the push that carries its center of
  // mass over the far edge: positive control that the box is tipped by enough of a push and no more.
  void expectSmallPushRocksAndSettles(const float step)
  {
    Dominoes dominoes(step);
    dominoes.addDomino(0, 1);
    dominoes.settle();

    dominoes.pushHigh(0, { 0.1f, 0, 0 });

    float mostTilted = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
      dominoes.tick();
      mostTilted = std::max(mostTilted, tiltOf(dominoes.objects[0]));
    }

    EXPECT_GT(mostTilted, 1.0f);
    EXPECT_LT(mostTilted, 20.0f);
    EXPECT_LT(tiltOf(dominoes.objects[0]), 1.0f);
    EXPECT_NEAR(transformOf(dominoes.objects[0])->getPosition().y, 1.0f, 0.05f);
  }

  // The resting body's velocity and spin after a hitter meets it sideways, with the contact normal tilted by tilt.
  std::pair<glm::vec3, glm::vec3> restingBodyAfterSideHit(const float tilt)
  {
    const auto scene = fixtures::makeScene();

    const auto resting = addObject(scene, "Resting", { 2, 0, 0 });
    const auto restingBody = addBody(resting, false);
    const auto hitter = addObject(scene, "Hitter", { 0, 0, 0 });
    const auto hitterBody = addBody(hitter, false);

    restingBody->setFalling(false);
    hitterBody->setVelocity({ 1, 0, 0 });

    PhysicsSystem::handleCollision(*hitterBody, ownColliderOf(*hitterBody), resting, { -0.1f, tilt, 0 },
                                   glm::vec3{ 1, 0.8f, 0 }, dt);

    return { restingBody->getVelocity(), restingBody->getAngularVelocity() };
  }

  // A box spinning about a horizontal axis on a flat manifold, whose falling flag is already cleared.
  glm::vec3 spinAfterFlatContact(const bool restedOnThisPlane)
  {
    const auto scene = fixtures::makeScene();
    const auto box = addObject(scene, "Box", { 0, 0, 0 });
    const auto body = addBody(box, false);
    const auto ground = addObject(scene, "Ground", { 0, -1, 0 });

    body->setFalling(false);
    if (restedOnThisPlane)
    {
      body->setSupportNormal({ 0, 1, 0 });
    }
    body->setAngularVelocity({ 180, 0, 0 });
    PhysicsSystem::handleCollision(*body, ownColliderOf(*body), ground, { 0, 0.01f, 0 }, flatUnderside, dt);

    return body->getAngularVelocity();
  }
}

TEST(PhysicsTipping, APushHighOnARestingBoxTipsItOverItsFarBottomEdge)
{
  expectHardPushTips(dt);
}

TEST(PhysicsTipping, APushHighOnARestingBoxTipsItAtTheServersTick)
{
  expectHardPushTips(serverTick);
}

TEST(PhysicsTipping, ASmallPushLeavesARestingBoxFlush)
{
  Dominoes dominoes;
  dominoes.addDomino(0, 1);
  dominoes.settle();

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
  EXPECT_GT(transformOf(dominoes.objects[0])->getPosition().x, -1e-3f);
}

TEST(PhysicsTipping, APushTooSmallToTipABoxRocksItAndItComesBackFlush)
{
  expectSmallPushRocksAndSettles(dt);
}

TEST(PhysicsTipping, APushTooSmallToTipABoxRocksItAndItComesBackFlushAtTheServersTick)
{
  expectSmallPushRocksAndSettles(serverTick);
}

TEST(PhysicsTipping, AFallingDominoTipsTheNextOneOver)
{
  Dominoes dominoes(serverTick);

  // The first leans 15 degrees toward the second, past the point where its center of mass is over its edge, so it
  // falls onto it. Their faces are 0.9 apart.
  dominoes.addDomino(0, 1.01f);
  transformOf(dominoes.objects[0])->setRotation({ 0, 0, -15 });
  dominoes.addDomino(1.2f, 1);

  dominoes.tick(600);

  EXPECT_GT(tiltOf(dominoes.objects[0]), 45.0f);
  EXPECT_GT(tiltOf(dominoes.objects[1]), 45.0f);
  EXPECT_GT(transformOf(dominoes.objects[1])->getPosition().x, 1.2f);
}

TEST(PhysicsTipping, ASideContactIsTheSameWhicheverWayRoundingTiltsItsNormal)
{
  const auto [upVelocity, upSpin] = restingBodyAfterSideHit(1e-6f);
  const auto [downVelocity, downSpin] = restingBodyAfterSideHit(-1e-6f);

  // Positive control: the hit does move and turn the resting body.
  EXPECT_GT(upVelocity.x, 0.01f);
  EXPECT_GT(glm::length(upSpin), 1.0f);

  // The two normals differ by 2e-6, so the responses may differ by about as much relatively; the sign-dependent
  // bug this guards against gave a spin of zero on one side.
  fixtures::expectNear("velocity", downVelocity, upVelocity, 1e-4f * glm::length(upVelocity));
  fixtures::expectNear("spin", downSpin, upSpin, 1e-4f * glm::length(upSpin));
}

TEST(PhysicsTipping, ASpinningBoxThatDidNotRestOnThisPlaneLastTickStillShedsIt)
{
  // Falling is cleared by any small upward push, a side contact's included, so it alone does not say that the body
  // rested here.
  fixtures::expectNear("not resting here", spinAfterFlatContact(false), { 0, 0, 0 }, 1e-4f);

  // Positive control: the same spin on a body that rested on this plane is kept.
  fixtures::expectNear("resting here", spinAfterFlatContact(true), { 180, 0, 0 }, 1e-3f);
}
