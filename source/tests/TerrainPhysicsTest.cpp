#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ContactSolver.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "ConvexMeshFixtures.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace {
  using namespace physicsFixtures;
  using Field = std::function<float(glm::vec3)>;

  constexpr float sphereRadius = 1.0f;
  constexpr float penetrationSlop = 0.0005f;
  constexpr float centerX = 48.0f;

  // Samples cover x and z in [0, 96) and y in [-32, 64).
  void fillField(terrain::TerrainVolume& volume, const Field& signedDistanceUnits)
  {
    const float voxelSize = volume.voxelSize();

    for (const terrain::ChunkCoord coord : terrain::chunksTouched({ { 0, -32, 0 }, { 96, 64, 96 } }))
    {
      std::vector<int16_t> distances(terrain::samplesPerChunk);
      std::vector<uint8_t> materials(terrain::samplesPerChunk, 1);
      const glm::ivec3 origin = terrain::chunkOrigin(coord);

      for (int z = 0; z < terrain::chunkSize; ++z)
      {
        for (int y = 0; y < terrain::chunkSize; ++y)
        {
          for (int x = 0; x < terrain::chunkSize; ++x)
          {
            const glm::vec3 position = voxelSize * glm::vec3(origin + glm::ivec3(x, y, z));
            distances[static_cast<size_t>(terrain::sampleIndex(x, y, z))] =
              terrain::encodeDistance(signedDistanceUnits(position) / voxelSize);
          }
        }
      }

      volume.setChunk(coord, terrain::TerrainChunk::dense(std::move(distances), std::move(materials)));
    }
  }

  Field slopeGround(const float degrees)
  {
    const float angle = glm::radians(degrees);
    const glm::vec3 normal(-std::sin(angle), std::cos(angle), 0.0f);

    return [normal](const glm::vec3 p) { return glm::dot(p, normal); };
  }

  // Solid below y = |x - centerX|: a valley whose two walls are 45 degrees.
  Field valleyGround()
  {
    return [](const glm::vec3 p) { return (p.y - std::abs(p.x - centerX)) / std::sqrt(2.0f); };
  }

  // Solid for x within 2.5 of 60: a wall five units thick, its near face at x = 57.5.
  Field slabGround()
  {
    return [](const glm::vec3 p) { return std::abs(p.x - 60.0f) - 2.5f; };
  }

  // 100 meters per second, as the displacement of one tick.
  constexpr float fastSpeed = 100.0f * PhysicsSystem::unitsPerMeter * dt;

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<terrain::TerrainVolume> volume = std::make_shared<terrain::TerrainVolume>(1.0f);
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
    CollisionSystem collisions;

    World(const glm::vec3& position, const glm::vec3& rotationDegrees)
      : object(fixtures::addObject(scene, "Terrain", position))
    {
      fixtures::transformOf(object)->setRotation(rotationDegrees);
    }

    // Surface at terrain-local y = 0 over a flat of the given size in samples.
    static World flat(const glm::ivec2 size = { 96, 96 }, const glm::vec3& position = glm::vec3(0),
                      const glm::vec3& rotationDegrees = glm::vec3(0))
    {
      World world(position, rotationDegrees);
      terrain::commit(*world.volume, terrain::fillFlat(*world.volume, size, 0.0f, 16.0f, 1));
      world.bind();

      return world;
    }

    static World field(const Field& field, const glm::vec3& position = glm::vec3(0),
                       const glm::vec3& rotationDegrees = glm::vec3(0))
    {
      World world(position, rotationDegrees);
      fillField(*world.volume, field);
      world.bind();

      return world;
    }

    void bind()
    {
      object->addComponent(terrain);
      terrain->setVolume(volume);
    }

    [[nodiscard]] glm::vec3 worldOf(const glm::vec3& local) const
    {
      const auto transform = fixtures::transformOf(object);

      return transform->getPosition() + transform->getOrientation() * local;
    }

    [[nodiscard]] glm::vec3 localOf(const glm::vec3& world) const
    {
      const auto transform = fixtures::transformOf(object);

      return glm::inverse(transform->getOrientation()) * (world - transform->getPosition());
    }

    void tick()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
      collisions.fixedUpdate(*scene.objectManager, dt);
    }

    void run(const int ticks)
    {
      for (int i = 0; i < ticks; ++i)
      {
        tick();
      }
    }

    [[nodiscard]] CollisionPair pairWith(const std::shared_ptr<Object>& other) const
    {
      return CollisionPair::make(object->getUUID(), other->getUUID());
    }
  };

  struct Ball {
    std::shared_ptr<Object> object;
    std::shared_ptr<SphereCollider> collider;
    std::shared_ptr<RigidBody> body;

    Ball(World& world, const glm::vec3& local, const float radius = sphereRadius)
      : object(fixtures::addObject(world.scene, "Ball", world.worldOf(local))),
        collider(fixtures::addSphereCollider(object, radius)),
        body(addBody(object, true))
    {
      body->setMass(1.0f);
      body->setFriction(0.5f);
    }

    [[nodiscard]] glm::vec3 position() const
    {
      return fixtures::positionOf(object);
    }
  };

  struct Rod {
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    std::shared_ptr<CapsuleCollider> collider;

    Rod(World& world, const glm::vec3& local, const glm::vec3& rotationDegrees, const float friction)
      : object(fixtures::addObject(world.scene, "Rod", world.worldOf(local)))
    {
      fixtures::transformOf(object)->setRotation(rotationDegrees);
      collider = fixtures::addCapsuleCollider(object, 0.5f, 2.0f);
      body = addBody(object, true);
      body->setMass(1.0f);
      body->setFriction(friction);
    }

    [[nodiscard]] glm::vec3 position() const
    {
      return fixtures::positionOf(object);
    }
  };

  bool contains(const std::vector<CollisionPair>& pairs, const CollisionPair& pair)
  {
    return std::ranges::find(pairs, pair) != pairs.end();
  }

  // The sphere resting on the flat from 2 units up: its height relative to the surface at every tick.
  struct Drop {
    float restHeight = 0.0f;
    float drift = 0.0f;
    float lowest = 0.0f;
  };

  Drop settle(World& world, const std::shared_ptr<Object>& object)
  {
    Drop drop;
    drop.lowest = 1e9f;
    float latest = 0.0f;
    float earliest = 0.0f;

    constexpr int ticks = 600;
    for (int i = 0; i < ticks; ++i)
    {
      world.tick();
      const float height = world.localOf(fixtures::positionOf(object)).y;
      drop.lowest = std::min(drop.lowest, height);
      if (i == 100)
      {
        earliest = height;
      }
      if (i > 100)
      {
        drop.drift = std::max(drop.drift, std::fabs(height - earliest));
      }
      latest = height;
    }
    drop.restHeight = latest;

    return drop;
  }

  Drop dropOnto(World& world, const glm::vec3& startLocal)
  {
    Ball ball(world, startLocal);

    return settle(world, ball.object);
  }

  // A unit box, or the tetrahedron hull standing on its base, whose lowest point is one unit below its origin.
  Drop dropSolid(World& world, const bool hull, const float startHeight)
  {
    const auto object = fixtures::addObject(world.scene, "Solid", world.worldOf({ 48, startHeight, 48 }));
    if (hull)
    {
      const auto collider = std::make_shared<ConvexMeshCollider>();
      EXPECT_TRUE(collider->setHull(convexFixtures::tetrahedron()));
      object->addComponent(collider);
    }
    else
    {
      fixtures::addBoxCollider(object);
    }

    const auto body = addBody(object, true);
    body->setMass(1.0f);
    body->setFriction(0.5f);

    return settle(world, object);
  }
}

