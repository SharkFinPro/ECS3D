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
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
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

TEST(PhysicsTipping, ABoxReleasedLeaningPivotsOnItsBottomEdgeAndLandsOnItsSide)
{
  Dominoes dominoes(serverTick);
  dominoes.addDomino(0, 1.01f);
  const auto& domino = dominoes.objects[0];
  transformOf(domino)->setRotation({ 0, 0, -15 });

  const auto edgeOf = [&domino]
  {
    const auto& transform = *transformOf(domino);

    return transform.getPosition() + transform.getOrientation() * glm::vec3(0.15f, -1.0f, 0.0f);
  };
  const float startX = edgeOf().x;

  // The edge it stands on stays about where it was while the box swings through 45 degrees.
  float mostSlipped = 0.0f;
  int ticks = 0;
  while (tiltOf(domino) < 45.0f && ticks < 600)
  {
    dominoes.tick();
    mostSlipped = std::max(mostSlipped, std::fabs(edgeOf().x - startX));
    ++ticks;
  }

  EXPECT_LT(ticks, 600);
  EXPECT_LT(mostSlipped, 0.05f);

  // Turned all the way about the edge, its center ends a half height beyond it and a half thickness up.
  dominoes.tick(150);
  EXPECT_GT(tiltOf(domino), 85.0f);
  EXPECT_NEAR(transformOf(domino)->getPosition().x, startX + 1.0f, 0.15f);
  EXPECT_NEAR(transformOf(domino)->getPosition().y, 0.15f, 0.05f);
}

namespace {
  // A static ramp turned about z by slopeDegrees. Bodies are placed to lie flat on its top face, down to a
  // thousandth of overlap, as a spawner puts them.
  struct Ramp {
    fixtures::Scene scene;
    CollisionSystem collisions;
    glm::vec3 normal;
    static constexpr float slopeDegrees = 25.0f;
    static constexpr float halfThickness = 0.5f;

    Ramp()
    {
      const auto ramp = addObject(scene, "Ramp", { 0, 0, 0 }, { 10, halfThickness, 10 });
      transformOf(ramp)->setRotation({ 0, 0, slopeDegrees });
      fixtures::addBoxCollider(ramp);
      normal = transformOf(ramp)->getOrientation() * glm::vec3(0, 1, 0);
    }

    std::shared_ptr<Object> addBox(const glm::vec3& scale, const float friction)
    {
      const auto box = addObject(scene, "Box", normal * (halfThickness + scale.y - 0.001f), scale);
      transformOf(box)->setRotation({ 0, 0, slopeDegrees });
      fixtures::addBoxCollider(box);
      addBody(box, true)->setFriction(friction);

      return box;
    }

    void tick(const int count)
    {
      for (int i = 0; i < count; ++i)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
        collisions.fixedUpdate(*scene.objectManager, serverTick);
      }
    }

    // How far the box's up axis has turned from the slope's normal, in degrees.
    [[nodiscard]] float tiltFromSlope(const std::shared_ptr<Object>& box) const
    {
      const float up = glm::dot(transformOf(box)->getOrientation() * glm::vec3(0, 1, 0), normal);

      return glm::degrees(std::acos(std::clamp(up, -1.0f, 1.0f)));
    }
  };

  void expectNearBy(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    fixtures::expectNear(what, actual, expected, 1e-4f);
  }
}

TEST(PhysicsTipping, AYawedDominoLeaningOnTheNextTipsItOver)
{
  Dominoes dominoes(serverTick);
  dominoes.addDomino(0, 1.01f);
  dominoes.addDomino(0, 1);

  // The pair stands on the run's line, turned 30 degrees about y. The first leans 15 degrees along the run.
  const float yaw = glm::radians(30.0f);
  const glm::vec3 run{ std::cos(yaw), 0.0f, -std::sin(yaw) };
  const glm::quat turn = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
  transformOf(dominoes.objects[0])->setPosition(glm::vec3(0, 1.01f, 0));
  transformOf(dominoes.objects[0])->setWorldOrientation(turn * glm::angleAxis(glm::radians(-15.0f), glm::vec3(0, 0, 1)));
  transformOf(dominoes.objects[1])->setPosition(glm::vec3(0, 1, 0) + 1.2f * run);
  transformOf(dominoes.objects[1])->setWorldOrientation(turn);
  for (const auto& body : dominoes.bodies)
  {
    body->setMass(0.6f);
    body->setFriction(0.5f);
  }

  dominoes.tick(300);

  EXPECT_GT(tiltOf(dominoes.objects[0]), 45.0f);
  EXPECT_GT(tiltOf(dominoes.objects[1]), 45.0f);
}

TEST(PhysicsTipping, AYawedDominoPushedHighTipsOverItsFarBottomEdge)
{
  Dominoes dominoes(serverTick);
  dominoes.addDomino(0, 1);
  transformOf(dominoes.objects[0])->setRotation({ 0, 30, 0 });
  dominoes.settle();

  dominoes.pushHigh(0, { std::cos(glm::radians(30.0f)), 0, -std::sin(glm::radians(30.0f)) });
  dominoes.tick(150);

  EXPECT_GT(tiltOf(dominoes.objects[0]), 45.0f);
  EXPECT_LT(transformOf(dominoes.objects[0])->getPosition().y, 0.5f);
}

