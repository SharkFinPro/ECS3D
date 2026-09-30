#include <gtest/gtest.h>

#include "TestScene.h"
#include "CollisionSystem.h"
#include "PhysicsSystem.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Collider.h"

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
  constexpr float dt = 1.0f / 50.0f;

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    CollisionSystem collisions;
    std::vector<std::shared_ptr<Object>> objects;
    std::shared_ptr<Object> ground;

    World(const BroadPhaseMode mode, const bool refresh)
    {
      collisions.setBroadPhaseMode(mode);
      collisions.setContactRefreshEnabled(refresh);

      ground = fixtures::addObject(scene, "Ground", { 0.0f, -1.0f, 0.0f }, { 30.0f, 1.0f, 30.0f });
      fixtures::addBoxCollider(ground);
    }

    void step()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisions.fixedUpdate(*scene.objectManager, dt);
    }

    void stepFor(const int ticks)
    {
      for (int tick = 0; tick < ticks; ++tick)
      {
        step();
      }
    }

    // A grid of rotated boxes and spheres falling onto the ground.
    void addPile(const int nx, const int ny, const int nz)
    {
      std::mt19937 random(7);
      std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
      std::uniform_real_distribution<float> size(0.25f, 0.5f);
      std::uniform_real_distribution<float> angle(0.0f, 90.0f);

      int counter = 0;
      for (int x = 0; x < nx; ++x)
      {
        for (int y = 0; y < ny; ++y)
        {
          for (int z = 0; z < nz; ++z)
          {
            const glm::vec3 position(static_cast<float>(x - nx / 2) * 1.3f + jitter(random),
                                     1.0f + static_cast<float>(y) * 1.3f + jitter(random),
                                     static_cast<float>(z - nz / 2) * 1.3f + jitter(random));
            const float scale = size(random);

            auto object = fixtures::addObject(scene, "Body" + std::to_string(counter), position,
                                              { scale, scale, scale });
            fixtures::transformOf(object)->setRotation({ angle(random), angle(random), angle(random) });

            if (counter % 3 == 0)
            {
              fixtures::addSphereCollider(object, 1.0f);
            }
            else
            {
              fixtures::addBoxCollider(object);
            }

            fixtures::addRigidBody(object);
            objects.push_back(object);
            ++counter;
          }
        }
      }
    }
  };

  std::shared_ptr<Collider> colliderOf(const std::shared_ptr<Object>& object)
  {
    return object->getComponent<Collider>(ComponentType::collider);
  }

  ColliderPose poseOf(const std::shared_ptr<Object>& object)
  {
    const auto collider = colliderOf(object);
    return { collider->getPosition(), collider->getRotation(), collider->getScale() };
  }
}

TEST(ContactRefresh, WithRefreshOffTheTreeStillReproducesTheSweepExactly)
{
  World sweep(BroadPhaseMode::sweep, false);
  World tree(BroadPhaseMode::tree, false);
  sweep.addPile(4, 4, 4);
  tree.addPile(4, 4, 4);

  ASSERT_FALSE(tree.collisions.isContactRefreshEnabled());

  for (int tick = 0; tick < 200; ++tick)
  {
    sweep.step();
    tree.step();
  }

  EXPECT_EQ(tree.collisions.getContactsRefreshedTotal(), 0u);
  ASSERT_EQ(sweep.objects.size(), tree.objects.size());

  for (size_t i = 0; i < sweep.objects.size(); ++i)
  {
    const auto a = fixtures::positionOf(sweep.objects[i]);
    const auto b = fixtures::positionOf(tree.objects[i]);
    EXPECT_EQ(a.x, b.x) << "object " << i;
    EXPECT_EQ(a.y, b.y) << "object " << i;
    EXPECT_EQ(a.z, b.z) << "object " << i;

    const auto ra = fixtures::transformOf(sweep.objects[i])->getRotation();
    const auto rb = fixtures::transformOf(tree.objects[i])->getRotation();
    EXPECT_EQ(ra.x, rb.x) << "object " << i;
    EXPECT_EQ(ra.y, rb.y) << "object " << i;
    EXPECT_EQ(ra.z, rb.z) << "object " << i;
  }
}

