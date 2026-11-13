#include <gtest/gtest.h>

#include <TransformPushGate.h>

#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"

#include <cstdint>
#include <glm/vec3.hpp>

namespace {
  using fixtures::transformOf;
}

TEST(TransformPushGate, TheFirstCallPushes)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(7));
}

TEST(TransformPushGate, ZeroIsAnOrdinaryFirstID)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(0));
  EXPECT_FALSE(gate.needsPush(0));
}

TEST(TransformPushGate, TheSameIDIsSkippedAfterThePush)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(7));

  for (int i = 0; i < 100; ++i)
  {
    EXPECT_FALSE(gate.needsPush(7));
  }
}

TEST(TransformPushGate, AChangedIDPushesOnceThenSkips)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(7));
  EXPECT_TRUE(gate.needsPush(8));
  EXPECT_FALSE(gate.needsPush(8));
  EXPECT_FALSE(gate.needsPush(8));
}

TEST(TransformPushGate, ReturningToAnEarlierIDPushesAgain)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(9));
  EXPECT_TRUE(gate.needsPush(4));
  EXPECT_FALSE(gate.needsPush(4));
  EXPECT_TRUE(gate.needsPush(9));
  EXPECT_FALSE(gate.needsPush(9));
}

TEST(TransformPushGate, AFreshGateAfterReplacementPushesTheSameIDAgain)
{
  TransformPushGate gate;

  EXPECT_TRUE(gate.needsPush(7));
  EXPECT_FALSE(gate.needsPush(7));

  gate = TransformPushGate{};

  EXPECT_TRUE(gate.needsPush(7));
}

TEST(TransformPushGate, EveryTransformChangeMovesTheWorldUpdateID)
{
  fixtures::Scene scene;
  const auto parent = fixtures::addObject(scene, "Parent");
  const auto otherParent = fixtures::addObject(scene, "OtherParent");
  const auto child = fixtures::addChildObject(scene, "Child", parent);
  const auto transform = transformOf(child);

  auto last = transform->getWorldUpdateID();
  const auto expectChanged = [&]()
  {
    const auto current = transform->getWorldUpdateID();
    EXPECT_NE(current, last);
    last = current;
  };

  transform->setPosition(glm::vec3(1, 2, 3));
  expectChanged();

  transform->setScale(glm::vec3(2, 2, 2));
  expectChanged();

  transform->setRotation(glm::vec3(10, 20, 30));
  expectChanged();

  transformOf(parent)->setPosition(glm::vec3(5, 0, 0));
  expectChanged();

  child->setParent(otherParent);
  expectChanged();

  transform->markReparented();
  expectChanged();

  EXPECT_EQ(transform->getWorldUpdateID(), last);
}
