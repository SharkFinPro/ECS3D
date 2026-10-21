#include <gtest/gtest.h>

#include "CoinRushTestHelpers.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "TemplateTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace templateTest;
using namespace coinrushTest;

namespace
{
  constexpr float daisHalfWidth = 6.0f;

  bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  bool offDais(const glm::vec3& position)
  {
    return std::max(std::abs(position.x), std::abs(position.z)) > daisHalfWidth;
  }

  void step(const ObjectManager& objectManager, CollisionSystem& collisionSystem)
  {
    PhysicsSystem::fixedUpdate(objectManager, dt);
    collisionSystem.fixedUpdate(objectManager, dt);
  }

  std::shared_ptr<Object> place(ObjectManager& objectManager, const nlohmann::json& body, const glm::vec3& position)
  {
    const auto object = objectManager.instantiate(body);
    fixtures::transformOf(object)->setPosition(position);

    return object;
  }

  struct Throw
  {
    int leftAtTick = -1;
    glm::vec3 finalPosition{ 0.0f };
  };

  // A boulder dropped from the spawner with the spawner's own Initial Velocity, set on the body the way
  // Spawner does (units per second, stored per tick). Scripts do not run, so nothing else touches it.
  Throw throwBoulder(const glm::vec2& offset)
  {
    LoadedTemplate project(templateFolder);
    EXPECT_TRUE(project.loaded);
    auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();
    const auto& scene = *project.sceneManager.getCurrentScene();
    const auto spawner = objectNamed(scene, "Boulder Spawner");
    const auto velocity = vec3Of(fieldOf(scriptNamed(spawner, "Spawner"), "m_initialVelocity"));
    const auto origin = positionOfJson(spawner);

    const auto boulder = place(objectManager, prefabNamed(projectJson(templateFolder), "Big Boulder").at("body"),
                               origin + glm::vec3(offset.x, 0.0f, offset.y));
    boulder->getComponent<RigidBody>(ComponentType::rigidBody)->setVelocity(velocity * dt);

    Throw result;
    CollisionSystem collisionSystem;
    for (int tick = 0; tick < 300; ++tick)
    {
      step(objectManager, collisionSystem);
      if (result.leftAtTick < 0 && offDais(fixtures::positionOf(boulder)))
      {
        result.leftAtTick = tick;
      }
    }

    result.finalPosition = fixtures::positionOf(boulder);

    return result;
  }
}

TEST(CoinRushSamplePhysics, PlayersSettleOnTheFloorAndOnTheDais)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();
  const auto body = prefabBody(project, playerPrefab);

  const auto atSpawn = place(objectManager, body, { -24, 1.02f, -24 });
  const auto onDais = place(objectManager, body, { 0, 3.02f, 0 });

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < 300; ++tick)
  {
    step(objectManager, collisionSystem);
  }

  EXPECT_NEAR(fixtures::positionOf(atSpawn).y, 1.0f, 0.1f);
  EXPECT_NEAR(fixtures::positionOf(onDais).y, 3.0f, 0.1f);
}

TEST(CoinRushSamplePhysics, ABoulderThrownFromAnySpawnOffsetRollsOffTheDais)
{
  const std::vector<glm::vec2> offsets = { { 0, 0 }, { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 } };
  const auto body = prefabNamed(projectJson(templateFolder), "Big Boulder").at("body");
  const float radius = boulderWorldRadius(body);

  for (const auto& offset : offsets)
  {
    SCOPED_TRACE(std::to_string(offset.x) + ", " + std::to_string(offset.y));
    const auto result = throwBoulder(offset);

    ASSERT_TRUE(isFinite(result.finalPosition));
    EXPECT_GE(result.leftAtTick, 0);
    EXPECT_TRUE(offDais(result.finalPosition));
    EXPECT_LT(result.finalPosition.y, 2.0f + radius);
    EXPECT_GT(result.finalPosition.y, 0.5f);
  }
}

TEST(CoinRushSamplePhysics, APlayerSteeringUpARampReachesTheDaisTop)
{
  LoadedTemplate project(templateFolder);
  ASSERT_TRUE(project.loaded);
  auto& objectManager = *project.sceneManager.getCurrentScene()->getObjectManager();
  const auto body = prefabBody(project, playerPrefab);
  const auto player = place(objectManager, body, { 17, 1.02f, 0 });
  const auto rigidBody = player->getComponent<RigidBody>(ComponentType::rigidBody);

  // PlayerScript's steer: close a tenth of the gap to topSpeed * dt along the heading each tick.
  constexpr float topSpeed = 9.0f;
  constexpr float grip = 0.1f;
  const glm::vec3 heading(-1.0f, 0.0f, 0.0f);

  CollisionSystem collisionSystem;
  bool reached = false;
  for (int tick = 0; tick < 300 && !reached; ++tick)
  {
    auto velocity = rigidBody->getVelocity();
    const glm::vec3 horizontal(velocity.x, 0.0f, velocity.z);
    velocity += (heading * (topSpeed * dt) - horizontal) * grip;
    rigidBody->setVelocity(velocity);

    step(objectManager, collisionSystem);
    const auto position = fixtures::positionOf(player);
    reached = position.y > 2.9f && std::abs(position.x) < daisHalfWidth - 0.5f;
  }

  EXPECT_TRUE(reached);
  EXPECT_LT(fixtures::positionOf(player).y, 3.5f);
}
