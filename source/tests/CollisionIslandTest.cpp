#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  constexpr float dt = 1.0f / 50.0f;
  constexpr float pileSpacing = 40.0f;

  struct BodyState {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
  };

  struct Pile {
    std::vector<std::shared_ptr<Object>> bodies;
  };

  BodyState stateOf(const std::shared_ptr<Object>& object)
  {
    const auto body = object->getComponent<RigidBody>(ComponentType::rigidBody);

    return { transformOf(object)->getPosition(), transformOf(object)->getRotation(), body->getVelocity(),
             body->getAngularVelocity() };
  }

  void expectSameState(const BodyState& actual, const BodyState& expected, const float tolerance)
  {
    // Positions sit at x up to a few hundred, where float spacing alone is a few 1e-5.
    fixtures::expectNear("position", actual.position, expected.position, 10.0f * tolerance);
    fixtures::expectNear("rotation", actual.rotation, expected.rotation, tolerance);
    fixtures::expectNear("velocity", actual.velocity, expected.velocity, tolerance);
    fixtures::expectNear("angular velocity", actual.angularVelocity, expected.angularVelocity, tolerance);
  }

  std::shared_ptr<Object> addDynamicBox(const fixtures::Scene& scene, const glm::vec3& position)
  {
    const auto box = addObject(scene, "Box", position);
    physicsFixtures::addBody(box, true);
    fixtures::addBoxCollider(box);

    return box;
  }

  bool metDuringRun(const std::vector<CollisionPair>& seen, const std::shared_ptr<Object>& a,
                    const std::shared_ptr<Object>& b)
  {
    return std::ranges::find(seen, CollisionPair::make(a->getUUID(), b->getUUID())) != seen.end();
  }

  // A stack of boxes dropped on it; the variant changes heights and offsets so piles differ.
  Pile addBoxes(const fixtures::Scene& scene, const int variant, const float originX)
  {
    const float lift = 0.05f * static_cast<float>(variant % 4);
    const float shift = 0.15f * static_cast<float>(variant % 3);

    Pile pile;
    pile.bodies.push_back(addDynamicBox(scene, { originX, 2.2f + lift, 0 }));
    pile.bodies.push_back(addDynamicBox(scene, { originX + shift, 4.6f + lift, 0.1f }));
    if (variant % 2 == 0)
    {
      pile.bodies.push_back(addDynamicBox(scene, { originX - 0.2f, 7.0f + lift, 0 }));
    }

    return pile;
  }

  Pile addPile(const fixtures::Scene& scene, const int variant, const float originX)
  {
    const auto floor = addObject(scene, "Floor", { originX, 0, 0 }, { 10, 1, 10 });
    fixtures::addBoxCollider(floor);

    return addBoxes(scene, variant, originX);
  }

  enum class FloorShape { box, hull };

  // Piles spread along x on one static floor built fresh here, so its lazy collider caches start cold.
  std::vector<std::vector<BodyState>> simulateOnSharedFloor(const std::vector<int>& variants, const float spacing,
                                                            const FloorShape shape)
  {
    const auto scene = makeScene();

    const float length = spacing * static_cast<float>(variants.size());
    const auto floor = addObject(scene, "Floor", { length / 2, 0, 0 }, { length + 20, 1, 20 });
    if (shape == FloorShape::box)
    {
      fixtures::addBoxCollider(floor);
    }
    else
    {
      floor->addComponent(std::make_shared<ConvexMeshCollider>());
    }

    std::vector<Pile> piles;
    for (size_t i = 0; i < variants.size(); ++i)
    {
      piles.push_back(addBoxes(scene, variants[i], spacing * static_cast<float>(i)));
    }

    CollisionSystem collisionSystem;
    std::vector<CollisionPair> seen;
    for (int tick = 0; tick < 120; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
      seen.insert(seen.end(), collisionSystem.getCollisionEnters().begin(),
                  collisionSystem.getCollisionEnters().end());
    }

    std::vector<std::vector<BodyState>> result;
    for (const auto& pile : piles)
    {
      EXPECT_TRUE(metDuringRun(seen, pile.bodies[0], pile.bodies[1]));
      EXPECT_GT(transformOf(pile.bodies[0])->getPosition().y, 1.9f);

      std::vector<BodyState> states;
      for (const auto& body : pile.bodies)
      {
        auto state = stateOf(body);
        state.position.x -= spacing * static_cast<float>(result.size());
        states.push_back(state);
      }

      result.push_back(std::move(states));
    }

    return result;
  }

  // Runs the given piles in one scene, each at its own x, and returns every body's final state pile by pile.
  // Also checks that the stacked boxes touched each other and ended up resting above their floor.
  std::vector<std::vector<BodyState>> simulate(const std::vector<int>& variants, const float spacing)
  {
    const auto scene = makeScene();

    std::vector<Pile> piles;
    for (size_t i = 0; i < variants.size(); ++i)
    {
      piles.push_back(addPile(scene, variants[i], spacing * static_cast<float>(i)));
    }

    CollisionSystem collisionSystem;
    std::vector<CollisionPair> seen;
    for (int tick = 0; tick < 120; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
      seen.insert(seen.end(), collisionSystem.getCollisionEnters().begin(),
                  collisionSystem.getCollisionEnters().end());
    }

    std::vector<std::vector<BodyState>> result;
    for (const auto& pile : piles)
    {
      EXPECT_TRUE(metDuringRun(seen, pile.bodies[0], pile.bodies[1]));
      EXPECT_GT(transformOf(pile.bodies[0])->getPosition().y, 1.9f);

      std::vector<BodyState> states;
      for (const auto& body : pile.bodies)
      {
        auto state = stateOf(body);
        state.position.x -= spacing * static_cast<float>(result.size());
        states.push_back(state);
      }

      result.push_back(std::move(states));
    }

    return result;
  }

  // A compound body whose two child colliders each rest on a different dynamic body; returns the final states.
  std::vector<BodyState> compoundOverTwoSupports()
  {
    const auto scene = makeScene();

    const auto floor = addObject(scene, "Floor", { 0, 0, 0 }, { 20, 1, 20 });
    fixtures::addBoxCollider(floor);

    const auto left = addDynamicBox(scene, { -2, 2.0f, 0 });
    const auto right = addDynamicBox(scene, { 2, 2.0f, 0 });

    const auto compound = addObject(scene, "Compound", { 0, 4.1f, 0 });
    physicsFixtures::addBody(compound, true);
    std::vector<std::shared_ptr<Object>> parts;
    for (const float x : { -2.0f, 2.0f })
    {
      const auto part = addChildObject(scene, "Part", compound);
      transformOf(part)->setPosition({ x, 0, 0 });
      fixtures::addBoxCollider(part);
      parts.push_back(part);
    }

    CollisionSystem collisionSystem;
    std::vector<CollisionPair> seen;
    for (int tick = 0; tick < 60; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisionSystem.fixedUpdate(*scene.objectManager, dt);
      seen.insert(seen.end(), collisionSystem.getCollisionEnters().begin(),
                  collisionSystem.getCollisionEnters().end());
    }

    EXPECT_TRUE(metDuringRun(seen, parts[0], left));
    EXPECT_TRUE(metDuringRun(seen, parts[1], right));
    EXPECT_GT(transformOf(compound)->getPosition().y, 3.9f);

    return { stateOf(compound), stateOf(left), stateOf(right) };
  }
}