TEST(TerrainPhysics, ASphereDroppedOnFlatTerrainRestsOnTheSurface)
{
  auto world = World::flat();

  const auto drop = dropOnto(world, { 48, sphereRadius + 2.0f, 48 });

  EXPECT_NEAR(drop.restHeight, sphereRadius, 3.0f * penetrationSlop);
  EXPECT_LT(drop.drift, 0.005f);
  EXPECT_GT(drop.lowest, sphereRadius - 5.0f * penetrationSlop);

  // Positive control: without the terrain's collision the same drop falls through.
  auto open = World::flat();
  open.terrain->setCollisionEnabled(false);

  const auto fallen = dropOnto(open, { 48, sphereRadius + 2.0f, 48 });

  EXPECT_LT(fallen.restHeight, -50.0f);
}

TEST(TerrainPhysics, ASphereRollsAcrossManyChunkBordersWithoutAKink)
{
  auto world = World::flat({ 1100, 64 });
  Ball ball(world, { 10, sphereRadius, 16 });

  // Frictionless, so the solver's rolling resistance (which scales with friction) does not slow it: any change in
  // its speed or heading comes from the terrain contacts.
  ball.body->setFriction(0.0f);
  constexpr float speed = 0.5f;
  ball.body->setVelocity({ speed, 0, 0 });

  float previousSpeed = speed;
  float largestChange = 0.0f;
  float largestHeading = 0.0f;
  int ticks = 0;

  while (ball.position().x < 1000.0f && ticks < 4000)
  {
    world.tick();
    ++ticks;

    const auto velocity = ball.body->getVelocity();
    const float planar = std::hypot(velocity.x, velocity.z);
    largestChange = std::max(largestChange, std::fabs(planar - previousSpeed));
    largestHeading = std::max(largestHeading, glm::degrees(std::atan2(std::fabs(velocity.z), velocity.x)));
    previousSpeed = planar;
  }

  // Positive control: it really did cross the whole strip, rolling.
  EXPECT_GE(ball.position().x, 1000.0f);
  EXPECT_NEAR(previousSpeed, speed, 0.01f);
  EXPECT_NEAR(ball.position().y, sphereRadius, 0.01f);

  EXPECT_LT(largestChange, 0.005f);
  EXPECT_LT(largestHeading, 0.5f);
}

