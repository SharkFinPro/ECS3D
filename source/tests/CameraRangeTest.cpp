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
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 5.0f + Camera::minFarPlaneClearance);
}

TEST(CameraRange, TheRangeConstantsKeepTheFrustumNonDegenerate)
{
  EXPECT_GT(Camera::minFovDegrees, 0.0f);
  EXPECT_LT(Camera::maxFovDegrees, 180.0f);
  EXPECT_GT(Camera::minNearPlane, 0.0f);
  EXPECT_GT(Camera::minFarPlaneClearance, 0.0f);
}

TEST(CameraRange, SetFovKeepsTheValuesAroundTheMaxBoundary)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setFov(178.9f);
  EXPECT_FLOAT_EQ(camera->getFov(), 178.9f);

  camera->setFov(179.1f);
  EXPECT_FLOAT_EQ(camera->getFov(), Camera::maxFovDegrees);
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

TEST(CameraRange, SingleFieldSettersAreOrderDependentUnlikeLoadFromJSON)
{
  // Unlike loadFromJSON/unpack (which apply both values atomically through setNearFarPlanes), the public
  // setNearPlane/setFarPlane each react only to whatever the *other* plane currently holds - see the
  // header comment on setNearPlane/setFarPlane. Starting from the defaults (near 0.1, far 1000):
  //
  // near-then-far: setNearPlane clamps near to the minimum against the still-default far (1000, so far
  // is untouched), then setFarPlane pushes the requested far up against that new, small near.
  const auto nearThenFar = makeCamera();
  ASSERT_NE(nearThenFar, nullptr);
  nearThenFar->setNearPlane(0.0005f);
  nearThenFar->setFarPlane(0.0015f);

  EXPECT_FLOAT_EQ(nearThenFar->getNearPlane(), Camera::minNearPlane);
  EXPECT_GT(nearThenFar->getFarPlane(), nearThenFar->getNearPlane());
  // Far stayed close to near; it was never pushed up against the stale default far plane.
  EXPECT_LT(nearThenFar->getFarPlane(), 0.01f);

  // far-then-near: setFarPlane pushes the requested far up against the still-default near (0.1), then
  // setNearPlane only lowers near - it never pulls an already-too-large far back down.
  const auto farThenNear = makeCamera();
  ASSERT_NE(farThenNear, nullptr);
  farThenNear->setFarPlane(0.0015f);
  farThenNear->setNearPlane(0.0005f);

  EXPECT_FLOAT_EQ(farThenNear->getNearPlane(), Camera::minNearPlane);
  EXPECT_GT(farThenNear->getFarPlane(), farThenNear->getNearPlane());
  EXPECT_GT(farThenNear->getFarPlane(), nearThenFar->getFarPlane());
}

TEST(CameraRange, SetNearPlaneAtALargeMagnitudeKeepsFarStrictlyGreater)
{
  // Past roughly a near of 32768, float's representable spacing exceeds minFarPlaneClearance, so
  // near + minFarPlaneClearance alone can round back down to near itself.
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  camera->setNearPlane(1e6f);
  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());

  camera->setNearPlane(1e7f);
  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());
}

TEST(CameraRange, LoadFromJSONAtALargeMagnitudeKeepsFarStrictlyGreater)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  nlohmann::json data;
  data["type"] = "Camera";
  data["nearPlane"] = 1e7f;
  data["farPlane"] = 1e7f;
  camera->loadFromJSON(data);

  EXPECT_GT(camera->getFarPlane(), camera->getNearPlane());
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

namespace {
  // Builds the wire payload Camera::pack() writes (tag, direction, fov, nearPlane, farPlane, active),
  // with the direction field replaced by the caller's own value. The camera is inactive, which is the
  // opposite of the default, so a dropped read shows.
  net::Message packedCameraWithDirection(const glm::vec3& direction)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::camera);
    message.write(direction);
    message.write(70.0f);
    message.write(0.05f);
    message.write(500.0f);
    message.write(false);
    return message;
  }

  net::MessageReader readerPastTag(const net::Message& message)
  {
    net::MessageReader reader(message);
    // pack writes the type discriminator first; unpack expects the reader positioned after it.
    static_cast<void>(reader.read<ComponentType>());
    return reader;
  }
}

