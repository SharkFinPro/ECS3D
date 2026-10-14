#include <gtest/gtest.h>

#include "PhysicsGenericRestingFixtures.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {
  using namespace restingFixtures;
  using fixtures::transformOf;

  constexpr int ticks = 300;
  constexpr float positionTolerance = 1e-4f;
  constexpr float rotationTolerance = 0.01f;

  struct Case {
    const char* name;
    Launch launch;
    // Called once before the tick it is given, on the body of each rig.
    int pushTick = -1;
  };

  const std::vector<Case> cases{
    { "tilted drop", { { 0, 5, 0 }, { 20, 0, 10 } } },
    { "corner drop", { { 0, 5, 0 }, { 35, 0, 45 } } },
    { "spinning drop", { { 0, 5, 0 }, { 10, 20, 5 }, { 0.1f, 0, 0 }, { 30, 60, 0 } } },
    { "flat drop", { { 0, 5, 0 } } },
    { "edge balance drop", { { 0, 5, 0 }, { 0, 0, 45 } } },
    { "slide", { { 0, 2.0f, 0 }, { 0, 0, 0 }, { 0.3f, 0, 0 } } },
    { "push", { { 0, 2.0f, 0 } }, 100 }
  };

  void push(Rig& rig)
  {
    const auto position = transformOf(rig.body)->getPosition();
    rig.rigidBody->addPendingForce({ 40, 0, 0 }, position + glm::vec3(0, 0.8f, 0), ForceMode::impulse);
  }

  void expectSameTrajectory(const Case& scenario, const Shape bodyShape, const Shape groundShape, const float dt)
  {
    Rig reference(Shape::box, Shape::box, scenario.launch);
    Rig other(bodyShape, groundShape, scenario.launch);

    for (int tick = 0; tick < ticks; ++tick)
    {
      if (tick == scenario.pushTick)
      {
        push(reference);
        push(other);
      }

      reference.step(dt);
      other.step(dt);

      const auto expected = transformOf(reference.body);
      const auto actual = transformOf(other.body);
      ASSERT_LT(glm::length(expected->getPosition() - actual->getPosition()), positionTolerance)
        << scenario.name << " dt " << dt << " tick " << tick;
      ASSERT_LT(glm::length(expected->getRotation() - actual->getRotation()), rotationTolerance)
        << scenario.name << " dt " << dt << " tick " << tick;
    }
  }

  void expectAllCasesMatch(const Shape bodyShape, const Shape groundShape)
  {
    for (const auto& scenario : cases)
    {
      for (const float dt : { 0.1f, 0.02f })
      {
        expectSameTrajectory(scenario, bodyShape, groundShape, dt);
      }
    }
  }
}

TEST(PhysicsGenericResting, UnitCubeHullFollowsTheBoxOntoABoxGround)
{
  expectAllCasesMatch(Shape::hull, Shape::box);
}

TEST(PhysicsGenericResting, BoxFollowsItselfOntoAUnitCubeHullGround)
{
  expectAllCasesMatch(Shape::box, Shape::hull);
}

TEST(PhysicsGenericResting, UnitCubeHullFollowsTheBoxOntoAUnitCubeHullGround)
{
  expectAllCasesMatch(Shape::hull, Shape::hull);
}

TEST(PhysicsGenericResting, StackOfUnitCubeHullsFollowsAStackOfBoxes)
{
  for (const float dt : { 0.1f, 0.02f })
  {
    const Launch lower{ { 0, 2.5f, 0 } };
    const Launch upper{ { 0.3f, 4.7f, 0.1f }, { 0, 15, 0 } };

    Rig boxes(Shape::box, Shape::box, lower);
    Rig hulls(Shape::hull, Shape::box, lower);
    const auto upperBox = boxes.addBodyAt(upper, Shape::box);
    const auto upperHull = hulls.addBodyAt(upper, Shape::hull);

    for (int tick = 0; tick < ticks; ++tick)
    {
      boxes.step(dt);
      hulls.step(dt);

      for (const auto& [expected, actual] : { std::pair{ boxes.body, hulls.body }, std::pair{ upperBox, upperHull } })
      {
        ASSERT_LT(glm::length(transformOf(expected)->getPosition() - transformOf(actual)->getPosition()),
                  positionTolerance) << "dt " << dt << " tick " << tick;
        ASSERT_LT(glm::length(transformOf(expected)->getRotation() - transformOf(actual)->getRotation()),
                  rotationTolerance) << "dt " << dt << " tick " << tick;
      }
    }
  }
}
