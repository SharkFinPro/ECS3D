#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;

  // The stacks run at the server's tick: at dt 0.1 a tick of gravity is five times as much, and a stack sunk that far
  // into itself near its friction angle slides off.
  constexpr float serverTick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;
  constexpr float gravityStep = -gravityPerTick * (serverTick * serverTick) / (dt * dt);

  // Each box above the first sinks about one tick of gravity into the one below it, since the upper contact is
  // resolved before the lower box is pushed back up. A box settles within twice that per level (measured, about one
  // to 1.4 ticks per level).
  constexpr float sinkPerLevel = 2.0f * gravityStep;

  // What a box that settles in place may still move from its start, or be moving by, per tick: a tenth and a hundredth
  // of a tick of gravity, against measured values of at most 0.03 and 3e-7 of one.
  constexpr float restSlack = 0.1f * gravityStep;
  constexpr float restSpeed = 0.01f * gravityStep;

  // Near the friction angle a settled stack creeps by float noise that differs between platforms, so the
  // distance from the start gets this much extra there. A slide still fails the settled and velocity checks.
  constexpr float steepSlope = 20.0f;
  constexpr float steepSlack = 0.25f * gravityStep;

  // Boxes stacked on a static box tilted about z, each turned like it and resting flush on the one below.
  struct SlopeStack {
    fixtures::Scene scene = fixtures::makeScene();
    std::vector<std::shared_ptr<Object>> boxes;
    std::vector<std::shared_ptr<RigidBody>> bodies;
    std::vector<glm::vec3> start;
    CollisionSystem collisions;
    glm::vec3 normal;
    glm::vec3 downhill;

    SlopeStack(const int count, const float degrees)
    {
      const float angle = glm::radians(degrees);
      normal = { -std::sin(angle), std::cos(angle), 0.0f };
      downhill = { -std::cos(angle), -std::sin(angle), 0.0f };

      const auto support = fixtures::addObject(scene, "Slope", { 0, 0, 0 });
      fixtures::transformOf(support)->setScale({ 40, 1, 10 });
      fixtures::transformOf(support)->setRotation({ 0, 0, degrees });
      fixtures::addBoxCollider(support);

      for (int level = 0; level < count; ++level)
      {
        const auto position = normal * (2.0f + 2.0f * static_cast<float>(level));
        const auto box = fixtures::addObject(scene, "Box" + std::to_string(level), position);
        fixtures::transformOf(box)->setRotation({ 0, 0, degrees });
        fixtures::addBoxCollider(box);

        const auto body = addBody(box, true);
        body->setFriction(0.5f);

        boxes.push_back(box);
        bodies.push_back(body);
        start.push_back(position);
      }
    }

    void run(const int count)
    {
      for (int tick = 0; tick < count; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, serverTick);
        collisions.fixedUpdate(*scene.objectManager, serverTick);
      }
    }

    [[nodiscard]] glm::vec3 positionOf(const size_t level) const
    {
      return fixtures::transformOf(boxes[level])->getPosition();
    }

    [[nodiscard]] std::vector<glm::vec3> positions() const
    {
      std::vector<glm::vec3> all;
      for (size_t level = 0; level < boxes.size(); ++level)
      {
        all.push_back(positionOf(level));
      }

      return all;
    }

    // How far the box sits inside the one below it, along the slope's normal.
    [[nodiscard]] float overlapWithBelow(const size_t level) const
    {
      return 2.0f - glm::dot(positionOf(level) - positionOf(level - 1), normal);
    }

    [[nodiscard]] float slidDownhill(const size_t level) const
    {
      return glm::dot(positionOf(level) - start[level], downhill);
    }
  };
}

class StackOnSlope : public testing::TestWithParam<std::pair<int, float>> {};

