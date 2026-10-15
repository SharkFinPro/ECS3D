#include <gtest/gtest.h>

#include "PhysicsGenericRestingFixtures.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace restingFixtures;
  using fixtures::transformOf;

  constexpr int ticks = 300;
  constexpr float positionTolerance = 1e-4f;
  constexpr float rotationTolerance = 0.01f;
  constexpr float flushCosine = 0.9999f;
  constexpr float balancedHeight = 2.4142f;
  constexpr float flatHeight = 2.0f;

  enum class Track { everyTick, endState };

  struct Case {
    const char* name;
    Launch launch;
    Track track;
    int pushTick = -1;
    // A body balanced on an edge may fall either way on float noise, so it may also end balanced.
    bool mayStayBalanced = false;
  };

  // Tipping bodies amplify float differences between the box and hull paths (and between compilers), so only the
  // end state of those is compared. The rest follow the box tick for tick.
  const std::vector<Case> cases{
    { "tilted drop", { { 0, 5, 0 }, { 20, 0, 10 } }, Track::endState },
    { "corner drop", { { 0, 5, 0 }, { 35, 0, 45 } }, Track::endState },
    { "spinning drop", { { 0, 5, 0 }, { 10, 20, 5 }, { 0.1f, 0, 0 }, { 30, 60, 0 } }, Track::endState },
    { "flat drop", { { 0, 5, 0 } }, Track::everyTick },
    { "edge balance drop", { { 0, 5, 0 }, { 0, 0, 45 } }, Track::endState, -1, true },
    { "slide", { { 0, 2.0f, 0 }, { 0, 0, 0 }, { 0.3f, 0, 0 } }, Track::everyTick },
    { "push", { { 0, 2.0f, 0 } }, Track::everyTick, 100 }
  };

  void push(Rig& rig)
  {
    const auto position = transformOf(rig.body)->getPosition();
    rig.rigidBody->addPendingForce({ 2, 0, 0 }, position + glm::vec3(0, 0.8f, 0), ForceMode::impulse);
  }

  // Some local axis points straight up, so the body lies on a face whatever way it faces about the vertical.
  bool isFlush(const Transform& transform)
  {
    const auto axes = glm::mat3_cast(transform.getOrientation());

    return std::max({ std::fabs(axes[0].y), std::fabs(axes[1].y), std::fabs(axes[2].y) }) > flushCosine;
  }

  // Which local axis points most nearly up, and which way: the face the body lies on is the opposite one.
  std::pair<int, bool> topFace(const Transform& transform)
  {
    const auto axes = glm::mat3_cast(transform.getOrientation());
    int top = 0;
    for (int column = 1; column < 3; ++column)
    {
      if (std::fabs(axes[column].y) > std::fabs(axes[top].y))
      {
        top = column;
      }
    }

    return { top, axes[top].y > 0.0f };
  }

  void expectSettled(const Rig& rig, const Case& scenario, const char* which)
  {
    const auto transform = transformOf(rig.body);
    EXPECT_LT(glm::length(rig.rigidBody->getAngularVelocity()), PhysicsSystem::restAngularSpeed)
      << scenario.name << " " << which;
    EXPECT_LT(glm::length(rig.rigidBody->getVelocity()), 1e-3f) << scenario.name << " " << which;

    const float height = transform->getPosition().y;
    const bool balanced = scenario.mayStayBalanced && std::fabs(height - balancedHeight) < 0.01f;
    EXPECT_TRUE(balanced || (isFlush(*transform) && std::fabs(height - flatHeight) < 0.01f))
      << scenario.name << " " << which << " height " << height;
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

      if (scenario.track == Track::everyTick)
      {
        const auto expected = transformOf(reference.body);
        const auto actual = transformOf(other.body);
        ASSERT_LT(glm::length(expected->getPosition() - actual->getPosition()), positionTolerance)
          << scenario.name << " dt " << dt << " tick " << tick;
        ASSERT_LT(glm::length(expected->getRotation() - actual->getRotation()), rotationTolerance)
          << scenario.name << " dt " << dt << " tick " << tick;
      }
    }

    expectSettled(reference, scenario, "box");
    expectSettled(other, scenario, "other");

    const auto expected = transformOf(reference.body);
    const auto actual = transformOf(other.body);
    EXPECT_LT(glm::length(expected->getPosition() - actual->getPosition()), 1.0f) << scenario.name << " dt " << dt;
    if (std::fabs(expected->getPosition().y - flatHeight) < 0.01f)
    {
      EXPECT_EQ(topFace(*expected), topFace(*actual)) << scenario.name << " dt " << dt;
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
