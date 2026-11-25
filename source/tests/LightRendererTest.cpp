#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/LightRenderer.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <memory>
#include <string>

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

  EXPECT_EQ(light->getLightType(), LightType::point);
  EXPECT_FLOAT_EQ(light->getRange(), 10.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 100.0f);
}

TEST(LightRenderer, DefaultsAreUsableAsASpotLight)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  // Neither is read while the light is a point light, but a zero direction points nowhere and a zero
  // cone angle closes the cone, so switching to a spot light would produce nothing.
  EXPECT_NE(light->getDirection(), glm::vec3(0, 0, 0));
  EXPECT_GT(light->getConeAngle(), 0.0f);
}

TEST(LightRenderer, RoundTripsEveryFieldThroughJson)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setLightType(LightType::spot);
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
  EXPECT_EQ(loaded->getLightType(), LightType::spot);
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
  message.write(static_cast<uint8_t>(LightType::spot));
  message.write(glm::vec3(1));
  message.write(0.0f);
  message.write(0.75f);
  message.write(0.75f);
  message.write(glm::vec3(0, -1, 0));
  message.write(200.0f);
  message.write(10.0f);
  message.write(100.0f);

  net::MessageReader reader(message);
  // pack writes the type discriminator first; unpack expects the reader positioned after it.
  static_cast<void>(reader.read<ComponentType>());

  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->unpack(reader);

  EXPECT_FLOAT_EQ(light->getConeAngle(), LightRenderer::maxConeAngleDegrees);
}

TEST(LightRenderer, TheConeRangeConstantsKeepTheShadowFrustumNonDegenerate)
{
  EXPECT_GT(LightRenderer::minConeAngleDegrees, 0.0f);
  EXPECT_LT(LightRenderer::maxConeAngleDegrees, 90.0f);
}

TEST(LightRenderer, SetConeAngleKeepsTheValuesAroundTheMaxBoundary)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setConeAngle(88.9f);
  EXPECT_FLOAT_EQ(light->getConeAngle(), 88.9f);

  light->setConeAngle(89.1f);
  EXPECT_FLOAT_EQ(light->getConeAngle(), LightRenderer::maxConeAngleDegrees);
}

TEST(LightRenderer, EveryFieldRoundTripsThroughThePackedWireFormat)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setLightType(LightType::spot);
  light->setColor({ 0.25f, 0.5f, 0.75f });
  light->setAmbient(0.1f);
  light->setDiffuse(0.2f);
  light->setSpecular(0.9f);
  light->setDirection({ 1, 0, 0 });
  light->setConeAngle(30.0f);

  net::Message message(net::MessageType::undefined);
  light->pack(message);

  net::MessageReader reader(message);
  EXPECT_EQ(reader.read<ComponentType>(), ComponentType::lightRenderer);

  const auto loaded = makeLight();
  ASSERT_NE(loaded, nullptr);
  loaded->unpack(reader);

  EXPECT_EQ(loaded->getLightType(), LightType::spot);
  EXPECT_EQ(loaded->getColor(), glm::vec3(0.25f, 0.5f, 0.75f));
  EXPECT_FLOAT_EQ(loaded->getAmbient(), 0.1f);
  EXPECT_FLOAT_EQ(loaded->getDiffuse(), 0.2f);
  EXPECT_FLOAT_EQ(loaded->getSpecular(), 0.9f);
  EXPECT_EQ(loaded->getDirection(), glm::vec3(1, 0, 0));
  EXPECT_FLOAT_EQ(loaded->getConeAngle(), 30.0f);
}

namespace {
  constexpr LightType allLightTypes[] = { LightType::point, LightType::spot, LightType::directional };

  nlohmann::json legacyLightJson(const bool isSpotlight)
  {
    return {
      { "type", "LightRenderer" },
      { "color", { 1.0f, 1.0f, 1.0f } },
      { "direction", { 0.0f, -1.0f, 0.0f } },
      { "isSpotlight", isSpotlight },
      { "ambient", 0.0f },
      { "diffuse", 0.75f },
      { "specular", 0.75f },
      { "coneAngle", 30.0f }
    };
  }

  net::Message packedLightWithType(const uint8_t type)
  {
    net::Message message(net::MessageType::undefined);
    message.write(ComponentType::lightRenderer);
    message.write(type);
    message.write(glm::vec3(1));
    message.write(0.0f);
    message.write(0.75f);
    message.write(0.75f);
    message.write(glm::vec3(0, -1, 0));
    message.write(30.0f);
    message.write(10.0f);
    message.write(100.0f);
    return message;
  }
}

TEST(LightRenderer, EveryLightTypeRoundTripsThroughJsonWithRangeAndShadowDistance)
{
  for (const LightType type : allLightTypes)
  {
    const auto light = makeLight();
    ASSERT_NE(light, nullptr);
    light->setLightType(type);
    light->setRange(37.5f);
    light->setShadowDistance(250.0f);

    const auto loaded = makeLight();
    ASSERT_NE(loaded, nullptr);
    loaded->loadFromJSON(light->serialize());

    EXPECT_EQ(loaded->getLightType(), type);
    EXPECT_FLOAT_EQ(loaded->getRange(), 37.5f);
    EXPECT_FLOAT_EQ(loaded->getShadowDistance(), 250.0f);
  }
}

TEST(LightRenderer, SerializeWritesTheTypeNameAndNoLegacyFlag)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setLightType(LightType::directional);

  const auto saved = light->serialize();

  EXPECT_EQ(saved.at("lightType").get<std::string>(), "directional");
  EXPECT_TRUE(saved.contains("range"));
  EXPECT_TRUE(saved.contains("shadowDistance"));
  EXPECT_FALSE(saved.contains("isSpotlight"));
}

