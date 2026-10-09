#include <gtest/gtest.h>

#include "Billboard.h"
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <limits>

namespace {
  constexpr float tolerance = 1e-4f;

  void expectVec3Near(const glm::vec3& actual, const glm::vec3& expected)
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // Looks down -Z from the origin, so a point at (0, 0, -d) has depth d.
  glm::mat4 axisView()
  {
    return glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  }

  void expectFacesEye(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up)
  {
    const glm::mat4 view = glm::lookAt(eye, target, up);
    const glm::mat3 rotation = glm::mat3(view);
    const glm::quat orientation = billboardOrientation(view);

    expectVec3Near(orientation * glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(rotation[0][0], rotation[1][0], rotation[2][0]));
    expectVec3Near(orientation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(rotation[0][1], rotation[1][1], rotation[2][1]));
    expectVec3Near(orientation * glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(rotation[0][2], rotation[1][2], rotation[2][2]));

    EXPECT_GT(glm::dot(orientation * glm::vec3(0.0f, 0.0f, 1.0f), eye - target), 0.0f);
  }

  // The NDC height of a camera-facing vertical edge of the given world size centered on point, as a fraction
  // of the full -1..1 range.
  float projectedFraction(const glm::mat4& view, const glm::vec3& point, const float size,
                          const float fovDegrees, const float aspect)
  {
    const glm::mat4 projection = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 1000.0f);
    const glm::vec3 up = billboardOrientation(view) * glm::vec3(0.0f, 1.0f, 0.0f);

    const auto ndcY = [&](const glm::vec3& world)
    {
      const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
      return clip.y / clip.w;
    };

    return std::abs(ndcY(point + up * (size / 2.0f)) - ndcY(point - up * (size / 2.0f))) / 2.0f;
  }
}

TEST(BillboardTest, IdentityViewGivesIdentityRotation)
{
  const glm::quat orientation = billboardOrientation(glm::mat4(1.0f));

  expectVec3Near(orientation * glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
  expectVec3Near(orientation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  expectVec3Near(orientation * glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(BillboardTest, OrientationMatchesCameraAxesAndFacesTheEye)
{
  expectFacesEye(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  expectFacesEye(glm::vec3(3.0f, 4.0f, -7.0f), glm::vec3(-1.0f, 0.5f, 2.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  expectFacesEye(glm::vec3(2.0f, 10.0f, 1.0f), glm::vec3(2.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
}

TEST(BillboardTest, SizeGrowsLinearlyWithDepth)
{
  const glm::mat4 view = axisView();

  const auto nearSize = billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -10.0f), 90.0f, 0.1f, 0.05f);
  const auto farSize = billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -20.0f), 90.0f, 0.1f, 0.05f);

  ASSERT_TRUE(nearSize.has_value());
  ASSERT_TRUE(farSize.has_value());
  EXPECT_NEAR(*nearSize, 1.0f, tolerance);
  EXPECT_NEAR(*farSize, 2.0f * *nearSize, tolerance);
}

TEST(BillboardTest, SizeDependsOnDepthNotDistance)
{
  const glm::mat4 view = axisView();

  const auto onAxis = billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -10.0f), 60.0f, 0.1f, 0.05f);
  const auto offAxis = billboardWorldSize(view, glm::vec3(30.0f, -12.0f, -10.0f), 60.0f, 0.1f, 0.05f);

  ASSERT_TRUE(onAxis.has_value());
  ASSERT_TRUE(offAxis.has_value());
  EXPECT_NEAR(*offAxis, *onAxis, tolerance);
}

TEST(BillboardTest, ProjectedHeightIsTheRequestedFractionOfTheViewport)
{
  const glm::vec3 eye(1.0f, 2.0f, 3.0f);
  const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::vec3 forward = glm::normalize(-eye);

  for (const float fov : { 45.0f, 90.0f })
  {
    for (const float distance : { 5.0f, 40.0f })
    {
      const glm::vec3 point = eye + forward * distance;

      const auto size = billboardWorldSize(view, point, fov, 0.1f, lightGizmoScreenFraction);

      ASSERT_TRUE(size.has_value());
      EXPECT_NEAR(projectedFraction(view, point, *size, fov, 16.0f / 9.0f), lightGizmoScreenFraction, tolerance);
    }
  }
}

TEST(BillboardTest, NothingToDrawAtOrInsideTheNearPlaneOrBehindTheCamera)
{
  const glm::mat4 view = axisView();

  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, 5.0f), 60.0f, 0.1f, 0.05f).has_value());
  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f), 60.0f, 0.1f, 0.05f).has_value());
  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -0.05f), 60.0f, 0.1f, 0.05f).has_value());

  EXPECT_TRUE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -0.11f), 60.0f, 0.1f, 0.05f).has_value());
}

TEST(BillboardTest, NonFiniteInputsProduceNothing)
{
  const glm::mat4 view = axisView();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(nan, 0.0f, -10.0f), 60.0f, 0.1f, 0.05f).has_value());
  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -inf), 60.0f, 0.1f, 0.05f).has_value());
  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -10.0f), nan, 0.1f, 0.05f).has_value());
  EXPECT_FALSE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -10.0f), inf, 0.1f, 0.05f).has_value());

  EXPECT_TRUE(billboardWorldSize(view, glm::vec3(0.0f, 0.0f, -10.0f), 60.0f, 0.1f, 0.05f).has_value());
}

TEST(BillboardTest, FieldOfViewScalesTheSizeByTangentOfHalfAngle)
{
  const glm::mat4 view = axisView();
  const glm::vec3 point(0.0f, 0.0f, -10.0f);

  const auto narrow = billboardWorldSize(view, point, 40.0f, 0.1f, 0.05f);
  const auto wide = billboardWorldSize(view, point, 100.0f, 0.1f, 0.05f);

  ASSERT_TRUE(narrow.has_value());
  ASSERT_TRUE(wide.has_value());
  EXPECT_NEAR(*wide / *narrow, std::tan(glm::radians(50.0f)) / std::tan(glm::radians(20.0f)), tolerance);
}
