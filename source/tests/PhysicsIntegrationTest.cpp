#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "CollisionSystem.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"

#include <glm/vec3.hpp>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>

namespace {
  // The tick length the engine runs at. Every number below is derived from it rather than measured, so
  // a change to the timestep shows up as a failure that names the arithmetic rather than a mystery.
  constexpr float dt = 0.1f;

  // What integrate() adds to a body's velocity each tick under the default gravity. Note that integrate
  // moves by the velocity directly rather than by velocity * dt, so "velocity" is displacement per tick.
  constexpr float gravityPerTick = -9.81f * dt * 0.1f;

  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    return scene;
  }

  std::shared_ptr<Object> addObject(const Scene& scene, const std::string& name, const glm::vec3& position)
  {
    auto object = std::make_shared<Object>(name);
    scene.objectManager->addObject(object);
    object->getComponent<Transform>(ComponentType::transform)->setPosition(position);

    return object;
  }

  std::shared_ptr<RigidBody> addBody(const std::shared_ptr<Object>& object, const bool gravity)
  {
    auto body = std::make_shared<RigidBody>();
    object->addComponent(body);
    body->setDoGravity(gravity);

    return body;
  }

  std::shared_ptr<Transform> transformOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Transform>(ComponentType::transform);
  }

  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-4f;
    SCOPED_TRACE(what);

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

TEST(PhysicsIntegration, GravityAccumulatesInVelocityAndSquaresInDistance)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Falling", { 0, 0, 0 });
  const auto body = addBody(object, true);

  for (int tick = 0; tick < 3; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  // Velocity gains the same amount every tick, and the position gains the running total - so after three
  // ticks the body has moved 1+2+3 tick-steps rather than 3. Getting this wrong is the classic way a
  // physics change looks plausible and is not.
  EXPECT_NEAR(body->getVelocity().y, 3.0f * gravityPerTick, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 6.0f * gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, ABodyWithGravityOffDoesNotMoveOnItsOwn)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Static", { 1, 2, 3 });
  addBody(object, false);

  for (int tick = 0; tick < 10; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  }

  expectNear("position", transformOf(object)->getPosition(), { 1, 2, 3 });
}

TEST(PhysicsIntegration, FrictionBleedsHorizontalVelocityAndLeavesTheVerticalAlone)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Sliding", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setFriction(0.1f);
  body->setVelocity({ 1, 1, 1 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Friction is applied to the horizontal plane only, so a body sliding and falling loses the slide and
  // keeps the fall. Taking the vertical with it would make everything drift to a halt in mid-air.
  expectNear("velocity", body->getVelocity(), { 0.9f, 1.0f, 0.9f });
  expectNear("position", transformOf(object)->getPosition(), { 0.9f, 1.0f, 0.9f });
}

TEST(PhysicsIntegration, AQueuedForceIsAppliedOnceAndThenForgotten)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setFriction(0.0f);
  body->addPendingForce({ 0, 5, 0 }, transformOf(object)->getPosition());

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  EXPECT_NEAR(body->getVelocity().y, 5.0f, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 5.0f, 1e-5f);

  // A script queues a force for the tick, not forever. Draining is what stops one jump input from
  // accelerating the body every tick after it.
  EXPECT_TRUE(body->getPendingForces().empty());

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  EXPECT_NEAR(body->getVelocity().y, 5.0f, 1e-5f);
  EXPECT_NEAR(transformOf(object)->getPosition().y, 10.0f, 1e-5f);
}

TEST(PhysicsIntegration, AForceThroughTheCentreOfMassDoesNotSpinTheBody)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, transform->getPosition());

  expectNear("velocity", body->getVelocity(), { 1, 0, 0 });
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, 0 });
}

TEST(PhysicsIntegration, AForceOffTheCentreSpinsTheBodyThroughItsInertiaTensor)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Pushed", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const auto transform = transformOf(object);

  // Pushed along +x one unit above the centre. The impulse is r x F = (0,1,0) x (1,0,0) = (0,0,-1),
  // divided by the inertia tensor. At the default mass of 10 and unit scale the tensor is
  // (1/12) * 10 * 0.1 * (1 + 1) = 1/6 on every diagonal, so its inverse is 6.
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -6 });
}

