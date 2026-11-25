#ifndef SCENELIGHTINGCHECK_H
#define SCENELIGHTINGCHECK_H

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>

// Shared by the suites that check a shipped scene's lighting rig in its raw JSON.
namespace lightingcheck {
struct Tally
{
  int lights = 0;
  int directional = 0;
  int spot = 0;
  int point = 0;
  int withAmbient = 0;
  int ambientNotDirectional = 0;
  int localWithoutRange = 0;
  int namedSunDirectional = 0;
};

inline void tallyLight(const nlohmann::json& light, const bool namedSun, Tally& tally)
{
  const auto type = light.at("lightType").get<std::string>();
  const bool directional = type == "directional";
  const bool hasAmbient = light.at("ambient").get<float>() > 0.0f;
  const bool ranged = light.at("range").get<float>() > 0.0f;

  const auto count = [](const bool condition) { return static_cast<int>(condition); };

  ++tally.lights;
  tally.directional += count(directional);
  tally.spot += count(type == "spot");
  tally.point += count(type == "point");
  tally.withAmbient += count(hasAmbient);
  tally.ambientNotDirectional += count(!directional && hasAmbient);
  tally.localWithoutRange += count(!directional && !ranged);
  tally.namedSunDirectional += count(directional && namedSun);
}

inline void collect(const nlohmann::json& objects, Tally& tally)
{
  for (const auto& object : objects)
  {
    for (const auto& component : object.at("components"))
    {
      if (component.at("type") == "LightRenderer")
      {
        tallyLight(component, object.at("name") == "Sun", tally);
      }
    }

    if (object.contains("children"))
    {
      collect(object.at("children"), tally);
    }
  }
}

// One directional "Sun" that alone carries ambient, and every point or spot light ranged. The lights found are
// asserted positive so an empty walk cannot pass.
inline Tally expectSunRig(const nlohmann::json& objects)
{
  Tally tally;
  collect(objects, tally);

  EXPECT_GT(tally.lights, 0);
  EXPECT_EQ(tally.directional, 1);
  EXPECT_EQ(tally.namedSunDirectional, 1);
  EXPECT_EQ(tally.withAmbient, 1);
  EXPECT_EQ(tally.ambientNotDirectional, 0);
  EXPECT_EQ(tally.localWithoutRange, 0);

  return tally;
}
}

#endif
