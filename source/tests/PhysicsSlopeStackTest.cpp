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
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;

  // Each box above the first sinks about one tick of gravity into the one below it, since the upper contact is
  // resolved before the lower box is pushed back up. A box settles within this much of that per level.
  constexpr float sinkPerLevel = 0.15f;

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
        PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
        collisions.fixedUpdate(*scene.objectManager, dt);
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
  SlopeStack stack(count, degrees);

  stack.run(100);
  const auto settled = stack.positions();
  stack.run(100);

  for (size_t level = 0; level < settled.size(); ++level)
  {
    SCOPED_TRACE(testing::Message() << "level " << level);

    EXPECT_LT(glm::distance(stack.positionOf(level), settled[level]), 1e-3f);
    EXPECT_LT(glm::distance(stack.positionOf(level), stack.start[level]),
              sinkPerLevel * static_cast<float>(level) + 0.02f);
    EXPECT_LT(glm::length(stack.bodies[level]->getVelocity()), 1e-3f);

    if (level > 0)
    {
      EXPECT_LT(stack.overlapWithBelow(level), sinkPerLevel);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(PhysicsSlopeStack, StackOnSlope,
                         testing::Values(std::pair{ 2, 15.0f }, std::pair{ 3, 15.0f }, std::pair{ 4, 15.0f },
                                         std::pair{ 2, 10.0f }, std::pair{ 3, 20.0f },
                                         std::pair{ 2, 0.0f }, std::pair{ 3, 0.0f }));

TEST(PhysicsSlopeStack, ATwoBoxStackHoldsWhateverTheMassesInIt)
{
  for (const auto& [lowerMass, upperMass] : { std::pair{ 10.0f, 100.0f }, std::pair{ 100.0f, 10.0f },
                                              std::pair{ 1.0f, 500.0f }, std::pair{ 500.0f, 1.0f } })
  {
    SCOPED_TRACE(testing::Message() << "lower mass " << lowerMass << ", upper mass " << upperMass);

    SlopeStack stack(2, 15.0f);
    stack.bodies[0]->setMass(lowerMass);
    stack.bodies[1]->setMass(upperMass);

    stack.run(100);
    const auto settled = stack.positions();
    stack.run(100);

    EXPECT_LT(glm::distance(stack.positionOf(0), stack.start[0]), 0.02f);
    EXPECT_LT(glm::distance(stack.positionOf(1), stack.start[1]), sinkPerLevel + 0.02f);
    EXPECT_LT(glm::distance(stack.positionOf(0), settled[0]), 1e-3f);
    EXPECT_LT(glm::distance(stack.positionOf(1), settled[1]), 1e-3f);
    EXPECT_LT(stack.overlapWithBelow(1), sinkPerLevel);
  }
}

TEST(PhysicsSlopeStack, AStackAboveTheFrictionAngleSlides)
{
  for (const int count : { 2, 3 })
  {
    SCOPED_TRACE(testing::Message() << count << " boxes");

    // Positive control: the same stack holds on a gentler slope, so the slide is the slope's doing.
    SlopeStack holds(count, 15.0f);
    holds.run(40);
    SlopeStack slides(count, 40.0f);
    slides.run(40);

    for (size_t level = 0; level < static_cast<size_t>(count); ++level)
    {
      SCOPED_TRACE(testing::Message() << "level " << level);

      EXPECT_LT(holds.slidDownhill(level), 0.3f);
      EXPECT_GT(slides.slidDownhill(level), 2.0f);
    }
  }
}

// TEMPORARY diagnostic
#include <iostream>
TEST(PhysicsSlopeStack, DiagnosticTrace)
{
  SlopeStack stack(2, 15.0f);
  PhysicsSystem::trace = true;
  for (int tick = 0; tick < 30; ++tick)
  {
    std::cout << "TICK " << tick << "\n";
    stack.run(1);
    for (size_t i = 0; i < stack.boxes.size(); ++i)
    {
      const auto t = fixtures::transformOf(stack.boxes[i]);
      const auto p = t->getPosition();
      const auto v = stack.bodies[i]->getVelocity();
      const auto a = stack.bodies[i]->getAngularVelocity();
      const auto r = t->getRotation();
      const auto s = stack.bodies[i]->getSupportNormal();
      const auto h = stack.bodies[i]->getHeldImpulse();
      std::cout << "  B" << i << " pos=(" << p.x << "," << p.y << "," << p.z << ") vel=(" << v.x << "," << v.y
                << ") ang=(" << a.x << "," << a.y << "," << a.z << ") rot=(" << r.x << "," << r.y << "," << r.z
                << ") falling=" << stack.bodies[i]->isFalling() << " supN=(" << s.x << "," << s.y << ") stacked="
                << stack.bodies[i]->getStackedLoad() << " held=(" << h.x << "," << h.y << ")\n";
    }
    std::cout << "  overlap=" << stack.overlapWithBelow(1) << "\n";
  }
  PhysicsSystem::trace = false;
  ADD_FAILURE() << "diagnostic";
}