TEST(TerrainPhysics, ARollingSphereTravelsAsFarAsOnABoxFloor)
{
  auto world = World::flat({ 200, 64 });
  Ball onTerrain(world, { 28, sphereRadius, 16 });

  // A box floor with its top at y = 0, the surface the terrain has.
  auto floorScene = fixtures::makeScene();
  const auto floor = fixtures::addObject(floorScene, "Floor", { 100, -1, 32 }, { 100, 1, 32 });
  fixtures::addBoxCollider(floor);
  CollisionSystem floorCollisions;
  const auto ballObject = fixtures::addObject(floorScene, "Ball", { 28, sphereRadius, 16 });
  fixtures::addSphereCollider(ballObject, sphereRadius);
  const auto floorBody = addBody(ballObject, true);
  floorBody->setMass(1.0f);

  // Fast enough to cross the chunk border at x = 32, and with the same friction as the box.
  constexpr float speed = 0.9f;
  onTerrain.body->setVelocity({ speed, 0, 0 });
  floorBody->setVelocity({ speed, 0, 0 });
  ASSERT_FLOAT_EQ(onTerrain.body->getFriction(), floorBody->getFriction());

  for (int i = 0; i < 200; ++i)
  {
    world.tick();
    PhysicsSystem::fixedUpdate(*floorScene.objectManager, dt);
    floorCollisions.fixedUpdate(*floorScene.objectManager, dt);
  }

  const float onTerrainDistance = onTerrain.position().x - 28.0f;
  const float onFloorDistance = fixtures::positionOf(ballObject).x - 28.0f;

  // Positive control: it rolled a good way, past the border, and then stopped.
  EXPECT_GT(onFloorDistance, 4.0f);
  EXPECT_GT(onTerrain.position().x, 32.0f);
  EXPECT_NEAR(onTerrainDistance, onFloorDistance, 0.05f * onFloorDistance);
  EXPECT_LT(glm::length(onTerrain.body->getVelocity()), 1e-3f);
  EXPECT_LT(glm::length(floorBody->getVelocity()), 1e-3f);
}