TEST(ContactRefresh, ARefreshedBoxContactMatchesAFreshMeasurementOfTheMovedPair)
{
  World world(BroadPhaseMode::tree, true);

  const auto upper = fixtures::addObject(world.scene, "Upper", { 0.0f, 0.95f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  const auto lower = fixtures::addObject(world.scene, "Lower", { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  fixtures::addBoxCollider(upper);
  fixtures::addBoxCollider(lower);

  const auto upperCollider = colliderOf(upper);
  const auto lowerCollider = colliderOf(lower);

  const auto before = collisions::findContact(*upperCollider, *lowerCollider);
  ASSERT_TRUE(before.has_value());
  const auto selfThen = poseOf(upper);
  const auto otherThen = poseOf(lower);

  fixtures::transformOf(upper)->move({ 0.02f, -0.01f, 0.01f });
  fixtures::transformOf(lower)->move({ -0.005f, 0.0f, 0.0f });

  const auto refreshed = CollisionSystem::refreshContact(*before, selfThen, otherThen, poseOf(upper), poseOf(lower));
  ASSERT_EQ(refreshed.outcome, RefreshOutcome::refreshed);
  ASSERT_TRUE(refreshed.contact.has_value());

  const auto exact = collisions::findContact(*upperCollider, *lowerCollider);
  ASSERT_TRUE(exact.has_value());

  EXPECT_NEAR(refreshed.contact->depth(), exact->depth(), 1e-3f);
  EXPECT_GT(glm::dot(refreshed.contact->normal(), exact->normal()), 0.9999f);
}

TEST(ContactRefresh, AContactFallsBackWhenTheMotionCouldChangeItsFeatures)
{
  World world(BroadPhaseMode::tree, true);

  const auto upper = fixtures::addObject(world.scene, "Upper", { 0.0f, 0.95f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  const auto lower = fixtures::addObject(world.scene, "Lower", { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f });
  fixtures::addBoxCollider(upper);
  fixtures::addBoxCollider(lower);

  const auto contact = collisions::findContact(*colliderOf(upper), *colliderOf(lower));
  ASSERT_TRUE(contact.has_value());
  const auto selfThen = poseOf(upper);
  const auto otherThen = poseOf(lower);

  auto moved = selfThen;
  moved.position += glm::vec3(0.2f, 0.0f, 0.0f);
  EXPECT_EQ(CollisionSystem::refreshContact(*contact, selfThen, otherThen, moved, otherThen).outcome,
            RefreshOutcome::driftTooLarge);

  moved = selfThen;
  moved.position += glm::vec3(0.0f, 0.3f, 0.0f);
  EXPECT_EQ(CollisionSystem::refreshContact(*contact, selfThen, otherThen, moved, otherThen).outcome,
            RefreshOutcome::normalMotionTooLarge);

  moved = selfThen;
  moved.rotation += glm::vec3(0.0f, 1.0f, 0.0f);
  EXPECT_EQ(CollisionSystem::refreshContact(*contact, selfThen, otherThen, moved, otherThen).outcome,
            RefreshOutcome::rotationChanged);

  moved = selfThen;
  moved.position += glm::vec3(0.0f, 0.1f, 0.0f);
  const auto separated = CollisionSystem::refreshContact(*contact, selfThen, otherThen, moved, otherThen);
  EXPECT_EQ(separated.outcome, RefreshOutcome::separated);
  EXPECT_FALSE(separated.contact.has_value());
}

TEST(ContactRefresh, AStackOfFiveBoxesStaysStandingWithRefreshOn)
{
  World world(BroadPhaseMode::tree, true);

  std::vector<glm::vec3> starts;
  for (int i = 0; i < 5; ++i)
  {
    const glm::vec3 position(0.0f, 0.52f + static_cast<float>(i) * 1.02f, 0.0f);
    auto box = fixtures::addObject(world.scene, "Box" + std::to_string(i), position, { 0.5f, 0.5f, 0.5f });
    fixtures::addBoxCollider(box);
    fixtures::addRigidBody(box);
    world.objects.push_back(box);
    starts.push_back(position);
  }

  world.stepFor(500);

  // A column with nothing pushing it sideways has no reason to drift; a twentieth of a unit leaves room
  // for the solver's small corrections while still catching a stack that is sliding apart.
  constexpr float horizontalTolerance = 0.05f;

  for (size_t i = 0; i < world.objects.size(); ++i)
  {
    const auto position = fixtures::positionOf(world.objects[i]);

    EXPECT_LT(std::hypot(position.x - starts[i].x, position.z - starts[i].z), horizontalTolerance)
      << "box " << i;

    if (i == 0)
    {
      EXPECT_GT(position.y, 0.4f) << "box 0 sank into the ground";
    }
    else
    {
      EXPECT_GT(position.y, fixtures::positionOf(world.objects[i - 1]).y + 0.9f)
        << "box " << i << " sank into the box under it";
    }
  }

  EXPECT_GT(world.collisions.getContactsRefreshedTotal(), 0u);
}

TEST(ContactRefresh, AMixedPileStaysFiniteAndAboveTheGroundWithRefreshOn)
{
  World world(BroadPhaseMode::tree, true);
  world.addPile(5, 4, 5);

  ASSERT_EQ(world.objects.size(), 100u);

  world.stepFor(300);

  for (size_t i = 0; i < world.objects.size(); ++i)
  {
    const auto position = fixtures::positionOf(world.objects[i]);

    EXPECT_TRUE(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
      << "object " << i;
    EXPECT_GE(position.y, 0.0f) << "object " << i << " ended below the ground's top face";
  }

  EXPECT_GT(world.collisions.getContactsRefreshedTotal(), 0u);
}
