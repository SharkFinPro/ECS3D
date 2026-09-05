#include <gtest/gtest.h>

#include "objects/components/LightRenderer.h"

#include <glm/vec3.hpp>

TEST(LightRenderer, DefaultsToALightThatActuallyLights)
{
  const LightRenderer light;

  EXPECT_EQ(light.getColor(), glm::vec3(1, 1, 1));
  EXPECT_GT(light.getDiffuse(), 0.0f);
  EXPECT_GT(light.getSpecular(), 0.0f);
  EXPECT_FALSE(light.isSpotLight());
}

TEST(LightRenderer, DefaultDirectionIsNotDegenerate)
{
  const LightRenderer light;

  // Only used once the light is switched to a spot light, but a zero direction points nowhere.
  EXPECT_NE(light.getDirection(), glm::vec3(0, 0, 0));
}
