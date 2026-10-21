#include <gtest/gtest.h>

#include "PhysicsPlaygroundTestHelpers.h"
#include "TemplateTestHelpers.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace templateTest;
using namespace playgroundtest;

namespace
{
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

  float planeTravel(const std::shared_ptr<Object>& object, const glm::vec3& start)
  {
    const auto moved = fixtures::positionOf(object) - start;

    return glm::length(glm::vec2(moved.x, moved.z));
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

TEST(PhysicsPlaygroundSamplePhysics, TheDominoChainFallsAndTheSphereIsKnockedOffTheLedge)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto first = objectNamedExactly(objectManager, "Domino 1");
  const auto last = objectNamedExactly(objectManager, "Domino 30");
  const auto sphere = objectNamedExactly(objectManager, "Domino Sphere");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(last, nullptr);
  ASSERT_NE(sphere, nullptr);
  const auto sphereStart = fixtures::positionOf(sphere);

  // The first starts leaning 15 degrees and the last stands upright, so the final bounds are not trivial.
  EXPECT_NEAR(tiltDegrees(first), 15.0f, 0.1f);
  EXPECT_LT(tiltDegrees(last), 1.0f);

  tick(objectManager, 600);

  for (int index = 1; index <= 30; ++index)
  {
    const auto domino = objectNamedExactly(objectManager, "Domino " + std::to_string(index));
    ASSERT_NE(domino, nullptr);
    EXPECT_GT(tiltDegrees(domino), 45.0f) << "Domino " << index << " is still standing";
  }
  EXPECT_GT(planeTravel(sphere, sphereStart), 1.0f);
}

TEST(PhysicsPlaygroundSamplePhysics, ThePyramidStandsWhileTheCradleSpheresMove)
{
  LoadedTemplate loaded(sampleFolder);
  ASSERT_TRUE(loaded.loaded);
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  const auto cubes = pyramidCubes(objectManager);
  ASSERT_EQ(cubes.size(), 30u);
  const auto top = highest(cubes);
  const float startHeight = fixtures::positionOf(top).y;
  EXPECT_NEAR(startHeight, 3.55f, 1e-4f);

  const auto feeder = objectNamedExactly(objectManager, "Cradle Feeder Sphere");
  const auto far = objectNamedExactly(objectManager, "Cradle Sphere 7");
  ASSERT_NE(feeder, nullptr);
  ASSERT_NE(far, nullptr);
  const auto feederStart = fixtures::positionOf(feeder);
  const auto farStart = fixtures::positionOf(far);

  tick(objectManager, 600);

  EXPECT_NEAR(fixtures::positionOf(top).y, startHeight, 0.1f);
  EXPECT_GT(planeTravel(feeder, feederStart), 3.0f);
  EXPECT_GT(planeTravel(far, farStart), 0.5f);
}