TEST(LightRenderer, EveryLightTypeRoundTripsThroughTheWireWithRangeAndShadowDistance)
{
  for (const LightType type : allLightTypes)
  {
    const auto light = makeLight();
    ASSERT_NE(light, nullptr);
    light->setLightType(type);
    light->setRange(37.5f);
    light->setShadowDistance(250.0f);

    net::Message message(net::MessageType::undefined);
    light->pack(message);

    net::MessageReader reader(message);
    EXPECT_EQ(reader.read<ComponentType>(), ComponentType::lightRenderer);

    const auto loaded = makeLight();
    ASSERT_NE(loaded, nullptr);
    loaded->unpack(reader);

    EXPECT_EQ(loaded->getLightType(), type);
    EXPECT_FLOAT_EQ(loaded->getRange(), 37.5f);
    EXPECT_FLOAT_EQ(loaded->getShadowDistance(), 250.0f);
  }
}

TEST(LightRenderer, ALegacySpotlightFlagLoadsAsASpotLightWithTheOldUnattenuatedLook)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setShadowDistance(500.0f);

  light->loadFromJSON(legacyLightJson(true));

  EXPECT_EQ(light->getLightType(), LightType::spot);
  EXPECT_FLOAT_EQ(light->getRange(), 0.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 100.0f);
}

TEST(LightRenderer, ALegacyNonSpotlightFlagLoadsAsAPointLight)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setLightType(LightType::spot);

  light->loadFromJSON(legacyLightJson(false));

  EXPECT_EQ(light->getLightType(), LightType::point);
  EXPECT_FLOAT_EQ(light->getRange(), 0.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 100.0f);
}

TEST(LightRenderer, SetRangeClampsAndKeepsZeroAsUnbounded)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setRange(-5.0f);
  EXPECT_FLOAT_EQ(light->getRange(), 0.0f);

  light->setRange(0.2f);
  EXPECT_FLOAT_EQ(light->getRange(), LightRenderer::minRange);

  light->setRange(0.0f);
  EXPECT_FLOAT_EQ(light->getRange(), 0.0f);

  light->setRange(25.0f);
  EXPECT_FLOAT_EQ(light->getRange(), 25.0f);
}

TEST(LightRenderer, SetRangeIgnoresNonFiniteInput)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setRange(25.0f);

  light->setRange(std::numeric_limits<float>::quiet_NaN());
  EXPECT_FLOAT_EQ(light->getRange(), 25.0f);

  light->setRange(std::numeric_limits<float>::infinity());
  EXPECT_FLOAT_EQ(light->getRange(), 25.0f);

  light->setRange(30.0f);
  EXPECT_FLOAT_EQ(light->getRange(), 30.0f);
}

TEST(LightRenderer, SetShadowDistanceClampsToItsBounds)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);

  light->setShadowDistance(0.5f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), LightRenderer::minShadowDistance);

  light->setShadowDistance(5000.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), LightRenderer::maxShadowDistance);

  light->setShadowDistance(250.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 250.0f);
}

TEST(LightRenderer, SetShadowDistanceIgnoresNonFiniteInput)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setShadowDistance(250.0f);

  light->setShadowDistance(std::numeric_limits<float>::quiet_NaN());
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 250.0f);

  light->setShadowDistance(std::numeric_limits<float>::infinity());
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 250.0f);

  light->setShadowDistance(300.0f);
  EXPECT_FLOAT_EQ(light->getShadowDistance(), 300.0f);
}

TEST(LightRenderer, AnUnknownLightTypeNameIsRefusedAndChangesNothing)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setLightType(LightType::spot);
  light->setAmbient(0.4f);
  light->setRange(12.0f);

  auto bad = legacyLightJson(false);
  bad.erase("isSpotlight");
  bad["lightType"] = "laser";
  bad["ambient"] = 0.9f;
  bad["range"] = 50.0f;

  EXPECT_THROW(light->loadFromJSON(bad), RefusedPayload);

  EXPECT_EQ(light->getLightType(), LightType::spot);
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.4f);
  EXPECT_FLOAT_EQ(light->getRange(), 12.0f);

  // Positive control: the same payload with a valid type loads.
  bad["lightType"] = "directional";
  light->loadFromJSON(bad);

  EXPECT_EQ(light->getLightType(), LightType::directional);
  EXPECT_FLOAT_EQ(light->getAmbient(), 0.9f);
  EXPECT_FLOAT_EQ(light->getRange(), 50.0f);
}

TEST(LightRenderer, AWirePayloadWithAnUnknownTypeByteIsRefusedAndChangesNothing)
{
  const auto light = makeLight();
  ASSERT_NE(light, nullptr);
  light->setLightType(LightType::spot);
  light->setRange(12.0f);

  const auto bad = packedLightWithType(3);
  net::MessageReader badReader(bad);
  static_cast<void>(badReader.read<ComponentType>());

  EXPECT_THROW(light->unpack(badReader), RefusedPayload);

  EXPECT_EQ(light->getLightType(), LightType::spot);
  EXPECT_FLOAT_EQ(light->getRange(), 12.0f);

  // Positive control: the highest valid type byte unpacks.
  const auto good = packedLightWithType(2);
  net::MessageReader goodReader(good);
  static_cast<void>(goodReader.read<ComponentType>());
  light->unpack(goodReader);

  EXPECT_EQ(light->getLightType(), LightType::directional);
  EXPECT_FLOAT_EQ(light->getRange(), 10.0f);
}
