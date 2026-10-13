#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ConvexMeshFixtures.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <limits>
#include <memory>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // Sixty ticks, like the box tests: enough to land and settle without reaching the counter wrap.
  constexpr int ticks = 60;
  constexpr int settledFrom = 50;

  struct Settled
  {
    float lowestHeight = std::numeric_limits<float>::max();
    float highestHeight = std::numeric_limits<float>::lowest();
    float maxDrift = 0.0f;
    float maxSpin = 0.0f;
    float maxTilt = 0.0f;
  };

  // Runs the scene and measures the body once it has had every chance to settle.
  Settled run(const fixtures::Scene& scene, const std::shared_ptr<Object>& falling,
              const std::shared_ptr<RigidBody>& body)
  {
    CollisionSystem collisionSystem;
    const glm::vec3 start = transformOf(falling)->getPosition();

    Settled settled;
    for (int tick = 0; tick < ticks; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);

      if (tick >= settledFrom)
      {
        const glm::vec3 position = transformOf(falling)->getPosition();
        settled.lowestHeight = std::min(settled.lowestHeight, position.y);
        settled.highestHeight = std::max(settled.highestHeight, position.y);
        settled.maxDrift = std::max(settled.maxDrift, glm::length(glm::vec2(position.x - start.x,
                                                                           position.z - start.z)));
        settled.maxSpin = std::max(settled.maxSpin, glm::length(body->getAngularVelocity()));
        settled.maxTilt = std::max(settled.maxTilt, glm::length(transformOf(falling)->getRotation()));
      }
    }

    return settled;
  }

  void addGround(const fixtures::Scene& scene)
  {
    fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  }
}

TEST(ConvexMeshPhysics, ADefaultCubeHullDroppedOnAStaticBoxComesToRestOnTopOfItWithoutDriftingOrSpinning)
{
  const auto scene = makeScene();
  addGround(scene);

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<ConvexMeshCollider>());
  const auto body = addBody(falling, true);

  const auto settled = run(scene, falling, body);

  // Two unit cubes: resting on top puts their centers two apart, and falling five up had to travel to get
  // there, so a body that never landed (or sank in) is outside this band.
  EXPECT_GT(settled.lowestHeight, 1.8f);
  EXPECT_LT(settled.highestHeight, 2.2f);
  EXPECT_LT(settled.maxDrift, 0.05f);
  EXPECT_LT(settled.maxSpin, 0.05f);
  EXPECT_LT(settled.maxTilt, 1.0f);
}

TEST(ConvexMeshPhysics, ABoxDroppedOnAStaticHullRestsOnItToo)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<ConvexMeshCollider>());

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  fixtures::addBoxCollider(falling);
  const auto body = addBody(falling, true);

  const auto settled = run(scene, falling, body);

  EXPECT_GT(settled.lowestHeight, 1.8f);
  EXPECT_LT(settled.highestHeight, 2.2f);
  EXPECT_LT(settled.maxDrift, 0.05f);
  EXPECT_LT(settled.maxSpin, 0.05f);
}

TEST(ConvexMeshPhysics, ATetrahedronHullRestingOnItsBaseStaysAtRest)
{
  const auto scene = makeScene();
  addGround(scene);

  // Placed with its base on the ground top (y = 1), the base triangle's middle under the center.
  const auto resting = addObject(scene, "Tetrahedron", { 0, 2.0f, 0 });
  const auto hull = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(hull->setHull(convexFixtures::tetrahedron()));
  resting->addComponent(hull);
  const auto body = addBody(resting, true);

  const auto settled = run(scene, resting, body);

  EXPECT_GT(settled.lowestHeight, 1.8f);
  EXPECT_LT(settled.highestHeight, 2.2f);
  EXPECT_LT(settled.maxDrift, 0.05f);
  EXPECT_LT(settled.maxSpin, 0.05f);
  EXPECT_LT(settled.maxTilt, 1.0f);
}

TEST(ConvexMeshPhysics, AHullWithNothingBelowItKeepsFalling)
{
  const auto scene = makeScene();

  // The control for the tests above: without a support the same body does not stop where they say it does.
  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<ConvexMeshCollider>());
  const auto body = addBody(falling, true);

  const auto settled = run(scene, falling, body);

  EXPECT_LT(settled.highestHeight, 1.0f);
}
