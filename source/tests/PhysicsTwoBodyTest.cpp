#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "PhysicsTestHelpers.h"

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;
}

namespace {
  // A linear congruential generator, so the pile below comes out the same on every platform; the standard
  // distributions are free to differ between library implementations.
  class PileRandom {
  public:
    explicit PileRandom(const std::uint32_t seed) : m_state(seed) {}

    float between(const float low, const float high)
    {
      m_state = m_state * 1664525u + 1013904223u;
      return low + (high - low) * static_cast<float>(static_cast<double>(m_state) / 4294967296.0);
    }

  private:
    std::uint32_t m_state;
  };
}

namespace {
  struct PileOutcome {
    float fastestSideways;
    float shallowestClearance;
    size_t overGround;
    size_t bodyCount;
  };

  struct Pile {
    std::vector<std::shared_ptr<RigidBody>> bodies;
    // The radius of the largest sphere inside each body: its center cannot rest closer than this to the ground.
    std::vector<float> insideRadii;
  };

  Pile buildPile(const fixtures::Scene& scene, const std::uint32_t seed)
  {
    // Tower Collapse of the default project in miniature: a grid of spheres and turned boxes of random sizes, each as
    // heavy as its volume, so the pile they fall into mixes masses a hundred times apart.
    PileRandom random(seed);
    Pile pile;
    auto& bodies = pile.bodies;
    auto& insideRadii = pile.insideRadii;
    for (int level = 0; level < 4; ++level)
    {
      for (int row = 0; row < 4; ++row)
      {
        for (int column = 0; column < 4; ++column)
        {
          const float x = static_cast<float>(row) * 5.0f + random.between(-0.75f, 0.75f) - 7.5f;
          const float y = static_cast<float>(level) * 5.0f + 6.0f;
          const float z = static_cast<float>(column) * 5.0f + random.between(-0.75f, 0.75f) - 7.5f;

          if (random.between(0.0f, 1.0f) < 0.5f)
          {
            const float radius = random.between(0.25f, 1.5f);
            const auto sphere = addObject(scene, "Sphere", { x, y, z }, glm::vec3(radius));
            fixtures::addSphereCollider(sphere, 1.0f);
            bodies.push_back(addBody(sphere, true));
            bodies.back()->setMass(4.0f / 3.0f * glm::pi<float>() * radius * radius * radius);
            insideRadii.push_back(radius);
            continue;
          }

          const glm::vec3 scale{ random.between(0.25f, 1.5f), random.between(0.25f, 1.5f), random.between(0.25f, 1.5f) };
          const glm::vec3 rotation{ random.between(0, 360), random.between(0, 360), random.between(0, 360) };
          const auto box = addObject(scene, "Box", { x, y, z }, scale);
          transformOf(box)->setRotation(rotation);
          fixtures::addBoxCollider(box);
          bodies.push_back(addBody(box, true));
          bodies.back()->setMass(8.0f * scale.x * scale.y * scale.z);
          insideRadii.push_back(std::min({ scale.x, scale.y, scale.z }));
        }
      }
    }

    return pile;
  }

  // How far the lowest center is above the ground top (at 1) beyond its inside radius: negative means sunk into it.
  // A sphere has no rolling resistance, so one knocked hard enough rolls off the ground's edge (at 100) within the run
  // and falls; that is not sinking, and which spheres roll that far differs between platforms' rounding. Those are
  // left out, and counted in overGround.
  void measureClearance(const Pile& pile, PileOutcome& outcome)
  {
    for (size_t index = 0; index < pile.bodies.size(); ++index)
    {
      const auto position =
        pile.bodies[index]->getOwner()->getComponent<Transform>(ComponentType::transform)->getPosition();
      if (std::fabs(position.x) > 100.0f || std::fabs(position.z) > 100.0f)
      {
        continue;
      }

      ++outcome.overGround;
      outcome.shallowestClearance =
        std::min(outcome.shallowestClearance, position.y - 1.0f - pile.insideRadii[index]);
    }
  }

  PileOutcome settlePile(const std::uint32_t seed)
  {
    const auto scene = makeScene();

    const auto ground = addObject(scene, "Ground", { 0, -9, 0 }, { 100, 10, 100 });
    fixtures::addBoxCollider(ground);

    const auto pile = buildPile(scene, seed);

    // The server's tick, for six seconds: long enough for the whole grid to land and the pile to settle.
    constexpr float serverDt = 1.0f / 50.0f;
    CollisionSystem collisionSystem;

    PileOutcome outcome{ 0.0f, std::numeric_limits<float>::max(), 0, pile.bodies.size() };
    for (int tick = 0; tick < 300; ++tick)
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, serverDt);
      collisionSystem.fixedUpdate(*scene.objectManager, serverDt);

      for (const auto& body : pile.bodies)
      {
        const auto velocity = body->getVelocity();
        outcome.fastestSideways =
          std::max(outcome.fastestSideways, glm::length(glm::vec2(velocity.x, velocity.z)) / serverDt);
      }
    }

    measureClearance(pile, outcome);

    return outcome;
  }

  class PileOfUnevenBodies : public testing::TestWithParam<std::uint32_t> {};
}

TEST_P(PileOfUnevenBodies, SettlesWithoutFlingingAnyOfThemOut)
{
  const auto outcome = settlePile(GetParam());

  // Positive control: most of the pile is still over the ground, so the clearance below covers it.
  EXPECT_GE(outcome.overGround, outcome.bodyCount * 3 / 4);

  // Units per second. The bodies land at up to 35 falling straight down, and knock each other sideways at up to
  // about 43 in these layouts, so the bound leaves a margin of a quarter for the rounding of other platforms. A
  // heavy body used to squeeze a light one out at hundreds, and spin it to thousands of degrees a second, which
  // flung it further still.
  EXPECT_LT(outcome.fastestSideways, 60.0f);

  // Positive control: the pile does knock bodies sideways, so the bound above is measuring something.
  EXPECT_GT(outcome.fastestSideways, 1.0f);

  // No body sank into the ground. There is no upper bound: how tall the pile ends up depends on how the
  // bodies happen to fall, which differs between compilers.
  EXPECT_GT(outcome.shallowestClearance, -0.05f);
}