TEST(TerrainPhysics, ACapsuleLyingAlongASlopeComesToRest)
{
  // Its axis runs up the slope, so it rests on two points and friction holds it (a capsule lying across the slope
  // would roll, which the solver's rolling resistance cannot hold at this angle).
  constexpr float degrees = 20.0f;
  const float rise = std::tan(glm::radians(degrees));
  const glm::vec3 start(48, 48.0f * rise + 1.0f, 48);
  const glm::vec3 alongSlope(0, 0, degrees - 90.0f);

  auto world = World::field(slopeGround(degrees));
  Rod held(world, start, alongSlope, 0.6f);

  world.run(400);

  EXPECT_LT(glm::length(held.body->getVelocity()), 1e-3f);
  EXPECT_LT(glm::distance(held.position(), start), 1.0f);
  EXPECT_NEAR(held.position().y, 48.0f * rise + 0.5f / std::cos(glm::radians(degrees)), 0.2f);

  // Positive control: without friction the same capsule slides down the slope.
  auto slippery = World::field(slopeGround(degrees));
  Rod slid(slippery, start, alongSlope, 0.0f);

  slippery.run(10);

  EXPECT_LT(slid.position().x, 44.0f);
  EXPECT_GT(slid.position().y, 5.0f);
}

TEST(TerrainPhysics, AnUprightCapsuleCrossesAChunkBorderGoingUpAGentleSlope)
{
  constexpr float degrees = 10.0f;
  auto world = World::field(slopeGround(degrees));
  const float rise = std::tan(glm::radians(degrees));
  Rod rod(world, { 29.0f, 29.0f * rise + 1.1f, 48 }, { 0, 0, 0 }, 0.0f);

  const float launch = 0.9f;
  rod.body->setVelocity({ launch * std::cos(glm::radians(degrees)), launch * std::sin(glm::radians(degrees)), 0 });

  float previousSpeed = launch;
  float previousX = rod.position().x;
  float largestDrop = 0.0f;
  float furthest = previousX;
  for (int i = 0; i < 30; ++i)
  {
    world.tick();

    const float x = rod.position().x;
    const float speed = glm::length(rod.body->getVelocity());
    if (previousX > 30.0f && previousX < 33.0f)
    {
      largestDrop = std::max(largestDrop, previousSpeed - speed);
    }
    previousSpeed = speed;
    previousX = x;
    furthest = std::max(furthest, x);
  }

  // It slows by gravity along the slope, about 0.085 per tick, and keeps moving through the border at x = 32 rather
  // than stopping dead against it.
  EXPECT_GT(furthest, 33.0f);
  EXPECT_LT(largestDrop, 0.2f);
}

TEST(TerrainPhysics, ASphereInAGullyHoldsOneContactPerWall)
{
  auto world = World::field(valleyGround());
  constexpr float gullyRadius = 3.0f;
  Ball ball(world, { centerX + 0.3f, 8.0f, 48 }, gullyRadius);

  world.run(400);

  const auto cachedFor = [&world, &ball]
  {
    std::set<uint32_t> subIndices;
    float impulse = 0.0f;
    for (const auto& [key, cached] : world.collisions.getContactCache())
    {
      if (key.first == ball.collider.get() || key.second == ball.collider.get())
      {
        subIndices.insert(key.subIndex);
        for (size_t i = 0; i < cached.pointCount; ++i)
        {
          impulse += cached.points[i].normalImpulse;
        }
      }
    }

    return std::pair{ subIndices, impulse };
  };

  EXPECT_LT(glm::length(ball.body->getVelocity()), 1e-3f);
  EXPECT_NEAR(world.localOf(ball.position()).y, std::sqrt(2.0f) * gullyRadius, 0.05f);

  const std::set<uint32_t> both{ 0, 1 };
  for (int i = 0; i < 2; ++i)
  {
    world.tick();

    const auto [subIndices, impulse] = cachedFor();
    EXPECT_EQ(subIndices, both);
    EXPECT_GT(impulse, 0.0f);
  }
}

