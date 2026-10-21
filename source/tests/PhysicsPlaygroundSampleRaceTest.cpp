#include <gtest/gtest.h>

#include "PhysicsPlaygroundTestHelpers.h"
#include "TemplateTestHelpers.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>
#include <uuid.h>
#include <vector>

using namespace templateTest;
using namespace playgroundtest;
using nlohmann::json;

namespace
{
  struct Racer
  {
    std::shared_ptr<Object> object;
    glm::vec3 start;
  };

  // A body placed as the lane Spawner would: the prefab at the Spawner, with its initial velocity per tick.
  Racer release(LoadedTemplate& loaded, const std::string& lane)
  {
    const auto& spawner = only("Race Spawner " + lane);
    const auto prefab = uuids::uuid::from_string(scriptField(spawner, "Spawner", "m_prefab").get<std::string>());
    const auto body = loaded.assetRegistry.getPrefabBody(prefab.value());
    const auto start = worldPositionOf(spawner);

    const auto objectManager = loaded.sceneManager.getCurrentScene()->getObjectManager();
    const auto object = objectManager->instantiate(body);
    object->getComponent<Transform>(ComponentType::transform)->setPosition(start);

    const auto fields = spawner.at("scripts").at(0).at("fields");
    for (const auto& field : fields)
    {
      if (field.at("name") == "m_initialVelocity")
      {
        object->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity(vec3Of(field.at("value")) * dt);
      }
    }

    return { object, start };
  }

  float downhill(const Racer& racer)
  {
    return fixtures::positionOf(racer.object).z - racer.start.z;
  }
}

TEST(PhysicsPlaygroundSampleRace, TheBlockHoldsWhileTheRollersAndSliderGoDownhill)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);

  std::map<std::string, Racer> racers;
  for (const char* lane : { "Sphere", "Drum", "Capsule Across", "Capsule Along", "Block" })
  {
    racers.emplace(lane, release(loaded, lane));
  }

  tick(*loaded.sceneManager.getCurrentScene()->getObjectManager(), 300);

  const auto& block = racers.at("Block");
  EXPECT_LT(glm::distance(fixtures::positionOf(block.object), block.start), 0.05f);

  for (const char* lane : { "Sphere", "Drum", "Capsule Across", "Capsule Along" })
  {
    EXPECT_GT(downhill(racers.at(lane)), 2.0f) << lane;
  }
}

TEST(PhysicsPlaygroundSampleRace, TheHexLogThunksOverAFaceAndComesToRest)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto log = release(loaded, "Hex Log");

  tick(*loaded.sceneManager.getCurrentScene()->getObjectManager(), 300);

  EXPECT_GT(downhill(log), 0.5f);
  const auto body = log.object->getComponent<RigidBody>(ComponentType::rigidBody);
  EXPECT_LT(glm::length(body->getVelocity()), 0.005f);
}
