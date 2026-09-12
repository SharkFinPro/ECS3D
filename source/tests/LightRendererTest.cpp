#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/LightRenderer.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <memory>

namespace {
  // Built the way the product builds one: the editor's addComponent edit goes through the registry.
  std::shared_ptr<LightRenderer> makeLight()
  {
    ComponentRegistry componentRegistry;
    registerDataComponents(componentRegistry);

    return std::dynamic_pointer_cast<LightRenderer>(componentRegistry.create("LightRenderer"));
  }
}

TEST(LightRenderer, DefaultsToALightThatActuallyLights)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  EXPECT_EQ(light->getColor(), glm::vec3(1, 1, 1));
  EXPECT_GT(light->getDiffuse(), 0.0f);
  EXPECT_GT(light->getSpecular(), 0.0f);

  // Deliberately zero: ambient is an unattenuated flat fill, so lights that all carry it wash a scene out.
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.0f);

  EXPECT_FALSE(light->isSpotLight());
}

TEST(LightRenderer, DefaultsAreUsableAsASpotLight)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  // Neither is read while the light is a point light, but a zero direction points nowhere and a zero
  // cone angle closes the cone, so toggling to a spot light would produce nothing.
  EXPECT_NE(light->getDirection(), glm::vec3(0, 0, 0));
  EXPECT_GT(light->getConeAngle(), 0.0f);
}

TEST(LightRenderer, RoundTripsEveryFieldThroughJson)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setSpotLight(true);
  light->setColor({ 0.25f, 0.5f, 0.75f });
  light->setAmbient(0.1f);
  light->setDiffuse(0.2f);
  light->setSpecular(0.3f);
  light->setDirection({ 1, 0, 0 });
  light->setConeAngle(45.0f);

  const auto loaded = makeLight();
  ASSERT_NE(loaded, nullptr);
  loaded->loadFromJSON(light->serialize());

  // A field that serialize() writes but loadFromJSON forgets would silently keep its default here.
  EXPECT_EQ(loaded->isSpotLight(), true);
  EXPECT_EQ(loaded->getColor(), glm::vec3(0.25f, 0.5f, 0.75f));
  EXPECT_FLOAT_EQ(loaded->getAmbient(), 0.1f);
  EXPECT_FLOAT_EQ(loaded->getDiffuse(), 0.2f);
  EXPECT_FLOAT_EQ(loaded->getSpecular(), 0.3f);
  EXPECT_EQ(loaded->getDirection(), glm::vec3(1, 0, 0));
  EXPECT_FLOAT_EQ(loaded->getConeAngle(), 45.0f);
}

TEST(LightRenderer, SetConeAngleKeepsAnInRangeValueExactly)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setConeAngle(45.0f);

  EXPECT_FLOAT_EQ(light->getConeAngle(), 45.0f);
}

TEST(LightRenderer, SetConeAngleClampsAboveTheMax)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  // The shadow frustum's fov is coneAngle * 2, so 90+ projects 180+ degrees and degenerates.
  light->setConeAngle(200.0f);

  EXPECT_FLOAT_EQ(light->getConeAngle(), LightRenderer::maxConeAngleDegrees);
}

TEST(LightRenderer, SetConeAngleClampsBelowTheMin)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setConeAngle(-10.0f);

  EXPECT_FLOAT_EQ(light->getConeAngle(), LightRenderer::minConeAngleDegrees);
}

TEST(LightRenderer, AnOutOfRangeConeAngleIsClampedOnLoad)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  auto saved = light->serialize();
  saved["coneAngle"] = 200.0f;

  // A hand-edited or corrupt project file must not reintroduce a degenerate cone angle.
  const auto loaded = makeLight();
  ASSERT_NE(loaded, nullptr);
  loaded->loadFromJSON(saved);

  EXPECT_FLOAT_EQ(loaded->getConeAngle(), LightRenderer::maxConeAngleDegrees);
}

TEST(LightRenderer, AnOutOfRangeConeAngleIsClampedOffTheWire)
{
  net::Message message(net::MessageType::undefined);
  message.write(ComponentType::lightRenderer);
  message.write(true);
  message.write(glm::vec3(1));
  message.write(0.0f);
  message.write(0.75f);
  message.write(0.75f);
  message.write(glm::vec3(0, -1, 0));
  message.write(200.0f);

  net::MessageReader reader(message);
  // pack writes the type discriminator first; unpack expects the reader positioned after it.
  static_cast<void>(reader.read<ComponentType>());

  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->unpack(reader);

  EXPECT_FLOAT_EQ(light->getConeAngle(), LightRenderer::maxConeAngleDegrees);
}
