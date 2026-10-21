#include <gtest/gtest.h>

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

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace templateTest;

namespace
{
  const std::filesystem::path sampleFolder = sourceRoot / "templates" / "physics-playground";
  constexpr float dt = 1.0f / 50.0f;

  std::shared_ptr<Object> objectNamedExactly(const ObjectManager& objectManager, const std::string& name)
  {
    for (const auto& object : objectManager.getAllObjects())
    {
      if (object->getName() == name)
      {
        return object;
      }
    }

    return nullptr;
  }

  float tiltDegrees(const std::shared_ptr<Object>& object)
  {
    const auto up = fixtures::transformOf(object)->getOrientation() * glm::vec3(0, 1, 0);
    return glm::degrees(std::acos(std::clamp(up.y, -1.0f, 1.0f)));
  }

  std::vector<std::shared_ptr<Object>> pyramidCubes(const ObjectManager& objectManager)
  {
    std::vector<std::shared_ptr<Object>> cubes;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (object->getName() == "Pyramid Cube")
      {
        cubes.push_back(object);
      }
    }

    return cubes;
  }

  std::shared_ptr<Object> highest(const std::vector<std::shared_ptr<Object>>& objects)
  {
    return *std::ranges::max_element(objects, {}, [](const std::shared_ptr<Object>& object) {
      return fixtures::positionOf(object).y;
    });
  }

  void tick(const ObjectManager& objectManager, const int count)
  {
    CollisionSystem collisionSystem;
    for (int i = 0; i < count; ++i)
    {
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
    }
  }

  int movedBodies(const ObjectManager& objectManager, const std::vector<glm::vec3>& before)
  {
    int moved = 0;
    const auto& objects = objectManager.getAllObjects();
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
      const bool dynamic = objects[i]->getComponent<RigidBody>(ComponentType::rigidBody) != nullptr;
      moved += dynamic && glm::distance(fixtures::positionOf(objects[i]), before[i]) > 0.05f ? 1 : 0;
    }

    return moved;
  }
}

TEST(PhysicsPlaygroundSamplePhysics, SixHundredTicksStayFiniteAndAboveTheVoid)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  tick(objectManager, 600);

  for (const auto& object : objectManager.getAllObjects())
  {
    SCOPED_TRACE(object->getName());
    const auto position = fixtures::positionOf(object);
    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
    EXPECT_GT(position.y, -50.0f);
  }
}

TEST(PhysicsPlaygroundSamplePhysics, TheDominoChainFallsToTheLastDomino)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto first = objectNamedExactly(objectManager, "Domino 1");
  const auto last = objectNamedExactly(objectManager, "Domino 30");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(last, nullptr);

  // The first starts leaning 15 degrees and the last stands upright, so the final bound is not trivial.
  EXPECT_NEAR(tiltDegrees(first), 15.0f, 0.1f);
  EXPECT_LT(tiltDegrees(last), 1.0f);

  tick(objectManager, 600);

  EXPECT_GT(tiltDegrees(first), 45.0f);
  EXPECT_GT(tiltDegrees(last), 45.0f);
}

TEST(PhysicsPlaygroundSamplePhysics, ThePyramidStandsAndTheRunMovesThings)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  const auto cubes = pyramidCubes(objectManager);
  ASSERT_EQ(cubes.size(), 30u);
  const auto top = highest(cubes);
  const float startHeight = fixtures::positionOf(top).y;
  EXPECT_NEAR(startHeight, 3.55f, 1e-4f);

  std::vector<glm::vec3> before;
  for (const auto& object : objectManager.getAllObjects())
  {
    before.push_back(fixtures::positionOf(object));
  }

  tick(objectManager, 600);

  EXPECT_NEAR(fixtures::positionOf(top).y, startHeight, 0.1f);
  EXPECT_GE(movedBodies(objectManager, before), 3);
}
