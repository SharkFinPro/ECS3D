#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/components/LightRenderer.h"

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