TEST(PhysicsTipping, AYawedDominoGivenOnlyASmallPushComesBackFlush)
{
  Dominoes dominoes(serverTick);
  dominoes.addDomino(0, 1);
  transformOf(dominoes.objects[0])->setRotation({ 0, 30, 0 });
  dominoes.settle();

  dominoes.pushHigh(0, { 0.1f * std::cos(glm::radians(30.0f)), 0, -0.1f * std::sin(glm::radians(30.0f)) });
  dominoes.tick(300);

  EXPECT_LT(tiltOf(dominoes.objects[0]), 1.0f);
  EXPECT_NEAR(transformOf(dominoes.objects[0])->getPosition().y, 1.0f, 0.05f);
}


TEST(PhysicsTipping, ATallBoxFallsOverOnASlopeSteeperThanItsTippingAngle)
{
  Ramp ramp;
  const auto tall = ramp.addBox({ 0.15f, 1.0f, 0.5f }, 1.0f);

  ASSERT_LT(ramp.tiltFromSlope(tall), 0.1f);

  ramp.tick(300);

  EXPECT_GT(ramp.tiltFromSlope(tall), 45.0f);
}

TEST(PhysicsTipping, ASquatBoxHoldsOnTheSameSlope)
{
  Ramp ramp;
  const auto cube = ramp.addBox({ 0.5f, 0.5f, 0.5f }, 1.0f);
  const auto start = fixtures::positionOf(cube);

  ramp.tick(300);

  EXPECT_LT(ramp.tiltFromSlope(cube), 2.0f);
  EXPECT_LT(glm::distance(fixtures::positionOf(cube), start), 0.1f);
}

TEST(PhysicsTipping, TheSupportPointIsWhereTheCenterOfMassDropsStraightDownOntoTheContactPlane)
{
  constexpr float angle = glm::radians(25.0f);
  const glm::vec3 normal{ -std::sin(angle), std::cos(angle), 0.0f };
  const glm::vec3 uphill{ std::cos(angle), std::sin(angle), 0.0f };
  const glm::vec3 across{ 0, 0, 1 };

  const std::array<glm::vec3, 4> square{
    -0.5f * uphill - 0.5f * across, 0.5f * uphill - 0.5f * across,
    0.5f * uphill + 0.5f * across, -0.5f * uphill + 0.5f * across
  };

  // Positive control: low over the square, it is over the square either way, so the support is straight under it
  // along the normal.
  const glm::vec3 low = 0.2f * uphill + 0.3f * normal + 0.1f * across;
  expectNearBy("low", PhysicsSystem::supportPoint(low, normal, square), 0.2f * uphill + 0.1f * across);

  // High above the square's center along the normal, its weight acts straight down, well downhill of the square.
  const glm::vec3 high = 3.0f * normal;
  expectNearBy("high", PhysicsSystem::supportPoint(high, normal, square), -0.5f * uphill);
}

TEST(PhysicsTipping, ASupportPointOnANearlyLevelWallStaysUnderTheCenterOfMass)
{
  // A wall leaning just past the side-contact window: dropped straight down, the center of mass would land tens of
  // units away and put the push on the manifold's edge.
  const glm::vec3 normal = glm::normalize(glm::vec3(1.0f, 0.02f, 0.0f));
  const glm::vec3 up = glm::normalize(glm::vec3(-0.02f, 1.0f, 0.0f));
  const glm::vec3 across{ 0, 0, 1 };

  const std::array<glm::vec3, 4> square{
    -0.5f * up - 0.5f * across, 0.5f * up - 0.5f * across, 0.5f * up + 0.5f * across, -0.5f * up + 0.5f * across
  };

  const glm::vec3 centerOfMass = 0.1f * up + 0.2f * across + 0.8f * normal;
  expectNearBy("wall", PhysicsSystem::supportPoint(centerOfMass, normal, square), 0.1f * up + 0.2f * across);
}

TEST(PhysicsTipping, ALeaningContactTurnsARestingBodyButAStackedOneOnlyPushesItDown)
{
  // 0.05 up for 0.1 across puts the normal about 27 degrees above level: a body leaning on its side, which is
  // not within 45 degrees of the resting body's support normal, so the hit lands off its center and turns it.
  const auto [leanVelocity, leanSpin] = restingBodyAfterSideHit(0.05f);
  EXPECT_GT(glm::length(leanSpin), 1.0f);
  EXPECT_GT(leanVelocity.x, 0.01f);

  // Positive control: 0.3 up for 0.1 across is within 18 degrees of straight down onto it, a body stacked on it,
  // pushed through its center and held by its support.
  const auto [stackedVelocity, stackedSpin] = restingBodyAfterSideHit(0.3f);
  EXPECT_LT(glm::length(stackedSpin), 1.0f);
}
