#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/Camera.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
  // Built the way the product builds one: the editor's addComponent edit goes through the registry.
  std::shared_ptr<Camera> makeCamera()
  {
    ComponentRegistry componentRegistry;
    registerDataComponents(componentRegistry);

    return std::dynamic_pointer_cast<Camera>(componentRegistry.create("Camera"));
  }
}

TEST(CameraRange, SetFovKeepsAnInRangeValueExactly)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setFov(70.0f);

  EXPECT_FLOAT_EQ(camera->getFov(), 70.0f);
}

TEST(CameraRange, SetFovClampsBelowTheMin)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setFov(0.0f);

  EXPECT_FLOAT_EQ(camera->getFov(), Camera::minFovDegrees);
}

TEST(CameraRange, SetFovClampsAboveTheMax)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  // 180+ degrees projects a degenerate (or inverted) frustum.
  camera->setFov(200.0f);

  EXPECT_FLOAT_EQ(camera->getFov(), Camera::maxFovDegrees);
}

TEST(CameraRange, SetNearPlaneClampsBelowTheMin)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setNearPlane(-1.0f);

  EXPECT_FLOAT_EQ(camera->getNearPlane(), Camera::minNearPlane);
}

TEST(CameraRange, SetFarPlaneAtOrBelowNearIsPushedBeyondNear)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setNearPlane(5.0f);
  camera->setFarPlane(5.0f);

  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());
}

TEST(CameraRange, SetNearPlaneAboveTheCurrentFarPushesFar)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  // Positive control: with a default far plane well above this near value, far is left alone.
  camera->setNearPlane(1.0f);
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 1000.0f);

  camera->setNearPlane(2000.0f);

  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());
}

TEST(CameraRange, LoadFromJSONWithInconsistentValuesYieldsAValidCamera)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  nlohmann::json data;
  data["type"] = "Camera";
  data["fov"] = 0.0f;
  data["nearPlane"] = 5.0f;
  data["farPlane"] = 1.0f;
  camera->loadFromJSON(data);

  EXPECT_FLOAT_EQ(camera->getFov(), Camera::minFovDegrees);
  EXPECT_FLOAT_EQ(camera->getNearPlane(), 5.0f);
  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());
}

TEST(CameraRange, LoadFromJSONIsOrderIndependent)
{
  // Same fields as above but inserted far-then-near; nlohmann::json is a map, but the component's own
  // loadFromJSON must not read them in a way that produces a different result either way.
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  nlohmann::json data;
  data["type"] = "Camera";
  data["farPlane"] = 1.0f;
  data["nearPlane"] = 5.0f;
  data["fov"] = 0.0f;
  camera->loadFromJSON(data);

  const auto reference = makeCamera();
  ASSERT_NE(reference, nullptr);
  nlohmann::json referenceData;
  referenceData["type"] = "Camera";
  referenceData["fov"] = 0.0f;
  referenceData["nearPlane"] = 5.0f;
  referenceData["farPlane"] = 1.0f;
  reference->loadFromJSON(referenceData);

  EXPECT_FLOAT_EQ(camera->getFov(), reference->getFov());
  EXPECT_FLOAT_EQ(camera->getNearPlane(), reference->getNearPlane());
  EXPECT_FLOAT_EQ(camera->getFarPlane(), reference->getFarPlane());
}

TEST(CameraRange, NonFiniteSettersAreIgnored)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setFov(70.0f);
  camera->setNearPlane(0.05f);
  camera->setFarPlane(500.0f);

  constexpr float infinity = std::numeric_limits<float>::infinity();
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();

  camera->setFov(infinity);
  camera->setFov(-infinity);
  camera->setFov(nan);
  EXPECT_FLOAT_EQ(camera->getFov(), 70.0f);

  camera->setNearPlane(infinity);
  camera->setNearPlane(-infinity);
  camera->setNearPlane(nan);
  EXPECT_FLOAT_EQ(camera->getNearPlane(), 0.05f);

  camera->setFarPlane(infinity);
  camera->setFarPlane(-infinity);
  camera->setFarPlane(nan);
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 500.0f);
}