TEST(TerrainPhysics, ARotatedTerrainGivesTheSameRestHeightAboveItsSurface)
{
  auto world = World::flat({ 96, 96 }, { 7, 3, -5 }, { 0, 30, 0 });

  const auto drop = dropOnto(world, { 48, sphereRadius + 2.0f, 48 });

  EXPECT_NEAR(drop.restHeight, sphereRadius, 3.0f * penetrationSlop);
  EXPECT_LT(drop.drift, 0.005f);

  // Positive control: the surface really is above the world origin's level, so the rest was not at y = 0.
  EXPECT_NEAR(fixtures::positionOf(world.object).y, 3.0f, 1e-6f);
}

TEST(TerrainPhysics, ATerrainTiltedToATwentyDegreeSlopeHoldsACapsuleAtRest)
{
  auto world = World::flat({ 96, 96 }, { 0, 0, 0 }, { 0, 0, 20 });
  const glm::vec3 start(48, 1.0f, 48);
  Rod held(world, start, { 0, 0, -70 }, 0.6f);

  world.run(400);

  EXPECT_LT(glm::length(held.body->getVelocity()), 1e-3f);
  EXPECT_NEAR(world.localOf(held.position()).y, 0.5f, 0.05f);
  EXPECT_LT(glm::distance(world.localOf(held.position()), start), 1.0f);

  // Positive control: the surface really is a world slope, since without friction the capsule slides down it.
  auto slippery = World::flat({ 96, 96 }, { 0, 0, 0 }, { 0, 0, 20 });
  Rod slid(slippery, start, { 0, 0, -70 }, 0.0f);

  slippery.run(10);

  EXPECT_LT(slippery.localOf(slid.position()).x, 44.0f);
}

TEST(TerrainPhysics, CollisionEventsNameTheTerrainObject)
{
  auto world = World::flat();
  Ball ball(world, { 48, sphereRadius + 2.0f, 48 });
  const auto pair = world.pairWith(ball.object);

  bool entered = false;
  int enterTick = -1;
  for (int i = 0; i < 100 && !entered; ++i)
  {
    world.tick();
    entered = contains(world.collisions.getCollisionEnters(), pair);
    enterTick = i;
  }
  EXPECT_TRUE(entered);
  EXPECT_GT(enterTick, 0);

  world.run(200);
  bool staying = true;
  for (int i = 0; i < 20; ++i)
  {
    world.tick();
    staying = staying && contains(world.collisions.getCollisionStays(), pair);
  }
  EXPECT_TRUE(staying);

  ball.body->setVelocity({ 0, 2.0f, 0 });
  bool exited = false;
  for (int i = 0; i < 10 && !exited; ++i)
  {
    world.tick();
    exited = contains(world.collisions.getCollisionExits(), pair);
  }
  EXPECT_TRUE(exited);
}