TEST_P(StackOnSlope, BelowTheFrictionAngleStaysPut)
{
  const auto [count, degrees] = GetParam();
  const float slack = degrees > steepSlope ? steepSlack : 0.0f;
  SlopeStack stack(count, degrees);

  stack.run(10 * ticksPerSecond);
  const auto settled = stack.positions();
  stack.run(10 * ticksPerSecond);

  for (size_t level = 0; level < settled.size(); ++level)
  {
    SCOPED_TRACE(testing::Message() << "level " << level);

    EXPECT_LT(glm::distance(stack.positionOf(level), settled[level]), 1e-3f);
    EXPECT_LT(glm::distance(stack.positionOf(level), stack.start[level]),
              sinkPerLevel * static_cast<float>(level) + restSlack + slack);
    EXPECT_LT(glm::length(stack.bodies[level]->getVelocity()), restSpeed);

    if (level > 0)
    {
      EXPECT_LT(stack.overlapWithBelow(level), sinkPerLevel);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(PhysicsSlopeStack, StackOnSlope,
                         testing::Values(std::pair{ 2, 15.0f }, std::pair{ 3, 15.0f }, std::pair{ 4, 15.0f },
                                         std::pair{ 2, 10.0f }, std::pair{ 3, 20.0f },
                                         std::pair{ 2, 0.0f }, std::pair{ 3, 0.0f },
                                         // Friction 0.5 holds up to about 26.6 degrees. Closer to it, a settled stack's
                                         // creep depends on the platform's float rounding, and the slack allows for it.
                                         std::pair{ 2, 22.0f }, std::pair{ 3, 22.0f },
                                         std::pair{ 2, 24.0f }, std::pair{ 3, 24.0f }, std::pair{ 3, 25.0f }));

TEST(PhysicsSlopeStack, ATwoBoxStackHoldsWhateverTheMassesInIt)
{
  for (const auto& [lowerMass, upperMass] : { std::pair{ 10.0f, 100.0f }, std::pair{ 100.0f, 10.0f },
                                              std::pair{ 1.0f, 500.0f }, std::pair{ 500.0f, 1.0f } })
  {
    SCOPED_TRACE(testing::Message() << "lower mass " << lowerMass << ", upper mass " << upperMass);

    SlopeStack stack(2, 15.0f);
    stack.bodies[0]->setMass(lowerMass);
    stack.bodies[1]->setMass(upperMass);

    stack.run(10 * ticksPerSecond);
    const auto settled = stack.positions();
    stack.run(10 * ticksPerSecond);

    EXPECT_LT(glm::distance(stack.positionOf(0), stack.start[0]), restSlack);
    EXPECT_LT(glm::distance(stack.positionOf(1), stack.start[1]), sinkPerLevel + restSlack);
    EXPECT_LT(glm::distance(stack.positionOf(0), settled[0]), 1e-3f);
    EXPECT_LT(glm::distance(stack.positionOf(1), settled[1]), 1e-3f);
    EXPECT_LT(stack.overlapWithBelow(1), sinkPerLevel);
  }
}

TEST(PhysicsSlopeStack, EachLevelPassesItsWholeLoadDownTheSlope)
{
  constexpr int count = 5;
  SlopeStack stack(count, 20.0f);
  stack.run(10 * ticksPerSecond);

  // Each box presses its support with a tick of its own weight along the normal, and the support passes what rests
  // on it on whole, so every level holds one box's load more than the level above it. Projected onto world up,
  // what a level passes on would shrink by the cosine of the slope at every level below it.
  const auto& body = *stack.bodies[0];
  const float perBox = body.getMass() * gravityStep * glm::dot(stack.normal, glm::vec3(0, 1, 0));

  // A top box that slid off would leave the level under it nothing to pass on.
  // Measured along the slope, so sinking into the boxes below does not count. The top box slips about 0.09 from its
  // start, and 0.03 along the box under it, before friction holds it. The slip follows from EPA's contact depths, the
  // same whichever equally furthest corner a box's support returns.
  constexpr float topBoxBound = 0.25f;
  constexpr float topBoxRelativeBound = 0.1f;
  EXPECT_LT(std::fabs(stack.slidDownhill(count - 1)), topBoxBound);
  EXPECT_LT(std::fabs(glm::dot(stack.positionOf(count - 1) - stack.positionOf(count - 2), stack.downhill)),
            topBoxRelativeBound);

  // Positive control: the bound is not vacuous, since the same stack on a steeper slope moves farther than it.
  SlopeStack sliding(count, 40.0f);
  sliding.run(10 * ticksPerSecond);
  EXPECT_GT(sliding.slidDownhill(count - 1), topBoxBound);

  for (const auto& box : stack.bodies)
  {
    EXPECT_LT(glm::length(box->getAngularVelocity()), 0.5f);
  }

  for (int tick = 0; tick < 10; ++tick)
  {
    stack.run(1);

    for (int level = 0; level + 1 < count; ++level)
    {
      SCOPED_TRACE(testing::Message() << "tick " << tick << ", level " << level);

      const float added = stack.bodies[level]->getStackedLoad() - stack.bodies[level + 1]->getStackedLoad();
      EXPECT_NEAR(added, perBox, 0.01f * perBox);
    }
  }
}

TEST(PhysicsSlopeStack, AStackAboveTheFrictionAngleSlides)
{
  for (const int count : { 2, 3 })
  {
    SCOPED_TRACE(testing::Message() << count << " boxes");

    // Positive control: the same stack holds on a gentler slope, so the slide is the slope's doing.
    SlopeStack holds(count, 15.0f);
    holds.run(4 * ticksPerSecond);
    SlopeStack slides(count, 40.0f);
    slides.run(4 * ticksPerSecond);
    SlopeStack justAbove(count, 34.0f);
    justAbove.run(4 * ticksPerSecond);

    for (size_t level = 0; level < static_cast<size_t>(count); ++level)
    {
      SCOPED_TRACE(testing::Message() << "level " << level);

      EXPECT_LT(holds.slidDownhill(level), 0.3f);
      EXPECT_GT(slides.slidDownhill(level), 2.0f);
      EXPECT_GT(justAbove.slidDownhill(level), 2.0f);
    }
  }
}
