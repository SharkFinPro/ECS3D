#include <gtest/gtest.h>

#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>

TEST(ComponentVariable, WritesTheInitialValueWhileStopped)
{
  ComponentVariable variable(5);

  EXPECT_EQ(variable.value(), 5);

  variable.set(7);

  EXPECT_EQ(variable.value(), 7);
  EXPECT_EQ(variable.get(), 7);
  EXPECT_EQ(variable.getInitialValue(), 7);
}

TEST(ComponentVariable, LeavesTheInitialValueAloneWhileLive)
{
  ComponentVariable variable(5);
  variable.start();

  variable.set(7);

  EXPECT_EQ(variable.value(), 7);
  EXPECT_EQ(variable.getInitialValue(), 5);

  variable.stop();

  EXPECT_EQ(variable.value(), 5);
}

TEST(ComponentVariable, ReseedsTheLiveValueOnEveryStart)
{
  ComponentVariable variable(5);

  variable.start();
  variable.set(7);
  variable.stop();
  variable.start();

  EXPECT_EQ(variable.value(), 5);
}

TEST(ComponentVariable, TransformMoveDoesNotDisturbTheInitialPosition)
{
  Transform transform(glm::vec3(1, 2, 3), glm::vec3(1), glm::vec3(0));
  transform.start();

  transform.move(glm::vec3(0, 1, 0));

  EXPECT_EQ(transform.getLocalPosition(), glm::vec3(1, 3, 3));

  transform.stop();

  EXPECT_EQ(transform.getLocalPosition(), glm::vec3(1, 2, 3));
}