INSTANTIATE_TEST_SUITE_P(FirstThirty, PileOfUnevenBodies, testing::Range(1u, 31u));

// Layouts that knocked a body sideways at over a thousand units a second, or several hundred, before a response was
// sized against the part of a body that its support takes.
INSTANTIATE_TEST_SUITE_P(Flung, PileOfUnevenBodies, testing::Values(36u, 52u, 57u, 58u, 61u, 64u, 66u, 133u, 137u,
                                                                    237u, 331u, 333u, 358u));

TEST(PhysicsIntegration, ABodyIsIntegratedOnceEvenWhenItsChildInheritsIt)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", { 0, 0, 0 });
  const auto body = addBody(parent, true);

  auto child = std::make_shared<Object>("Child");
  child->setParent(parent);
  scene.objectManager->addObject(child);

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // getComponent walks to the parent for a rigid body, so a child would otherwise be integrated with its
  // parent's body and double every force on it - once per descendant.
  EXPECT_NEAR(body->getVelocity().y, 2.0f * gravityPerTick, 1e-5f);
  EXPECT_NEAR(transformOf(parent)->getPosition().y, gravityPerTick, 1e-5f);
}

TEST(PhysicsIntegration, ARotationTakesTheTimestepWhereAMoveDoesNot)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Spinning", { 0, 0, 0 });
  const auto body = addBody(object, false);

  body->setFriction(0.0f);
  body->setVelocity({ 1, 0, 0 });
  body->setAngularVelocity({ 0, 10, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // The asymmetry the top of this file records, asserted rather than described: the position moves by
  // the whole velocity while the rotation moves by the angular velocity times dt. Anyone who "fixed"
  // one of the two to match the other would break every tuned value in the project.
  expectNear("position", transformOf(object)->getPosition(), { 1, 0, 0 });
  expectNear("rotation", transformOf(object)->getRotation(), { 0, 10.0f * dt, 0 });

  // And the spin is damped a percent per tick afterwards, so a body left alone stops turning.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 9.9f, 0 });
}

TEST(PhysicsIntegration, SpinTurnsABodyAboutTheWorldAxisEvenWhenItIsAlreadyTurned)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Tilted", { 0, 0, 0 });
  const auto body = addBody(object, false);

  // Turned half way round and tilted 20 degrees, so its up axis leans toward -z. Spin about world +x
  // leans it back toward +z: ten degrees in one tick at 100 degrees per second.
  const glm::vec3 tilted{ 20, 180, 0 };
  expectNear("initial up", localUpOf(tilted), { 0, glm::cos(glm::radians(20.0f)), -glm::sin(glm::radians(20.0f)) });

  transformOf(object)->setRotation(tilted);
  body->setAngularVelocity({ 100, 0, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Adding the spin to the x angle turns about the body's own x axis instead, which the half turn has
  // reversed - the tilt grows to 30 degrees, and a torque meant to right the body topples it.
  expectNear("up", localUpOf(transformOf(object)->getRotation()),
             { 0, glm::cos(glm::radians(10.0f)), -glm::sin(glm::radians(10.0f)) });
}

TEST(PhysicsIntegration, ASpinningChildOfATurnedParentTurnsOnlyByItsOwnSpin)
{
  const auto scene = makeScene();
  const auto parent = addObject(scene, "Parent", { 5, 0, 0 });
  transformOf(parent)->setRotation({ 0, 30, 0 });

  const auto child = addChildObject(scene, "Child", parent);
  const auto body = addBody(child, false);
  body->setVelocity({ 1, 0, 0 });
  body->setAngularVelocity({ 0, 10, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // The turned world rotation written back as the local one adds the parent's 30 degrees again every tick.
  fixtures::expectNear("local rotation", transformOf(child)->getLocalRotation(), { 0, 10.0f * dt, 0 }, 1e-3f);
  fixtures::expectNear("rotation", transformOf(child)->getRotation(), { 0, 30.0f + 10.0f * dt, 0 }, 1e-3f);

  // The world-space move is carried into the parent's frame, turned 30 degrees about y.
  expectNear("local position", transformOf(child)->getLocalPosition(),
             { glm::cos(glm::radians(30.0f)), 0, glm::sin(glm::radians(30.0f)) });
  expectNear("position", transformOf(child)->getPosition(), { 6, 0, 0 });
}

TEST(PhysicsIntegration, ASpinTooSlowToSeeIsKeptButDoesNotRewriteTheRotation)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Drifting", { 0, 0, 0 });
  const auto body = addBody(object, false);

  const glm::vec3 rotation{ 10, 20, 30 };
  transformOf(object)->setRotation(rotation);
  body->setAngularVelocity({ 0, 0.005f, 0 });

  PhysicsSystem::fixedUpdate(*scene.objectManager, dt);

  // Compared exactly: turning by a spin this small only rewrites the angles by float noise, every tick.
  EXPECT_EQ(transformOf(object)->getRotation().x, rotation.x);
  EXPECT_EQ(transformOf(object)->getRotation().y, rotation.y);
  EXPECT_EQ(transformOf(object)->getRotation().z, rotation.z);

  // Still there for a torque to build on, so a slow start to tipping is not lost - just damped as usual.
  expectNear("angular velocity", body->getAngularVelocity(), { 0, 0.005f * 0.99f, 0 });
}
