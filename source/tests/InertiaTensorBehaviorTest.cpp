#include <gtest/gtest.h>

#include "CapsuleMoments.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
  using namespace physicsFixtures;

  struct Dropped
  {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    CollisionSystem collisions;

    explicit Dropped(const bool hull)
    {
      fixtures::addBoxCollider(fixtures::addObject(scene, "Ground", { 0, 0, 0 }));

      object = fixtures::addObject(scene, "Falling", { 0, 3, 0 });
      fixtures::transformOf(object)->setRotation({ 20, 0, 30 });
      if (hull)
      {
        object->addComponent(std::make_shared<ConvexMeshCollider>());
      }
      else
      {
        fixtures::addBoxCollider(object);
      }
      body = addBody(object, true);
    }

    void tick()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisions.fixedUpdate(*scene.objectManager, dt);
    }
  };

  struct Ball
  {
    static constexpr float degrees = 5.71059f;

    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    CollisionSystem collisions;
    glm::vec3 downhill{ -std::cos(glm::radians(degrees)), -std::sin(glm::radians(degrees)), 0.0f };

    Ball()
    {
      const float angle = glm::radians(degrees);
      const auto slope = fixtures::addObject(scene, "Slope", { 0, 0, 0 });
      fixtures::transformOf(slope)->setScale({ 40, 1, 10 });
      fixtures::transformOf(slope)->setRotation({ 0, 0, degrees });
      fixtures::addBoxCollider(slope);

      object = fixtures::addObject(scene, "Ball", glm::vec3(-std::sin(angle), std::cos(angle), 0.0f) * 1.5f);
      fixtures::addSphereCollider(object, 0.5f);
      body = addBody(object, true);
      body->setFriction(0.5f);
    }

    void run(const int count)
    {
      for (int tick = 0; tick < count; ++tick)
      {
        PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
        collisions.fixedUpdate(*scene.objectManager, dt);
      }
    }

    [[nodiscard]] float speed() const
    {
      return glm::dot(body->getVelocity(), downhill);
    }
  };
}

// Guards parity of the two shapes, not the inertia change itself: a corner tumble amplifies float differences, so
// only the pose they come to rest in is compared.
TEST(InertiaTensorBehavior, ACubeHullComesToRestInTheSamePoseAsABox)
{
  Dropped box(false);
  Dropped hull(true);

  float tumble = 0.0f;
  for (int tick = 0; tick < 90; ++tick)
  {
    box.tick();
    hull.tick();

    tumble = std::max(tumble, glm::length(box.body->getAngularVelocity()));
  }

  EXPECT_LT(glm::distance(fixtures::positionOf(box.object), fixtures::positionOf(hull.object)), 1e-2f);
  const float alignment = std::fabs(glm::dot(fixtures::transformOf(box.object)->getOrientation(),
                                             fixtures::transformOf(hull.object)->getOrientation()));
  EXPECT_GT(alignment, 0.9999f);
  // Positive control: the corner landing does spin the box, so the match is not two bodies at rest.
  EXPECT_GT(tumble, 10.0f);
}

TEST(InertiaTensorBehavior, ASolidSphereRollsDownASlopeAtItsOwnAccelerationNotTheBoxOfItsScale)
{
  Ball ball;
  ball.run(5);
  const float before = ball.speed();
  ball.run(10);

  // Solid sphere: m a (1 + 2/5) = m g sin - R, where the rolling resistance R is at most c m g cos, c a tenth of
  // the friction of 0.5. Gravity adds 0.0981 a tick. The box of its scale would divide by 1 + 8/3 instead. The
  // resistance acts on a spin that friction and the per-tick spin damping (0.99) also move, so the acceleration is
  // bracketed by the fully resisted and the unresisted rate rather than pinned. The window is early because the
  // damping trims the acceleration more the longer the ball has rolled.
  const float angle = glm::radians(Ball::degrees);
  const float resisted = -gravityPerTick * (std::sin(angle) - 0.05f * std::cos(angle)) / 1.4f;
  const float unresisted = -gravityPerTick * std::sin(angle) / 1.4f;
  const float acceleration = (ball.speed() - before) / 10.0f;

  EXPECT_GT(acceleration, 0.9f * resisted);
  // A ball that lost its rolling resistance would reach about 0.97 of the unresisted rate over this window.
  EXPECT_LT(acceleration, 0.95f * unresisted);
}

TEST(InertiaTensorBehavior, AnOffCenterPushSpinsASphereFiveThirdsAsFastAsTheBoxOfItsScale)
{
  const auto scene = fixtures::makeScene();
  const auto object = fixtures::addObject(scene, "Ball", { 0, 0, 0 });
  fixtures::addSphereCollider(object, 1.0f);
  const auto body = addBody(object, false);

  PhysicsSystem::applyVelocityChange(*body, *fixtures::transformOf(object), { 1, 0, 0 }, { 0, 1, 0 }, dt);

  // The box of unit scale gives 2/3 and a spin of 1.5; the solid sphere's 2/5 gives 2.5.
  expectNear("spin per tick", spinPerTickOf(*body), { 0, 0, -2.5f });
}

TEST(InertiaTensorBehavior, ACapsuleSpinsFasterAboutItsAxisThanAcrossIt)
{
  const auto scene = fixtures::makeScene();
  const auto moments = capsuleMoments::textbook(0.5f, 1.0f);

  // Lying along x. A push along z one half above the center turns it about its own axis, x.
  const auto lengthwise = fixtures::addObject(scene, "Lengthwise", { 0, 0, 0 });
  fixtures::transformOf(lengthwise)->setRotation({ 0, 0, 90 });
  fixtures::addCapsuleCollider(lengthwise, 0.5f, 2.0f);
  const auto lengthwiseBody = addBody(lengthwise, false);
  PhysicsSystem::applyVelocityChange(*lengthwiseBody, *fixtures::transformOf(lengthwise), { 0, 0, 1 }, { 0, 0.5f, 0 }, dt);
  EXPECT_NEAR(spinPerTickOf(*lengthwiseBody).x, 0.5f / moments.aboutAxis, 1e-3f);

  // The same push along x turns it across its axis, about z.
  const auto crosswise = fixtures::addObject(scene, "Crosswise", { 0, 0, 0 });
  fixtures::transformOf(crosswise)->setRotation({ 0, 0, 90 });
  fixtures::addCapsuleCollider(crosswise, 0.5f, 2.0f);
  const auto crosswiseBody = addBody(crosswise, false);
  PhysicsSystem::applyVelocityChange(*crosswiseBody, *fixtures::transformOf(crosswise), { 1, 0, 0 }, { 0, 0.5f, 0 }, dt);
  EXPECT_NEAR(spinPerTickOf(*crosswiseBody).z, -0.5f / moments.acrossAxis, 1e-3f);
}