TEST(TerrainPhysics, AMaskThatExcludesTheTerrainLayerGivesNoEventsAndNoResponse)
{
  auto world = World::flat();
  Ball masked(world, { 24, sphereRadius + 2.0f, 24 });
  masked.collider->setMask(0xFFFFFFFEu);
  const auto maskedPair = world.pairWith(masked.object);

  Ball plain(world, { 72, sphereRadius + 2.0f, 72 });
  const auto plainPair = world.pairWith(plain.object);

  bool maskedEvent = false;
  bool plainEvent = false;
  for (int i = 0; i < 200; ++i)
  {
    world.tick();
    maskedEvent = maskedEvent || contains(world.collisions.getCollisionEnters(), maskedPair) ||
                  contains(world.collisions.getCollisionStays(), maskedPair);
    plainEvent = plainEvent || contains(world.collisions.getCollisionEnters(), plainPair);
  }

  // Positive control: the unmasked ball is stopped and reports it.
  EXPECT_TRUE(plainEvent);
  EXPECT_NEAR(world.localOf(plain.position()).y, sphereRadius, 0.01f);

  EXPECT_FALSE(maskedEvent);
  EXPECT_LT(world.localOf(masked.position()).y, -50.0f);
}

TEST(TerrainPhysics, ASphereSunkDeepIntoTerrainIsLiftedOutInOneTickAndRests)
{
  auto world = World::flat();
  // 0.3 deep, more than the contact margin of 0.1.
  Ball ball(world, { 48, sphereRadius - 0.3f, 48 });

  world.tick();

  // Out to within about half the margin of the surface at once, not eased out over many ticks.
  const float lifted = world.localOf(ball.position()).y;
  EXPECT_GT(lifted, sphereRadius - 0.06f);
  EXPECT_LT(lifted, sphereRadius + 0.01f);

  world.run(200);

  EXPECT_NEAR(world.localOf(ball.position()).y, sphereRadius, 3.0f * penetrationSlop);
  EXPECT_LT(glm::length(ball.body->getVelocity()), 1e-3f);
}

TEST(TerrainPhysics, DisablingTheTerrainsCollisionMidPlayEndsTheContactAndTheBodyFalls)
{
  auto world = World::flat();
  Ball ball(world, { 48, sphereRadius + 0.5f, 48 });
  const auto pair = world.pairWith(ball.object);

  world.run(100);
  ASSERT_NEAR(world.localOf(ball.position()).y, sphereRadius, 0.01f);
  ASSERT_TRUE(contains(world.collisions.getCollisionStays(), pair));

  world.terrain->setCollisionEnabled(false);
  world.tick();

  EXPECT_TRUE(contains(world.collisions.getCollisionExits(), pair));

  world.run(5);

  EXPECT_LT(ball.body->getVelocity().y, 0.0f);
  EXPECT_LT(world.localOf(ball.position()).y, sphereRadius - 0.5f);
}

TEST(TerrainPhysics, ABoxDroppedOnFlatTerrainRestsOnTheSurface)
{
  auto world = World::flat();

  const auto drop = dropSolid(world, false, 3.0f);

  EXPECT_NEAR(drop.restHeight, 1.0f, 3.0f * penetrationSlop);
  EXPECT_LT(drop.drift, 0.005f);
  EXPECT_GT(drop.lowest, 1.0f - 5.0f * penetrationSlop);

  auto open = World::flat();
  open.terrain->setCollisionEnabled(false);

  EXPECT_LT(dropSolid(open, false, 3.0f).restHeight, -50.0f);
}

TEST(TerrainPhysics, AConvexHullDroppedOnFlatTerrainRestsOnTheSurface)
{
  auto world = World::flat();

  const auto drop = dropSolid(world, true, 3.0f);

  EXPECT_NEAR(drop.restHeight, 1.0f, 3.0f * penetrationSlop);
  EXPECT_LT(drop.drift, 0.005f);
  EXPECT_GT(drop.lowest, 1.0f - 5.0f * penetrationSlop);

  auto open = World::flat();
  open.terrain->setCollisionEnabled(false);

  EXPECT_LT(dropSolid(open, true, 3.0f).restHeight, -50.0f);
}

