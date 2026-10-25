#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsPlaygroundTestHelpers.h"
#include "PhysicsSystem.h"
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

// The four tests check the same 600-tick run, and the run dominates the suite's time, so it happens once per suite.
class PhysicsPlaygroundSamplePhysics : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    s_simulated = false;
    s_farthestTravel = 0.0f;
    s_loaded = std::make_unique<LoadedTemplate>(sampleFolder);
    if (!s_loaded->loaded)
    {
      return;
    }

    const auto& objectManager = *s_loaded->sceneManager.getCurrentScene()->getObjectManager();

    s_first = objectNamedExactly(objectManager, "Domino 1");
    s_second = objectNamedExactly(objectManager, "Domino 2");
    if (s_first != nullptr && s_second != nullptr)
    {
      s_firstStart = fixtures::positionOf(s_first);
      s_towardSecond = glm::normalize(glm::vec2(fixtures::positionOf(s_second).x - s_firstStart.x,
                                                fixtures::positionOf(s_second).z - s_firstStart.z));
      s_firstTiltBefore = tiltDegrees(s_first);
    }

    s_last = objectNamedExactly(objectManager, "Domino 30");
    if (s_last != nullptr)
    {
      s_lastTiltBefore = tiltDegrees(s_last);
    }

    s_sphere = objectNamedExactly(objectManager, "Domino Sphere");
    if (s_sphere != nullptr)
    {
      s_sphereStart = fixtures::positionOf(s_sphere);
    }

    s_cubes = pyramidCubes(objectManager);
    if (!s_cubes.empty())
    {
      s_top = highest(s_cubes);
      s_topStartHeight = fixtures::positionOf(s_top).y;
    }

    s_feeder = objectNamedExactly(objectManager, "Cradle Feeder Sphere");
    s_far = objectNamedExactly(objectManager, "Cradle Sphere 7");
    if (s_feeder != nullptr)
    {
      s_feederStart = fixtures::positionOf(s_feeder);
    }
    if (s_far != nullptr)
    {
      s_farStart = fixtures::positionOf(s_far);
    }

    CollisionSystem collisionSystem;
    for (int step = 0; step < 600; ++step)
    {
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
      if (s_far != nullptr)
      {
        s_farthestTravel = std::max(s_farthestTravel, planeTravel(s_far, s_farStart));
      }
    }

    s_simulated = true;
  }

  static void TearDownTestSuite()
  {
    s_first.reset();
    s_second.reset();
    s_last.reset();
    s_sphere.reset();
    s_cubes.clear();
    s_top.reset();
    s_feeder.reset();
    s_far.reset();
    s_loaded.reset();
    s_simulated = false;
  }

  static const ObjectManager& objectManager()
  {
    return *s_loaded->sceneManager.getCurrentScene()->getObjectManager();
  }

  [[nodiscard]] static bool isSimulated()
  {
    return s_simulated;
  }

  inline static std::unique_ptr<LoadedTemplate> s_loaded;
  // Set only once the run completes, so a setup that throws partway fails every test rather than letting one
  // pass on an unsimulated scene.
  inline static bool s_simulated = false;

  inline static std::shared_ptr<Object> s_first;
  inline static std::shared_ptr<Object> s_second;
  inline static glm::vec3 s_firstStart{ 0.0f };
  inline static glm::vec2 s_towardSecond{ 0.0f };
  inline static float s_firstTiltBefore = 0.0f;

  inline static std::shared_ptr<Object> s_last;
  inline static float s_lastTiltBefore = 0.0f;

  inline static std::shared_ptr<Object> s_sphere;
  inline static glm::vec3 s_sphereStart{ 0.0f };

  inline static std::vector<std::shared_ptr<Object>> s_cubes;
  inline static std::shared_ptr<Object> s_top;
  inline static float s_topStartHeight = 0.0f;

  inline static std::shared_ptr<Object> s_feeder;
  inline static std::shared_ptr<Object> s_far;
  inline static glm::vec3 s_feederStart{ 0.0f };
  inline static glm::vec3 s_farStart{ 0.0f };
  inline static float s_farthestTravel = 0.0f;
};

TEST_F(PhysicsPlaygroundSamplePhysics, SixHundredTicksStayFiniteAndAboveTheVoid)
{
  ASSERT_TRUE(isSimulated());

  for (const auto& object : objectManager().getAllObjects())
  {
    SCOPED_TRACE(object->getName());
    const auto position = fixtures::positionOf(object);
    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z));
    EXPECT_GT(position.y, -50.0f);
  }
}

TEST_F(PhysicsPlaygroundSamplePhysics, TheLeaningFirstDominoFallsTowardTheSecond)
{
  ASSERT_TRUE(isSimulated());
  ASSERT_NE(s_first, nullptr);
  ASSERT_NE(s_second, nullptr);

  EXPECT_NEAR(s_firstTiltBefore, 15.0f, 0.1f);

  EXPECT_GT(tiltDegrees(s_first), 45.0f);
  // Fallen toward the second rather than away from it, its center moved toward where the second stood.
  const auto moved = fixtures::positionOf(s_first) - s_firstStart;
  EXPECT_GT(glm::dot(glm::vec2(moved.x, moved.z), s_towardSecond), 0.3f);
}

TEST_F(PhysicsPlaygroundSamplePhysics, TheDominoChainFallsAndTheSphereIsKnockedOffTheLedge)
{
  ASSERT_TRUE(isSimulated());
  ASSERT_NE(s_last, nullptr);
  ASSERT_NE(s_sphere, nullptr);

  EXPECT_LT(s_lastTiltBefore, 1.0f);

  for (int index = 1; index <= 30; ++index)
  {
    const auto domino = objectNamedExactly(objectManager(), "Domino " + std::to_string(index));
    ASSERT_NE(domino, nullptr);
    EXPECT_GT(tiltDegrees(domino), 45.0f) << "Domino " << index << " is still standing";
  }
  EXPECT_GT(planeTravel(s_sphere, s_sphereStart), 1.0f);
}

TEST_F(PhysicsPlaygroundSamplePhysics, ThePyramidStandsWhileTheCradleSpheresMove)
{
  ASSERT_TRUE(isSimulated());

  ASSERT_EQ(s_cubes.size(), 30u);
  EXPECT_NEAR(s_topStartHeight, 3.55f, 1e-4f);

  ASSERT_NE(s_feeder, nullptr);
  ASSERT_NE(s_far, nullptr);

  EXPECT_NEAR(fixtures::positionOf(s_top).y, s_topStartHeight, 0.1f);
  EXPECT_GT(planeTravel(s_feeder, s_feederStart), 3.0f);
  // How far the impact carries the last sphere varies with each platform's rounding, so this pins only that
  // it reached the far end of the row.
  EXPECT_GT(s_farthestTravel, 0.1f);
}
