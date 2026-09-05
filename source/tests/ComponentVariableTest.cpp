#include <gtest/gtest.h>

#include "objects/components/Component.h"
#include "objects/components/Transform.h"

#include <glm/vec3.hpp>
#include <type_traits>
#include <utility>

namespace {
  // Component-wise with a tolerance: move() adds, so the result is arithmetic rather than a value copied
  // through, and glm has no printer here - an exact compare would fail with a hex dump of the bytes.
  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-5f;

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

// The whole point of the accessor: it hands back a copy, so no caller can hold a reference that a
// later start() quietly repoints at the other value, let alone write through one.
static_assert(std::is_same_v<decltype(std::declval<const ComponentVariable<int>&>().get()), int>);

TEST(ComponentVariable, WritesTheInitialValueWhileStopped)
{
  ComponentVariable variable(5);

  EXPECT_EQ(variable.get(), 5);

  variable.set(7);

  EXPECT_EQ(variable.get(), 7);
  EXPECT_EQ(variable.getInitialValue(), 7);
}

TEST(ComponentVariable, LeavesTheInitialValueAloneWhileLive)
{
  ComponentVariable variable(5);
  variable.start();

  variable.set(7);

  EXPECT_EQ(variable.get(), 7);
  EXPECT_EQ(variable.getInitialValue(), 5);

  variable.stop();

  EXPECT_EQ(variable.get(), 5);
}

TEST(ComponentVariable, ReseedsTheLiveValueOnEveryStart)
{
  ComponentVariable variable(5);

  variable.start();
  variable.set(7);
  variable.stop();
  variable.start();

  EXPECT_EQ(variable.get(), 5);
}

TEST(Transform, MoveDoesNotDisturbTheInitialPosition)
{
  Transform transform(glm::vec3(1, 2, 3), glm::vec3(1), glm::vec3(0));
  transform.start();

  transform.move(glm::vec3(0, 1, 0));

  // getLocalPosition, not getPosition: the latter walks to the owning object, and this one has none.
  expectNear(transform.getLocalPosition(), glm::vec3(1, 3, 3));

  transform.stop();

  expectNear(transform.getLocalPosition(), glm::vec3(1, 2, 3));
}