TEST(PhysicsIntegration, AHeavierBodyIsHarderToSpin)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Heavy", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setMass(20.0f);

  const auto transform = transformOf(object);
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  // Twice the mass, twice the inertia, half the spin from the same impulse. Mass entering the tensor is
  // the part a refactor is most likely to drop, and nothing else in the engine would notice.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -3 });
}

TEST(PhysicsIntegration, AWiderBodyIsHarderToSpinAboutItsShortAxis)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Wide", { 0, 0, 0 });
  const auto body = addBody(object, false);

  transformOf(object)->setScale({ 3, 1, 1 });

  const auto transform = transformOf(object);
  PhysicsSystem::applyForce(*body, *transform, { 1, 0, 0 }, { 0, 1, 0 });

  // Izz takes width and height: (1/12) * 10 * 0.1 * (9 + 1) = 5/6, so the same impulse spins it at 1.2
  // rather than 6. The tensor has to see the object's scale, not just its mass.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0, -1.2f });
}

TEST(PhysicsIntegration, ACollisionAlongTheTranslationVectorCancelsTheVelocityIntoIt)
{
  const auto scene = makeScene();

  const auto falling = addObject(scene, "Falling", { 0, 0, 0 });
  const auto body = addBody(falling, false);
  const auto ground = addObject(scene, "Ground", { 0, -2, 0 });

  body->setVelocity({ 0, -1, 0 });

  // Pushed a quarter unit back up out of the ground, with the contact directly below the centre.
  PhysicsSystem::handleCollision(*body, ground, { 0, 0.25f, 0 }, { 0, -1, 0 });

  // The correction moves the body clear, and the impulse removes exactly the velocity that was driving
  // it into the surface - so it rests rather than accumulating downward speed against something solid.
  EXPECT_NEAR(transformOf(falling)->getPosition().y, 0.25f, 1e-5f);
  EXPECT_NEAR(body->getVelocity().y, 0.0f, 1e-5f);
}

TEST(PhysicsIntegration, ABodyFallingOntoAStaticBoxComesToRestOnTopOfIt)
{
  const auto scene = makeScene();

  const auto ground = addObject(scene, "Ground", { 0, 0, 0 });
  ground->addComponent(std::make_shared<BoxCollider>());

  const auto falling = addObject(scene, "Falling", { 0, 5, 0 });
  falling->addComponent(std::make_shared<BoxCollider>());
  addBody(falling, true);

  CollisionSystem collisionSystem;

  float lowest = std::numeric_limits<float>::max();
  float highest = std::numeric_limits<float>::lowest();

  for (int tick = 0; tick < 60; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager);

    // Sampled only once it has had every chance to settle, so the fall itself is not measured.
    if (tick >= 50)
    {
      const float height = transformOf(falling)->getPosition().y;
      lowest = std::min(lowest, height);
      highest = std::max(highest, height);
    }
  }

  // Two unit boxes, so resting on top means their centres are two apart. The bounds are deliberately
  // loose rather than pinned to exactly 2: the body sinks by one gravity step between the integrate
  // that moves it and the collision pass that pushes it back out, and where EPA puts the contact point
  // decides whether the response also tips it, which raises the resting centre. What must hold is that
  // it neither sinks through the box below nor gets thrown off it.
  EXPECT_GT(lowest, 1.5f);
  EXPECT_LT(highest, 3.0f);
}

TEST(PhysicsIntegration, ABodyIsIntegratedOnceEvenWhenItsChildInheritsIt)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", { 0, 0, 0 });
  const auto body = addBody(parent, true);

  auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  scene.objectManager->addObject(child);

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // getComponent walks to the parent for a rigid body, so a child would otherwise be integrated with its
  // parent's body and double every force on it - once per descendant.
  EXPECT_NEAR(body->getVelocity().y, gravityPerTick, 1e-5f);
  EXPECT_NEAR(transformOf(parent)->getPosition().y, gravityPerTick, 1e-5f);
}
