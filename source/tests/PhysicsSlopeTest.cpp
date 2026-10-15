#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <memory>

namespace {
  using namespace physicsFixtures;

  constexpr float slopeDegrees = 5.71059f;
  constexpr int ticks = 120;
  constexpr int settledBy = 20;
}

// The body lands flush, then creeps along the slope. Every tick from landing on, its center must stay one body
// half-size plus one support half-size above the support's center plane, never pushed through the support.
TEST(PhysicsSlope, ABoxStaysOnASlopedBoxSupport)
{
  fixtures::Scene scene;
  const auto support = fixtures::addObject(scene, "Slope", { 0, 0, 0 });
  fixtures::transformOf(support)->setScale({ 10, 1, 10 });
  fixtures::transformOf(support)->setRotation({ 0, 0, slopeDegrees });
  fixtures::addBoxCollider(support);

  const auto body = fixtures::addObject(scene, "Body", { 0, 5, 0 });
  fixtures::transformOf(body)->setRotation({ 10, 0, 5 });
  fixtures::addBoxCollider(body);
  addBody(body, true);

  const float angle = glm::radians(slopeDegrees);
  const glm::vec3 slopeNormal{ -std::sin(angle), std::cos(angle), 0.0f };

  CollisionSystem collisions;
  for (int tick = 0; tick < ticks; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisions.fixedUpdate(*scene.objectManager, dt);

    if (tick >= settledBy)
    {
      const float height = glm::dot(fixtures::transformOf(body)->getPosition(), slopeNormal);
      ASSERT_NEAR(height, 2.0f, 0.1f) << "tick " << tick;
    }
  }
}