TEST(TerrainPhysics, ABoxOnATwentyDegreeSlopeComesToRest)
{
  constexpr float degrees = 20.0f;
  const float rise = std::tan(glm::radians(degrees));
  const glm::vec3 normal(-std::sin(glm::radians(degrees)), std::cos(glm::radians(degrees)), 0.0f);
  const glm::vec3 start = glm::vec3(48, 48.0f * rise, 48) + normal * 1.5f;

  const auto settleBox = [&](const float friction)
  {
    auto world = World::field(slopeGround(degrees));
    const auto object = fixtures::addObject(world.scene, "Crate", world.worldOf(start));
    fixtures::transformOf(object)->setRotation({ 0, 0, degrees });
    fixtures::addBoxCollider(object);
    const auto body = addBody(object, true);
    body->setMass(1.0f);
    body->setFriction(friction);

    world.run(300);
    const auto middle = fixtures::positionOf(object);
    float drift = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
      world.tick();
      drift = std::max(drift, glm::distance(fixtures::positionOf(object), middle));
    }

    return std::pair(drift, fixtures::positionOf(object));
  };

  const auto [drift, rest] = settleBox(0.6f);

  EXPECT_LT(drift, 0.003f);
  EXPECT_NEAR(glm::dot(rest - glm::vec3(48, 48.0f * rise, 48), normal), 1.0f, 3.0f * penetrationSlop);

  // Positive control: without friction the same box slides down the slope.
  EXPECT_GT(settleBox(0.0f).first, 1.0f);
}

TEST(TerrainPhysics, ASphereFiredAtAThinWallStopsAtItsFace)
{
  auto world = World::field(slabGround());
  Ball ball(world, { 0, 16, 48 });
  ball.body->setDoGravity(false);
  ball.body->setVelocity({ fastSpeed, 0, 0 });

  float farthest = 0.0f;
  for (int i = 0; i < 12; ++i)
  {
    world.tick();
    farthest = std::max(farthest, world.localOf(ball.position()).x);
  }

  // The face is at 57.5 and the sphere's radius is one, and the shot would have crossed the whole wall in one tick.
  EXPECT_GT(farthest, 56.5f - 0.05f);
  EXPECT_LT(farthest, 56.5f + 5.0f * penetrationSlop);
}

TEST(TerrainPhysics, ABoxFiredAtAThinWallStopsAtItsFace)
{
  auto world = World::field(slabGround());
  const auto object = fixtures::addObject(world.scene, "Crate", world.worldOf({ 0, 16, 48 }));
  const auto collider = fixtures::addBoxCollider(object);
  const auto body = addBody(object, false);
  body->setMass(1.0f);
  body->setVelocity({ fastSpeed, 0, 0 });

  // A box may turn a little, so its reach is measured rather than its center.
  float reach = 0.0f;
  float farthest = 0.0f;
  for (int i = 0; i < 12; ++i)
  {
    world.tick();
    reach = std::max(reach, collider->getBoundingBox().maxX);
    farthest = std::max(farthest, world.localOf(fixtures::positionOf(object)).x);
  }

  EXPECT_GT(farthest, 55.5f);
  EXPECT_LT(reach, 57.5f + 5.0f * penetrationSlop);
}

TEST(TerrainPhysics, ASphereFiredAtAHillsideNeverEndsATickInsideIt)
{
  constexpr float degrees = 20.0f;
  const glm::vec3 normal(-std::sin(glm::radians(degrees)), std::cos(glm::radians(degrees)), 0.0f);

  auto world = World::field(slopeGround(degrees));
  Ball ball(world, { 0, 30, 48 });
  ball.body->setDoGravity(false);
  ball.body->setVelocity({ fastSpeed, 0, 0 });

  // The hit is at the end of the second tick; after that the sphere has left the volume.
  float lowest = 1e9f;
  for (int i = 0; i < 2; ++i)
  {
    world.tick();
    lowest = std::min(lowest, glm::dot(world.localOf(ball.position()), normal));
  }

  EXPECT_GT(lowest, sphereRadius - 5.0f * penetrationSlop);
  // Positive control: the sphere did reach the slope, so the bound above was tested.
  EXPECT_LT(lowest, sphereRadius + 0.2f);
}