TEST(CollisionIslandTest, SeparatePilesResolveAsTheyWouldAlone)
{
  const std::vector<int> variants{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

  const auto together = simulate(variants, pileSpacing);
  ASSERT_EQ(together.size(), variants.size());

  for (size_t i = 0; i < variants.size(); ++i)
  {
    const auto alone = simulate({ variants[i] }, pileSpacing);
    ASSERT_EQ(alone.size(), 1u);
    ASSERT_EQ(together[i].size(), alone[0].size());

    for (size_t b = 0; b < alone[0].size(); ++b)
    {
      SCOPED_TRACE("pile " + std::to_string(i) + " body " + std::to_string(b));
      expectSameState(together[i][b], alone[0][b], 1e-4f);
    }
  }
}

TEST(CollisionIslandTest, ACompoundBodyTouchingTwoSeparateBodiesStaysDeterministic)
{
  // A race between islands is only probabilistically detectable, so the scene is run repeatedly.
  const auto first = compoundOverTwoSupports();
  ASSERT_EQ(first.size(), 3u);

  for (int run = 1; run < 10; ++run)
  {
    const auto again = compoundOverTwoSupports();
    ASSERT_EQ(again.size(), first.size());

    for (size_t b = 0; b < first.size(); ++b)
    {
      SCOPED_TRACE("run " + std::to_string(run) + " body " + std::to_string(b));
      expectSameState(again[b], first[b], 1e-6f);
    }
  }
}

class SharedFloorIslandTest : public testing::TestWithParam<FloorShape> {};

TEST_P(SharedFloorIslandTest, PilesOnOneStaticFloorResolveAsTheyWouldOnTheirOwn)
{
  // Whether islands race to fill a shared collider's caches is only probabilistically detectable here; the
  // sanitizer preset is the stronger check.
  const std::vector<int> variants{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  const auto shape = GetParam();

  const auto first = simulateOnSharedFloor(variants, pileSpacing, shape);
  ASSERT_EQ(first.size(), variants.size());

  for (size_t i = 0; i < variants.size(); ++i)
  {
    const auto alone = simulateOnSharedFloor({ variants[i] }, pileSpacing, shape);
    ASSERT_EQ(alone.size(), 1u);
    ASSERT_EQ(first[i].size(), alone[0].size());

    for (size_t b = 0; b < alone[0].size(); ++b)
    {
      SCOPED_TRACE("pile " + std::to_string(i) + " body " + std::to_string(b));
      expectSameState(first[i][b], alone[0][b], 1e-4f);
    }
  }

  for (int run = 1; run < 5; ++run)
  {
    const auto again = simulateOnSharedFloor(variants, pileSpacing, shape);
    ASSERT_EQ(again.size(), first.size());

    for (size_t i = 0; i < first.size(); ++i)
    {
      for (size_t b = 0; b < first[i].size(); ++b)
      {
        SCOPED_TRACE("run " + std::to_string(run) + " pile " + std::to_string(i) + " body " + std::to_string(b));
        expectSameState(again[i][b], first[i][b], 1e-6f);
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Floors, SharedFloorIslandTest, testing::Values(FloorShape::box, FloorShape::hull));