TEST(CameraRange, UnpackWithANonFiniteDirectionKeepsThePriorDirection)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 1.0f, 0.0f, 0.0f });

  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  const auto message = packedCameraWithDirection({ nan, nan, nan });
  auto reader = readerPastTag(message);

  camera->unpack(reader);

  EXPECT_EQ(camera->getDirection(), glm::vec3(1.0f, 0.0f, 0.0f));

  // The reader must stay aligned past the rejected direction: the fields that follow still unpack.
  EXPECT_FLOAT_EQ(camera->getFov(), 70.0f);
  EXPECT_FLOAT_EQ(camera->getNearPlane(), 0.05f);
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 500.0f);
  EXPECT_FALSE(camera->isActive());
}

TEST(CameraRange, UnpackWithAnInfiniteDirectionKeepsThePriorDirection)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 0.0f, 1.0f, 0.0f });

  constexpr float infinity = std::numeric_limits<float>::infinity();
  const auto message = packedCameraWithDirection({ infinity, 0.0f, 0.0f });
  auto reader = readerPastTag(message);

  camera->unpack(reader);

  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));

  EXPECT_FLOAT_EQ(camera->getFov(), 70.0f);
  EXPECT_FLOAT_EQ(camera->getNearPlane(), 0.05f);
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 500.0f);
  EXPECT_FALSE(camera->isActive());
}

TEST(CameraRange, UnpackWithAFiniteDirectionAppliesIt)
{
  // Positive control for the two tests above.
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 1.0f, 0.0f, 0.0f });

  const auto message = packedCameraWithDirection({ 0.0f, 0.0f, 1.0f });
  auto reader = readerPastTag(message);

  camera->unpack(reader);

  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 0.0f, 1.0f));
  EXPECT_FLOAT_EQ(camera->getFov(), 70.0f);
  EXPECT_FLOAT_EQ(camera->getNearPlane(), 0.05f);
  EXPECT_FLOAT_EQ(camera->getFarPlane(), 500.0f);
  EXPECT_FALSE(camera->isActive());
}

TEST(CameraRange, LoadFromJSONWithANullDirectionElementKeepsThePriorDirection)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 1.0f, 0.0f, 0.0f });

  nlohmann::json data;
  data["type"] = "Camera";
  data["direction"] = { nullptr, 0.0f, 0.0f };
  camera->loadFromJSON(data);

  EXPECT_EQ(camera->getDirection(), glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(CameraRange, LoadFromJSONWithANonFiniteDirectionElementKeepsThePriorDirection)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 0.0f, 1.0f, 0.0f });

  nlohmann::json data;
  data["type"] = "Camera";
  data["direction"] = { std::numeric_limits<double>::quiet_NaN(), 0.0f, 0.0f };
  camera->loadFromJSON(data);

  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(CameraRange, LoadFromJSONWithAFiniteDirectionAppliesIt)
{
  // Positive control for the two tests above.
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 1.0f, 0.0f, 0.0f });

  nlohmann::json data;
  data["type"] = "Camera";
  data["direction"] = { 0.0f, 0.0f, 1.0f };
  camera->loadFromJSON(data);

  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(CameraRange, LoadFromJSONWithoutADirectionKeepsThePriorDirection)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 1.0f, 0.0f, 0.0f });

  nlohmann::json data;
  data["type"] = "Camera";
  data["fov"] = 60.0f;
  camera->loadFromJSON(data);

  EXPECT_EQ(camera->getDirection(), glm::vec3(1.0f, 0.0f, 0.0f));
  EXPECT_FLOAT_EQ(camera->getFov(), 60.0f);
}

TEST(CameraRange, LoadFromJSONWithADirectionOfTheWrongSizeIsIgnored)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);
  camera->setDirection({ 0.0f, 1.0f, 0.0f });

  nlohmann::json data;
  data["type"] = "Camera";
  data["fov"] = 60.0f;

  data["direction"] = { 0.0f, 0.0f };
  camera->loadFromJSON(data);
  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));

  data["direction"] = { 0.0f, 0.0f, 1.0f, 0.0f };
  camera->loadFromJSON(data);
  EXPECT_EQ(camera->getDirection(), glm::vec3(0.0f, 1.0f, 0.0f));

  // The rest of the component still loads, so only the direction was skipped.
  EXPECT_FLOAT_EQ(camera->getFov(), 60.0f);
}

TEST(CameraRange, LoadFromJSONAppliesAnInactiveFlagAndDefaultsToActive)
{
  const auto camera = makeCamera();
  ASSERT_NE(camera, nullptr);

  nlohmann::json data;
  data["type"] = "Camera";
  data["active"] = false;
  camera->loadFromJSON(data);
  EXPECT_FALSE(camera->isActive());

  data.erase("active");
  camera->loadFromJSON(data);
  EXPECT_TRUE(camera->isActive());
}
