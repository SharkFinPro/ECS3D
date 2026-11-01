#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsPlaygroundTestHelpers.h"
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

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

using namespace templateTest;
using namespace playgroundtest;

namespace
{
  // Mirrors the Grip constant in PlayerScript.cs.
  constexpr float playerGrip = 0.1f;

  void walkPlayer(const ObjectManager& objectManager, const std::shared_ptr<Object>& player, const glm::vec3& heading,
                  const float topSpeed, const int ticks)
  {
    const auto body = player->getComponent<RigidBody>(ComponentType::rigidBody);
    CollisionSystem collisionSystem;
    for (int i = 0; i < ticks; ++i)
    {
      const auto velocity = body->getVelocity();
      const auto steer = (heading * (topSpeed * dt) - glm::vec3(velocity.x, 0.0f, velocity.z)) * playerGrip;
      body->addPendingForce(steer, fixtures::positionOf(player), ForceMode::velocityChange);
      body->setAngularVelocity(glm::vec3(0.0f));
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
    }
  }
}

TEST(PhysicsPlaygroundSamplePyramid, WalkingThePlayerIntoThePyramidBringsItDown)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  const auto player = objectNamedExactly(objectManager, "Player 0");
  ASSERT_NE(player, nullptr);
  const auto topSpeed = scriptField(only("Player 0"), "PlayerScript", "m_topSpeed").get<float>();
  ASSERT_GT(topSpeed, 0.0f);

  // In front of the pyramid on its center line (station root at z -26), clear of the cubes.
  fixtures::transformOf(player)->setPosition({ 0.4f, 1.05f, -18.0f });

  std::vector<std::shared_ptr<Object>> cubes;
  std::vector<float> startHeights;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getName() == "Pyramid Cube")
    {
      cubes.push_back(object);
      startHeights.push_back(fixtures::positionOf(object).y);
    }
  }
  ASSERT_EQ(cubes.size(), 30u);
  const auto topIndex = static_cast<std::size_t>(std::ranges::max_element(startHeights) - startHeights.begin());
  ASSERT_NEAR(startHeights[topIndex], 3.55f, 1e-4f);

  // Positive control: left alone for as long as the walk run lasts, every cube stays where it started.
  constexpr int walkTicks = 250;
  constexpr int settleTicks = 250;
  walkPlayer(objectManager, player, glm::vec3(0.0f), topSpeed, walkTicks + settleTicks);
  for (std::size_t i = 0; i < cubes.size(); ++i)
  {
    SCOPED_TRACE(i);
    const auto idle = fixtures::positionOf(cubes[i]);
    ASSERT_TRUE(std::isfinite(idle.x) && std::isfinite(idle.y) && std::isfinite(idle.z));
    ASSERT_NEAR(idle.y, startHeights[i], 0.1f);
  }

  walkPlayer(objectManager, player, { 0.0f, 0.0f, -1.0f }, topSpeed, walkTicks);
  walkPlayer(objectManager, player, glm::vec3(0.0f), topSpeed, settleTicks);

  // Measured at top speeds 4 and 9: the top cube fell 1.0 and 3.0, and 2 and 8 cubes dropped over half a unit.
  EXPECT_LT(fixtures::positionOf(cubes[topIndex]).y, startHeights[topIndex] - 0.5f);
  int dropped = 0;
  for (std::size_t i = 0; i < cubes.size(); ++i)
  {
    const auto position = fixtures::positionOf(cubes[i]);
    SCOPED_TRACE(i);
    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
    EXPECT_GT(position.y, 0.0f);
    dropped += position.y < startHeights[i] - 0.5f ? 1 : 0;
  }
  EXPECT_GE(dropped, 2);
}
