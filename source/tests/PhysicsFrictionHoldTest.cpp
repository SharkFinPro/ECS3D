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
#include <cmath>
#include <memory>
#include <utility>

namespace {
  using namespace physicsFixtures;

  constexpr int ticks = 200;

  // A body resting flush on a static box tilted about z, its center one half-extent of each box up the slope normal.
  struct Slope {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<Object> body;
    std::shared_ptr<RigidBody> rigidBody;
    CollisionSystem collisions;
    glm::vec3 start;
    glm::vec3 normal;
    glm::vec3 downhill;

    explicit Slope(const float degrees, const bool sphere = false)
    {
      const float angle = glm::radians(degrees);
      normal = { -std::sin(angle), std::cos(angle), 0.0f };
      downhill = { -std::cos(angle), -std::sin(angle), 0.0f };

      const auto support = fixtures::addObject(scene, "Slope", { 0, 0, 0 });
      fixtures::transformOf(support)->setScale({ 40, 1, 10 });
      fixtures::transformOf(support)->setRotation({ 0, 0, degrees });
      fixtures::addBoxCollider(support);

      start = normal * (sphere ? 1.5f : 2.0f);
      body = fixtures::addObject(scene, "Body", start);
      if (sphere)
      {
        fixtures::addSphereCollider(body, 0.5f);
      }
      else
      {
        fixtures::transformOf(body)->setRotation({ 0, 0, degrees });
        fixtures::addBoxCollider(body);
      }

      rigidBody = addBody(body, true);
      rigidBody->setFriction(0.5f);
    }

    void run(const int count, const float step)
    {
      for (int tick = 0; tick < count; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, step);
        collisions.fixedUpdate(*scene.objectManager, step);
      }
    }

    [[nodiscard]] float slidDistance() const
    {
      return glm::length(fixtures::transformOf(body)->getPosition() - start);
    }
  };
}

class FrictionHoldOnSlope : public testing::TestWithParam<std::pair<float, float>> {};

TEST_P(FrictionHoldOnSlope, ABoxBelowTheFrictionAngleStaysPut)
{
  const auto [degrees, step] = GetParam();
  Slope slope(degrees);
  slope.run(ticks, step);

  EXPECT_LT(slope.slidDistance(), 1e-3f);
  EXPECT_LT(glm::length(slope.rigidBody->getVelocity()), 1e-4f);
}

INSTANTIATE_TEST_SUITE_P(PhysicsFriction, FrictionHoldOnSlope,
                         testing::Values(std::pair{ 5.71059f, 0.1f }, std::pair{ 5.71059f, 0.02f },
                                         std::pair{ 15.0f, 0.1f }, std::pair{ 15.0f, 0.02f }));

TEST(PhysicsFriction, ABoxAboveTheFrictionAngleSlidesAndSpeedsUp)
{
  Slope slope(40.0f);
  slope.run(20, 0.1f);
  const float early = slope.slidDistance();
  const auto earlySpeed = glm::length(slope.rigidBody->getVelocity());
  slope.run(20, 0.1f);

  EXPECT_GT(slope.slidDistance(), early + 0.5f);
  EXPECT_GT(glm::length(slope.rigidBody->getVelocity()), earlySpeed);

  // Kinetic friction takes mu cos from gravity's sin along the slope; the engine's gravity is g * 0.1 per tick.
  const float angle = glm::radians(40.0f);
  const float expected = -gravityPerTick * (std::sin(angle) - 0.5f * std::cos(angle));
  const float gained = glm::length(slope.rigidBody->getVelocity()) - earlySpeed;
  EXPECT_NEAR(gained / 20.0f, expected, 0.25f * expected);
}

TEST(PhysicsFriction, ASphereOnASlopeRollsDownhill)
{
  Slope slope(5.71059f, true);
  slope.run(60, 0.1f);

  EXPECT_GT(slope.slidDistance(), 0.5f);
  EXPECT_GT(glm::dot(fixtures::transformOf(slope.body)->getPosition() - slope.start, slope.downhill), 0.5f);
}

TEST(PhysicsFriction, TheFrictionAngleSeparatesHoldingFromSliding)
{
  Slope holds(25.0f);
  holds.run(ticks, 0.1f);
  Slope slides(28.0f);
  slides.run(ticks, 0.1f);

  EXPECT_LT(holds.slidDistance(), 1e-3f);
  EXPECT_GT(slides.slidDistance(), 1.0f);
}

TEST(PhysicsFriction, ABoxSlidingUpTheSlopeStopsAndStaysPut)
{
  Slope slope(15.0f);
  slope.rigidBody->setVelocity(-slope.downhill * 0.3f);
  slope.run(60, 0.1f);
  const auto stopped = fixtures::transformOf(slope.body)->getPosition();
  slope.run(ticks, 0.1f);

  EXPECT_GT(glm::dot(stopped - slope.start, -slope.downhill), 0.3f);
  EXPECT_LT(glm::distance(fixtures::transformOf(slope.body)->getPosition(), stopped), 1e-3f);
}

// Measured with the hold skipped for rolling bodies and the sphere's radius as its rolling arm; a hold that took the
// rolling slide back would lag it.
TEST(PhysicsFriction, ASphereRollsTheSameDistanceOnASlope)
{
  Slope slope(5.71059f, true);
  slope.run(60, 0.1f);

  EXPECT_NEAR(slope.slidDistance(), 3.779873f, 1e-3f);
}

// A push below what friction holds adds to the slide gravity gives, and the hold takes both back.
TEST(PhysicsFriction, APushedBoxBelowTheFrictionLimitStaysPut)
{
  Slope slope(15.0f);
  for (int tick = 0; tick < ticks; ++tick)
  {
    slope.rigidBody->addPendingForce(slope.downhill * 0.15f, fixtures::transformOf(slope.body)->getPosition(),
                                     ForceMode::acceleration);
    slope.run(1, 0.1f);
  }

  EXPECT_LT(slope.slidDistance(), 1e-3f);
}