TEST(TerrainPhysics, ASphereAndABoxPlacedDeepInTheGroundAreLiftedOutAndRest)
{
  auto world = World::flat();
  Ball ball(world, { 30, -6, 48 });

  const auto object = fixtures::addObject(world.scene, "Crate", world.worldOf({ 60, -6, 48 }));
  fixtures::addBoxCollider(object);
  const auto body = addBody(object, true);
  body->setMass(1.0f);
  body->setFriction(0.5f);

  world.run(4);

  EXPECT_GT(world.localOf(ball.position()).y, sphereRadius - 0.1f);
  EXPECT_GT(world.localOf(fixtures::positionOf(object)).y, 1.0f - 0.1f);

  world.run(300);

  EXPECT_NEAR(world.localOf(ball.position()).y, sphereRadius, 3.0f * penetrationSlop);
  EXPECT_NEAR(world.localOf(fixtures::positionOf(object)).y, 1.0f, 3.0f * penetrationSlop);
  EXPECT_LT(glm::length(ball.body->getVelocity()), 1e-3f);
  EXPECT_LT(glm::length(body->getVelocity()), 1e-3f);
}

TEST(TerrainPhysics, ALargeBoxFiredAtAThinWallStopsAtItsFace)
{
  auto world = World::field(slabGround());
  const auto object = fixtures::addObject(world.scene, "Crate", world.worldOf({ -5, 16, 48 }), glm::vec3(3));
  const auto collider = fixtures::addBoxCollider(object);
  const auto body = addBody(object, false);
  body->setMass(1.0f);
  body->setVelocity({ fastSpeed, 0, 0 });

  // Its bounding sphere is wider than the distance band, so only its vertices can see the ground coming.
  float reach = 0.0f;
  float farthest = 0.0f;
  for (int i = 0; i < 12; ++i)
  {
    world.tick();
    reach = std::max(reach, collider->getBoundingBox().maxX);
    farthest = std::max(farthest, world.localOf(fixtures::positionOf(object)).x);
  }

  EXPECT_GT(farthest, 53.0f);
  EXPECT_LT(reach, 57.5f + 5.0f * penetrationSlop);
}

TEST(TerrainPhysics, ASphereDeepInARotatedTerrainIsLiftedAlongItsOwnUp)
{
  auto world = World::flat({ 96, 96 }, glm::vec3(0), { 0, 0, 90 });
  Ball ball(world, { 30, -6, 48 });
  ball.body->setDoGravity(false);

  world.run(4);

  const auto local = world.localOf(ball.position());
  EXPECT_GT(local.y, sphereRadius - 0.1f);
  EXPECT_LT(local.y, sphereRadius + 0.1f);
  EXPECT_NEAR(local.x, 30.0f, 0.1f);
  EXPECT_NEAR(local.z, 48.0f, 0.1f);
}

TEST(TerrainPhysics, ASphereFiredAtAThinWallOfARotatedTerrainStopsAtItsFace)
{
  auto world = World::field(slabGround(), glm::vec3(0), { 0, 0, 90 });
  Ball ball(world, { 0, 16, 48 });
  ball.body->setDoGravity(false);
  ball.body->setVelocity(world.worldOf({ fastSpeed, 0, 0 }) - world.worldOf(glm::vec3(0)));

  float farthest = 0.0f;
  for (int i = 0; i < 12; ++i)
  {
    world.tick();
    farthest = std::max(farthest, world.localOf(ball.position()).x);
  }

  EXPECT_GT(farthest, 56.5f - 0.05f);
  EXPECT_LT(farthest, 56.5f + 5.0f * penetrationSlop);
}
