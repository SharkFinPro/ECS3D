#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "ConvexMeshFixtures.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/Terrain.h"
#include "queries/SceneQueries.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"
#include "terrain/TerrainTypes.h"
#include "terrain/TerrainVolume.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

// The quality gate for terrain collision: scenes that must hold in the headless suite with the solver and the
// generator as they are. Every scene runs at the server's tick (50 Hz), on terrain with the engine's default voxel
// size (2.5 units, half a meter at 5 units per meter), and prints what it measured.
namespace {
  using Field = std::function<float(glm::vec3)>;

  constexpr float tick = 1.0f / 50.0f;
  constexpr int ticksPerSecond = 50;
  constexpr float voxel = 2.5f;
  // One millimeter, at 5 world units per meter.
  constexpr float millimeter = 0.005f;
  constexpr float penetrationSlop = 0.0005f;
  constexpr float pi = 3.14159265358979f;
  constexpr float center = 120.0f;
  constexpr int settleSeconds = 3;
  constexpr int observeSeconds = 60;

  // Samples cover x and z in [0, 96) and y in [-32, 64), which is 240 units across at the default voxel size.
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

  Field flatGround()
  {
    return [](const glm::vec3 p) { return p.y; };
  }

  glm::vec3 slopeNormal(const float degrees)
  {
    const float angle = glm::radians(degrees);

    return { -std::sin(angle), std::cos(angle), 0.0f };
  }

  // Solid below the plane through the origin that rises along +x.
  Field slopeGround(const float degrees)
  {
    const glm::vec3 normal = slopeNormal(degrees);

    return [normal](const glm::vec3 p) { return glm::dot(p, normal); };
  }

  // Solid below y = |x - center|: a valley whose two walls are 45 degrees.
  Field valleyGround()
  {
    return [](const glm::vec3 p) { return (p.y - std::abs(p.x - center)) / std::sqrt(2.0f); };
  }

  // The exact distance to the solid wedge below a roof whose two faces fall away from x = center at the given angle.
  Field roofGround(const float degrees)
  {
    const float angle = glm::radians(degrees);
    const glm::vec2 face(std::cos(angle), -std::sin(angle));

    return [face](const glm::vec3 p)
    {
      const glm::vec2 offset(std::abs(p.x - center), p.y);
      const float along = std::max(0.0f, glm::dot(offset, face));
      const float distance = glm::length(offset - along * face);

      return offset.y < offset.x * face.y / face.x ? -distance : distance;
    };
  }

  // Solid below a bowl-shaped dimple of the given radius whose rim is at y = 0.
  Field dimpleGround(const float radius)
  {
    return [radius](const glm::vec3 p)
    {
      const float toBowl = glm::distance(p, glm::vec3(center, 0.0f, center));

      return std::max(p.y, radius - toBowl);
    };
  }

  struct Rolling {
    float amplitude;
    float wavelength;

    [[nodiscard]] float height(const float x, const float z) const
    {
      const float wave = 2.0f * pi / wavelength;

      return amplitude * std::sin(wave * x) * std::sin(wave * z);
    }

    // The first order distance estimate the terrain generators use: the vertical gap over sqrt(1 + |grad h|^2).
    [[nodiscard]] Field field() const
    {
      const Rolling self = *this;

      return [self](const glm::vec3 p)
      {
        const float wave = 2.0f * pi / self.wavelength;
        const float dx = self.amplitude * wave * std::cos(wave * p.x) * std::sin(wave * p.z);
        const float dz = self.amplitude * wave * std::sin(wave * p.x) * std::cos(wave * p.z);

        return (p.y - self.height(p.x, p.z)) / std::sqrt(1.0f + dx * dx + dz * dz);
      };
    }
  };

  Rolling rollingTerrain()
  {
    return { 1.5f, 80.0f };
  }

  enum class Shape { box, sphere, capsule, hull };

  struct Body {
    Shape shape = Shape::box;
    std::shared_ptr<Object> object;
    std::shared_ptr<RigidBody> body;
    std::shared_ptr<ConvexMeshCollider> hull;
    glm::vec3 halfExtents{ 1.0f };

    static constexpr float sphereRadius = 1.0f;
    static constexpr float capsuleRadius = 0.5f;
    static constexpr float capsuleCoreHalf = 0.5f;

    [[nodiscard]] glm::vec3 position() const
    {
      return fixtures::positionOf(object);
    }

    [[nodiscard]] glm::quat orientation() const
    {
      return fixtures::transformOf(object)->getOrientation();
    }

    // The distance every query point must stay above the ground.
    [[nodiscard]] float radius() const
    {
      switch (shape)
      {
        case Shape::sphere:
          return sphereRadius;
        case Shape::capsule:
          return capsuleRadius;
        default:
          return 0.0f;
      }
    }

    // Each of the body's extreme points in world space, and for a box a fine grid over its bottom face too: over a
    // ridge it is the face's interior that touches, not its corners.
    [[nodiscard]] std::vector<glm::vec3> points() const
    {
      const glm::quat turn = orientation();
      const glm::vec3 at = position();
      std::vector<glm::vec3> result;

      switch (shape)
      {
        case Shape::sphere:
          result.push_back(at);
          break;
        case Shape::capsule:
          result.push_back(at + turn * glm::vec3(0, capsuleCoreHalf, 0));
          result.push_back(at - turn * glm::vec3(0, capsuleCoreHalf, 0));
          break;
        case Shape::hull:
          result = hull->getWorldVertices();
          break;
        case Shape::box:
        {
          for (const float sx : { -1.0f, 1.0f })
          {
            for (const float sy : { -1.0f, 1.0f })
            {
              for (const float sz : { -1.0f, 1.0f })
              {
                result.push_back(at + turn * (halfExtents * glm::vec3(sx, sy, sz)));
              }
            }
          }

          constexpr float step = 1.25f;
          const int columns = std::max(1, static_cast<int>(std::ceil(2.0f * halfExtents.x / step)));
          const int rows = std::max(1, static_cast<int>(std::ceil(2.0f * halfExtents.z / step)));
          for (int i = 0; i <= columns; ++i)
          {
            for (int j = 0; j <= rows; ++j)
            {
              const float x = -halfExtents.x + 2.0f * halfExtents.x * static_cast<float>(i) / static_cast<float>(columns);
              const float z = -halfExtents.z + 2.0f * halfExtents.z * static_cast<float>(j) / static_cast<float>(rows);
              result.push_back(at + turn * glm::vec3(x, -halfExtents.y, z));
            }
          }
          break;
        }
      }

      return result;
    }
  };

  struct World {
    fixtures::Scene scene = fixtures::makeScene();
    std::shared_ptr<terrain::TerrainVolume> volume = std::make_shared<terrain::TerrainVolume>(voxel);
    std::shared_ptr<Object> object;
    std::shared_ptr<Terrain> terrain = std::make_shared<Terrain>();
    Field ground;
    CollisionSystem collisions;

    explicit World(const Field& field, const glm::vec3& position = glm::vec3(0))
      : object(fixtures::addObject(scene, "Terrain", position)), ground(field)
    {
      fillField(*volume, field);
      object->addComponent(terrain);
      terrain->setVolume(volume);
    }

    // A world whose ground is a static box ramp with the same surface as slopeGround, for comparison.
    static World boxRamp(const float degrees)
    {
      World world(slopeGround(degrees));
      world.terrain->setCollisionEnabled(false);
      const auto ramp = fixtures::addObject(world.scene, "Ramp", -2.0f * slopeNormal(degrees) + glm::vec3(0, 0, center), { 400, 2, 100 });
      fixtures::transformOf(ramp)->setRotation({ 0, 0, degrees });
      fixtures::addBoxCollider(ramp);

      return world;
    }

    [[nodiscard]] glm::vec3 localOf(const glm::vec3& world) const
    {
      return world - fixtures::positionOf(object);
    }

    [[nodiscard]] glm::vec3 worldOf(const glm::vec3& local) const
    {
      return local + fixtures::positionOf(object);
    }

    void tickOnce()
    {
      PhysicsSystem::fixedUpdate(*scene.objectManager, tick);
      collisions.fixedUpdate(*scene.objectManager, tick);
    }

    void run(const int ticks)
    {
      for (int i = 0; i < ticks; ++i)
      {
        tickOnce();
      }
    }

    // How far the body's deepest point is inside the ground, in the given distance field: negative while above it.
    [[nodiscard]] float depth(const Body& body, const Field& distance) const
    {
      float deepest = -std::numeric_limits<float>::max();
      for (const glm::vec3& point : body.points())
      {
        deepest = std::max(deepest, body.radius() - distance(localOf(point)));
      }

      return deepest;
    }

    [[nodiscard]] float depth(const Body& body) const
    {
      return depth(body, ground);
    }
  };

  // A dynamic body with its lowest point at rest height above its origin. The hull is the tetrahedron on its base unless
  // another is given.
  Body addBody(World& world, const Shape shape, const glm::vec3& position, const glm::vec3& rotation,
               const float friction, const glm::vec3& scale = glm::vec3(1.0f), const ConvexHull& hull = {})
  {
    Body result;
    result.shape = shape;
    result.halfExtents = scale;
    result.object = fixtures::addObject(world.scene, "Body", position, scale);
    fixtures::transformOf(result.object)->setRotation(rotation);

    switch (shape)
    {
      case Shape::box:
        fixtures::addBoxCollider(result.object);
        break;
      case Shape::sphere:
        fixtures::addSphereCollider(result.object, Body::sphereRadius);
        break;
      case Shape::capsule:
        fixtures::addCapsuleCollider(result.object, Body::capsuleRadius, 2.0f);
        break;
      case Shape::hull:
        result.hull = std::make_shared<ConvexMeshCollider>();
        EXPECT_TRUE(result.hull->setHull(hull.vertices.empty() ? convexFixtures::tetrahedron() : hull));
        result.object->addComponent(result.hull);
        break;
    }

    result.body = physicsFixtures::addBody(result.object, true);
    result.body->setMass(1.0f);
    result.body->setFriction(friction);

    return result;
  }

  // What a run of the scene shows once the body has had its three seconds.
  struct Rest {
    float settleTime = 0.0f;       // seconds until the body stays within a millimeter of where it ends
    float drift = 0.0f;            // the furthest it strays from its place at three seconds, over the next sixty
    float deepest = 0.0f;          // the deepest point's depth over the same sixty seconds
    float deepestEver = 0.0f;      // the same from the first tick
    float finalSpeed = 0.0f;
    float height = 0.0f;           // the body's final world height
  };

  // The depth is that of grounded when given, which is for a body whose own is not the one touching the ground.
  Rest measureRest(World& world, const Body& body, const Field& distance, const float seconds = observeSeconds,
                   const Body* grounded = nullptr)
  {
    const int total = (settleSeconds + static_cast<int>(seconds)) * ticksPerSecond;
    const auto track = [&body]
    {
      return std::pair(body.position(), body.position() + body.orientation() * glm::vec3(0.5f, 0.5f, 0.5f));
    };

    std::vector<std::pair<glm::vec3, glm::vec3>> trail;
    trail.reserve(static_cast<size_t>(total));
    Rest rest;
    rest.deepest = -std::numeric_limits<float>::max();
    rest.deepestEver = rest.deepest;

    for (int i = 0; i < total; ++i)
    {
      world.tickOnce();
      trail.push_back(track());
      const float depth = world.depth(grounded ? *grounded : body, distance);
      rest.deepestEver = std::max(rest.deepestEver, depth);
      if (i >= settleSeconds * ticksPerSecond)
      {
        rest.deepest = std::max(rest.deepest, depth);
      }
    }

    const auto far = [](const auto& a, const auto& b)
    {
      return std::max(glm::distance(a.first, b.first), glm::distance(a.second, b.second));
    };

    const auto& reference = trail[static_cast<size_t>(settleSeconds * ticksPerSecond - 1)];
    for (size_t i = static_cast<size_t>(settleSeconds * ticksPerSecond); i < trail.size(); ++i)
    {
      rest.drift = std::max(rest.drift, far(trail[i], reference));
    }

    size_t last = 0;
    for (size_t i = 0; i < trail.size(); ++i)
    {
      if (far(trail[i], trail.back()) > millimeter)
      {
        last = i + 1;
      }
    }
    rest.settleTime = static_cast<float>(last) * tick;
    rest.finalSpeed = glm::length(body.body->getVelocity()) / tick;
    rest.height = body.position().y;

    return rest;
  }

  Rest measureRest(World& world, const Body& body, const float seconds = observeSeconds, const Body* grounded = nullptr)
  {
    return measureRest(world, body, world.ground, seconds, grounded);
  }

  void report(const std::string& scene, const Rest& rest)
  {
    std::cout << "[gate] " << scene << ": settle " << rest.settleTime << " s, drift " << rest.drift / millimeter
              << " mm, depth " << rest.deepest / penetrationSlop << " slop (ever " << rest.deepestEver / penetrationSlop
              << "), speed " << rest.finalSpeed << "\n";
    ::testing::Test::RecordProperty(scene + " settleSeconds", std::to_string(rest.settleTime));
  }

  constexpr float slopeDegrees = 20.0f;
  constexpr float slopeFriction = 0.6f;

  // A body dropped this far along the slope's normal lands with some speed instead of being laid down.
  constexpr float dropHeight = 0.5f;

  // Where a body of the given rest height above the plane starts, on the slope at x = 200.
  glm::vec3 onSlope(const float restHeight)
  {
    const glm::vec3 surface(200.0f, 200.0f * std::tan(glm::radians(slopeDegrees)), center);

    return surface + slopeNormal(slopeDegrees) * (restHeight + dropHeight);
  }

  float restHeightOf(const Shape shape)
  {
    return shape == Shape::capsule ? Body::capsuleRadius : 1.0f;
  }

  // The slope, a body flat on it, and its friction.
  Body onTheSlope(World& world, const Shape shape, const float friction)
  {
    const glm::vec3 turn = shape == Shape::sphere ? glm::vec3(0) :
                           shape == Shape::capsule ? glm::vec3(90, 0, slopeDegrees) : glm::vec3(0, 0, slopeDegrees);

    return addBody(world, shape, onSlope(restHeightOf(shape)), turn, friction);
  }

  void restOnTheSlope(const Shape shape, const std::string& name)
  {
    World world(slopeGround(slopeDegrees));
    const Body body = onTheSlope(world, shape, slopeFriction);
    const Rest rest = measureRest(world, body);
    report(name, rest);

    EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
    EXPECT_LT(rest.drift, millimeter);
    EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
    EXPECT_LT(rest.finalSpeed, 1e-3f);

    // Positive control: without friction the same body slides down the slope.
    World slippery(slopeGround(slopeDegrees));
    const Body slid = onTheSlope(slippery, shape, 0.0f);
    const glm::vec3 start = slid.position();
    slippery.run(ticksPerSecond);

    EXPECT_GT(glm::distance(slid.position(), start), 5.0f);
  }

  // Where a rolling body is after the given seconds on the slope, starting from the same place.
  glm::vec3 afterRolling(World& world, const Shape shape, const float seconds)
  {
    const Body body = onTheSlope(world, shape, slopeFriction);
    world.run(static_cast<int>(seconds * ticksPerSecond));

    return body.position();
  }
}

TEST(TerrainResting, ABoxOnATwentyDegreeSlopeRestsWithin3SecondsAndHoldsFor60)
{
  restOnTheSlope(Shape::box, "slope box");
}

TEST(TerrainResting, AConvexHullOnATwentyDegreeSlopeRestsWithin3SecondsAndHoldsFor60)
{
  restOnTheSlope(Shape::hull, "slope hull");
}

TEST(TerrainResting, ACapsuleLyingAlongATwentyDegreeSlopeRestsWithin3SecondsAndHoldsFor60)
{
  // Its axis runs up the slope, so two points hold it.
  World world(slopeGround(slopeDegrees));
  const Body body = addBody(world, Shape::capsule, onSlope(Body::capsuleRadius), { 0, 0, slopeDegrees - 90.0f },
                            slopeFriction);
  const Rest rest = measureRest(world, body);
  report("slope capsule along", rest);

  EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
  EXPECT_LT(rest.drift, millimeter);
  EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
}

TEST(TerrainResting, ASphereOnATwentyDegreeSlopeBehavesAsOnABoxRamp)
{
  // Rolling resistance cannot hold a sphere on this slope, so the sphere's gate is that terrain changes nothing about
  // what a box ramp does with it.
  constexpr float seconds = 4.0f;
  auto terrain = World(slopeGround(slopeDegrees));
  auto ramp = World::boxRamp(slopeDegrees);

  const glm::vec3 start = onSlope(restHeightOf(Shape::sphere));
  const glm::vec3 onTerrain = afterRolling(terrain, Shape::sphere, seconds);
  const glm::vec3 onRamp = afterRolling(ramp, Shape::sphere, seconds);

  const float terrainRoll = glm::distance(onTerrain, start);
  const float rampRoll = glm::distance(onRamp, start);
  std::cout << "[gate] slope sphere: rolled " << terrainRoll << " on terrain, " << rampRoll << " on a box ramp in "
            << seconds << " s\n";

  // Positive control: it does roll, so the comparison is between two motions.
  EXPECT_GT(rampRoll, 10.0f);
  EXPECT_NEAR(terrainRoll, rampRoll, 0.05f * rampRoll);
}

TEST(TerrainResting, ACapsuleLyingAcrossATwentyDegreeSlopeBehavesAsOnABoxRamp)
{
  constexpr float seconds = 4.0f;
  auto terrain = World(slopeGround(slopeDegrees));
  auto ramp = World::boxRamp(slopeDegrees);

  const glm::vec3 start = onSlope(restHeightOf(Shape::capsule));
  const glm::vec3 onTerrain = afterRolling(terrain, Shape::capsule, seconds);
  const glm::vec3 onRamp = afterRolling(ramp, Shape::capsule, seconds);

  const float terrainRoll = glm::distance(onTerrain, start);
  const float rampRoll = glm::distance(onRamp, start);
  std::cout << "[gate] slope capsule across: rolled " << terrainRoll << " on terrain, " << rampRoll
            << " on a box ramp in " << seconds << " s\n";

  EXPECT_GT(rampRoll, 10.0f);
  EXPECT_NEAR(terrainRoll, rampRoll, 0.05f * rampRoll);
}

namespace {
  // Five boxes stacked upright at the given spot of the ground. The stacking tests hold the box at the top of a stack to
  // 0.02 units.
  void standFiveBoxes(const Field& field, const glm::vec2& at, const std::string& name)
  {
    constexpr float tolerance = 0.02f;
    constexpr float gap = 0.002f;

    const auto build = [&](World& world)
    {
      std::vector<Body> stack;
      for (int i = 0; i < 5; ++i)
      {
        const float rise = rollingTerrain().height(at.x, at.y) + 1.0f + (2.0f + gap) * static_cast<float>(i);
        stack.push_back(addBody(world, Shape::box, { at.x, rise, at.y }, glm::vec3(0), 0.5f));
      }

      return stack;
    };

    World world(field);
    const auto stack = build(world);
    const Rest rest = measureRest(world, stack.back(), observeSeconds, &stack.front());
    report(name, rest);

    EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
    EXPECT_LT(rest.drift, tolerance);
    EXPECT_LT(rest.finalSpeed, 1e-2f);
    EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
    EXPECT_NEAR(stack.back().position().y - stack.front().position().y, 4.0f * (2.0f + gap), 0.05f);

    // Positive control: without the terrain's collision the stack falls away.
    World open(field);
    open.terrain->setCollisionEnabled(false);
    const auto fallen = build(open);
    open.run(settleSeconds * ticksPerSecond);

    EXPECT_LT(fallen.back().position().y, -20.0f);
  }
}

TEST(TerrainResting, AFiveBoxStackInATroughOfRollingTerrainStandsFor60Seconds)
{
  // At the bottom of a trough a box rests on its four corners.
  standFiveBoxes(rollingTerrain().field(), { 60.0f, 20.0f }, "stack in a trough");
}

TEST(TerrainResting, AFiveBoxStackOnTheFlankOfRollingTerrainStandsFor60Seconds)
{
  // A 6.7 degree slope, with the stack upright on it.
  standFiveBoxes(rollingTerrain().field(), { 80.0f, 100.0f }, "stack on a flank");
}

TEST(TerrainResting, DownwardRaycastsOverRollingTerrainAgreeWithTheSampledSurface)
{
  const Rolling rolling{ 5.0f, 80.0f };
  const glm::vec3 position(7.0f, 3.0f, -5.0f);
  World world(rolling.field(), position);

  terrain::Sampler sampler(*world.volume);
  const auto surfaceAt = [&](const float x, const float z)
  {
    float low = -25.0f;
    float high = 25.0f;
    for (int i = 0; i < 60; ++i)
    {
      const float middle = 0.5f * (low + high);
      (sampler.distance({ x, middle, z }) < 0.0f ? low : high) = middle;
    }

    return 0.5f * (low + high);
  };

  float worst = 0.0f;
  float worstAnalytic = 0.0f;
  float lowest = 1e9f;
  float highest = -1e9f;
  int hits = 0;
  int rays = 0;
  for (float x = 30.7f; x < 210.0f; x += 11.3f)
  {
    for (float z = 30.7f; z < 210.0f; z += 11.3f)
    {
      ++rays;
      uuids::uuid object;
      glm::vec3 point;
      glm::vec3 normal;
      float distance = 0.0f;
      const glm::vec3 origin = world.worldOf({ x, 60.0f, z });
      if (!SceneQueries::raycast(*world.scene.objectManager, origin, { 0, -1, 0 }, 200.0f, 0xFFFFFFFFu, {}, object,
                                 point, normal, distance))
      {
        continue;
      }

      ++hits;
      const float sampled = surfaceAt(x, z);
      worst = std::max(worst, std::fabs(world.localOf(point).y - sampled));
      worstAnalytic = std::max(worstAnalytic, std::fabs(world.localOf(point).y - rolling.height(x, z)));
      lowest = std::min(lowest, world.localOf(point).y);
      highest = std::max(highest, world.localOf(point).y);
      EXPECT_EQ(object, world.object->getUUID());
    }
  }

  std::cout << "[gate] raycast grid: " << hits << " of " << rays << " rays hit, worst error against the sampled surface "
            << worst / voxel << " voxel, against the analytic height " << worstAnalytic / voxel << " voxel\n";
  RecordProperty("worstSampledErrorVoxels", std::to_string(worst / voxel));

  EXPECT_EQ(hits, rays);
  EXPECT_LT(worst, 0.05f * voxel);
  // Positive control: the surface is not flat, so agreeing with it means something.
  EXPECT_GT(highest - lowest, 5.0f);

  world.terrain->setCollisionEnabled(false);
  uuids::uuid object;
  glm::vec3 point;
  glm::vec3 normal;
  float distance = 0.0f;
  EXPECT_FALSE(SceneQueries::raycast(*world.scene.objectManager, world.worldOf({ 100, 60, 100 }), { 0, -1, 0 }, 200.0f,
                                     0xFFFFFFFFu, {}, object, point, normal, distance));
}

namespace {
  // The edit path the server applies: a brush over the bound volume, committed chunk by chunk, then the terrain's bounds
  // are invalidated so the next tick measures the new ground.
  void editUnder(World& world, const terrain::BrushOp::Kind kind, const glm::vec3& at, const float radius)
  {
    terrain::BrushOp op;
    op.kind = kind;
    op.center = at;
    op.radius = radius;
    op.material = 1;
    const auto edits = terrain::applyBrush(*world.volume, op);
    EXPECT_FALSE(edits.empty());

    terrain::commit(*world.volume, edits);
    world.terrain->invalidateBounds();
  }

  // The lowest the body's points stand above the ground, which is zero when it rests on it.
  float gapAbove(const World& world, const Body& body, terrain::Sampler& sampler)
  {
    float lowest = std::numeric_limits<float>::max();
    for (const glm::vec3& point : body.points())
    {
      lowest = std::min(lowest, sampler.metricDistance(world.localOf(point)) - body.radius());
    }

    return lowest;
  }
}

TEST(TerrainResting, DiggingAwayTheGroundUnderARestingBoxLetsItFallToTheNewFloor)
{
  World world(flatGround());
  const Body box = addBody(world, Shape::box, { center, 1.002f, center }, glm::vec3(0), 0.5f);
  world.run(settleSeconds * ticksPerSecond);
  ASSERT_NEAR(box.position().y, 1.0f, 3.0f * penetrationSlop);
  ASSERT_LT(std::fabs(box.body->getVelocity().y), 1e-3f);

  constexpr float craterRadius = 5.0f * voxel;
  editUnder(world, terrain::BrushOp::Kind::subtract, { center, 0.0f, center }, craterRadius);

  world.run(3);
  const float fell = box.body->getVelocity().y;
  const float above = box.position().y;
  world.run(2 * ticksPerSecond);

  terrain::Sampler sampler(*world.volume);
  const float gap = gapAbove(world, box, sampler);
  const float depth = world.depth(box, [&sampler](const glm::vec3 p) { return sampler.metricDistance(p); });
  std::cout << "[gate] dig under a box: velocity after 3 ticks " << fell << ", landed at " << box.position().y
            << " (floor " << -craterRadius << "), gap " << gap / penetrationSlop << " slop, depth "
            << depth / penetrationSlop << " slop, speed " << glm::length(box.body->getVelocity()) / tick << "\n";

  EXPECT_LT(fell, -0.02f);
  EXPECT_LT(above, 1.0f);
  EXPECT_LT(box.position().y, -craterRadius + 3.0f);
  EXPECT_GT(box.position().y, -craterRadius);
  EXPECT_LT(depth, 5.0f * penetrationSlop);
  EXPECT_LT(gap, 0.05f);
  EXPECT_LT(glm::length(box.body->getVelocity()), 1e-3f);
}

TEST(TerrainResting, FillingGroundUpIntoARestingBoxPushesItOutOntoTheNewSurface)
{
  World world(flatGround());
  const Body box = addBody(world, Shape::box, { center, 1.002f, center }, glm::vec3(0), 0.5f);
  world.run(settleSeconds * ticksPerSecond);
  ASSERT_NEAR(box.position().y, 1.0f, 3.0f * penetrationSlop);
  const glm::vec3 before = box.position();

  // The mound's center is at the old surface, so its flank leads out sideways as well as up.
  constexpr float moundRadius = 6.0f;
  editUnder(world, terrain::BrushOp::Kind::add, { center, 0.0f, center }, moundRadius);

  terrain::Sampler sampler(*world.volume);
  const Field sampled = [&sampler](const glm::vec3 p) { return sampler.metricDistance(p); };
  float firstTick = 0.0f;
  int ticksToClear = 0;
  float deepestLater = -1e9f;
  for (int i = 0; i < 150; ++i)
  {
    world.tickOnce();
    const float depth = world.depth(box, sampled);
    if (i == 0)
    {
      firstTick = depth;
    }
    if (depth >= 0.1f)
    {
      ticksToClear = i + 1;
    }
    if (i >= 10)
    {
      deepestLater = std::max(deepestLater, depth);
    }
  }

  const float gap = gapAbove(world, box, sampler);
  const float distanceToSurface = sampler.metricDistance(world.localOf(box.position()));
  std::cout << "[gate] fill into a box: depth " << firstTick << " after the first tick, clear of the ground after "
            << ticksToClear << " ticks, rests at " << box.position().x - center << ", " << box.position().y << ", "
            << box.position().z - center << " (mound top " << moundRadius << "), gap " << gap / penetrationSlop
            << " slop, depth " << deepestLater / penetrationSlop << " slop, speed "
            << glm::length(box.body->getVelocity()) / tick << "\n";

  EXPECT_LE(ticksToClear, 5);
  EXPECT_LT(deepestLater, 5.0f * penetrationSlop);
  EXPECT_LT(gap, 0.05f);
  EXPECT_GT(distanceToSurface, 0.5f);
  EXPECT_LT(glm::length(box.body->getVelocity()), 1e-3f);

  // Positive control: the edit really put the box inside the ground, and it moved out of it.
  EXPECT_GT(firstTick, 1.0f);
  EXPECT_GT(glm::distance(box.position(), before), 3.0f);
}

namespace {
  // A box more than 20 voxels wide in x, dropped half a unit onto the feature with its middle the given distance off it.
  void restWideBox(const Field& field, const float surfaceHeight, const float offset, const std::string& name)
  {
    constexpr float halfWidth = 26.0f;
    static_assert(2.0f * halfWidth / voxel > 20.0f);

    const glm::vec3 start(center + offset, surfaceHeight + 1.5f, center);
    const glm::vec3 size(halfWidth, 1.0f, 15.0f);

    World world(field);
    const Body box = addBody(world, Shape::box, start, glm::vec3(0), 0.5f, size);
    const Rest rest = measureRest(world, box);
    report(name, rest);

    EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
    EXPECT_LT(rest.drift, millimeter);
    EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
    EXPECT_LT(rest.finalSpeed, 1e-3f);

    // Positive control: with collision off the same box falls.
    World open(field);
    open.terrain->setCollisionEnabled(false);
    const Body dropped = addBody(open, Shape::box, start, glm::vec3(0), 0.5f, size);
    open.run(settleSeconds * ticksPerSecond);
    EXPECT_LT(dropped.position().y, start.y - 50.0f);
  }
}

TEST(TerrainResting, AWideBoxBalancedOnARidgeRestsWithoutSinkingIntoIt)
{
  restWideBox(roofGround(10.0f), 0.0f, 0.0f, "wide box centered on a ridge");
}

TEST(TerrainResting, AWideBoxOffCenterOnARidgeTipsOntoOneFaceAndRests)
{
  restWideBox(roofGround(10.0f), 0.0f, 8.0f, "wide box off a ridge");
}

TEST(TerrainResting, AWideBoxAcrossAGullyRestsOnItsWalls)
{
  // The walls are 45 degrees, so a box 52 units wide wedges at y = 26 where the gully is as wide as it.
  restWideBox(valleyGround(), 26.0f, 0.0f, "wide box across a gully");
}

TEST(TerrainResting, AWideBoxOffCenterAcrossAGullySettlesOnItsWalls)
{
  restWideBox(valleyGround(), 26.0f, 6.0f, "wide box off a gully");
}

TEST(TerrainResting, AHullStandingPointDownInADimpleSettlesAndRests)
{
  constexpr float radius = 5.0f * voxel;
  constexpr float reach = 2.0f;
  const std::vector<glm::vec3> corners{ { reach, 0, 0 }, { -reach, 0, 0 }, { 0, reach, 0 },
                                        { 0, -reach, 0 }, { 0, 0, reach }, { 0, 0, -reach } };
  const auto octahedron = buildConvexHull(corners);
  ASSERT_TRUE(octahedron.has_value());

  World world(dimpleGround(radius));
  const Body hull = addBody(world, Shape::hull, { center, -radius + reach + 0.05f, center }, glm::vec3(0), 0.5f,
                            glm::vec3(1.0f), *octahedron);
  const Rest rest = measureRest(world, hull);
  report("hull point down in a dimple", rest);

  EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
  EXPECT_LT(rest.drift, millimeter);
  EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
  // A body balanced on one vertex is unstable, so it topples onto an edge and rests there in the bowl.
  EXPECT_GT(hull.position().y, -radius);
  EXPECT_LT(hull.position().y, -radius + reach);
  EXPECT_LT(glm::distance(glm::vec2(hull.position().x, hull.position().z), glm::vec2(center)), 0.25f * radius);

  World open(dimpleGround(radius));
  open.terrain->setCollisionEnabled(false);
  const Body dropped = addBody(open, Shape::hull, { center, -radius + reach + 0.05f, center }, glm::vec3(0), 0.5f,
                               glm::vec3(1.0f), *octahedron);
  open.run(settleSeconds * ticksPerSecond);
  EXPECT_LT(dropped.position().y, -radius - 20.0f);
}

namespace {
  constexpr int pileColumns = 20;
  constexpr int pileRows = 10;
  constexpr int timedTicks = 60;
  constexpr int repetitions = 5;

  // 200 boxes spaced apart so each is its own island, resting on the world's ground.
  std::vector<Body> layBoxes(World& world)
  {
    std::vector<Body> boxes;
    for (int i = 0; i < pileColumns; ++i)
    {
      for (int j = 0; j < pileRows; ++j)
      {
        boxes.push_back(addBody(world, Shape::box, { 60.0f + 5.0f * static_cast<float>(i), 1.002f,
                                                     70.0f + 5.0f * static_cast<float>(j) }, glm::vec3(0), 0.5f));
      }
    }

    return boxes;
  }

  struct Timing {
    double collisions = 0.0;
    double whole = 0.0;
  };

  // Milliseconds per tick spent in the collision pass and in the whole step.
  Timing timeTicks(World& world)
  {
    using Clock = std::chrono::steady_clock;
    std::chrono::duration<double, std::milli> collisions{ 0 };
    std::chrono::duration<double, std::milli> whole{ 0 };

    for (int i = 0; i < timedTicks; ++i)
    {
      const auto start = Clock::now();
      PhysicsSystem::fixedUpdate(*world.scene.objectManager, tick);
      const auto middle = Clock::now();
      world.collisions.fixedUpdate(*world.scene.objectManager, tick);
      const auto end = Clock::now();
      collisions += end - middle;
      whole += end - start;
    }

    return { collisions.count() / timedTicks, whole.count() / timedTicks };
  }
}

TEST(TerrainResting, TwoHundredBoxesAtRestOnTerrainCostAboutAsMuchAsOnABoxFloor)
{
  World onTerrain(flatGround());
  const auto terrainBoxes = layBoxes(onTerrain);

  World onFloor(flatGround());
  onFloor.terrain->setCollisionEnabled(false);
  const auto floor = fixtures::addObject(onFloor.scene, "Floor", { center, -1.0f, center }, { 100, 1, 100 });
  fixtures::addBoxCollider(floor);
  const auto floorBoxes = layBoxes(onFloor);

  onTerrain.run(settleSeconds * ticksPerSecond);
  onFloor.run(settleSeconds * ticksPerSecond);
  for (size_t i = 0; i < terrainBoxes.size(); ++i)
  {
    ASSERT_NEAR(terrainBoxes[i].position().y, 1.0f, 3.0f * penetrationSlop);
    ASSERT_NEAR(floorBoxes[i].position().y, 1.0f, 3.0f * penetrationSlop);
  }

  std::vector<double> terrainTimes;
  std::vector<double> floorTimes;
  std::vector<double> terrainWhole;
  std::vector<double> floorWhole;
  for (int i = 0; i < repetitions; ++i)
  {
    const Timing a = timeTicks(onTerrain);
    const Timing b = timeTicks(onFloor);
    terrainTimes.push_back(a.collisions);
    floorTimes.push_back(b.collisions);
    terrainWhole.push_back(a.whole);
    floorWhole.push_back(b.whole);
  }

  const auto best = [](const std::vector<double>& values) { return *std::ranges::min_element(values); };
  const double ratio = best(terrainTimes) / best(floorTimes);
  const double wholeRatio = best(terrainWhole) / best(floorWhole);
  std::cout << "[gate] cost of 200 resting boxes per tick: terrain " << best(terrainTimes) << " ms, box floor "
            << best(floorTimes) << " ms, collision ratio " << ratio << " (whole step " << wholeRatio << ", target 1.25)\n";
  for (int i = 0; i < repetitions; ++i)
  {
    std::cout << "[gate]   run " << i << ": terrain " << terrainTimes[static_cast<size_t>(i)] << " ms, floor "
              << floorTimes[static_cast<size_t>(i)] << " ms\n";
  }
  RecordProperty("terrainMsPerTick", std::to_string(best(terrainTimes)));
  RecordProperty("boxFloorMsPerTick", std::to_string(best(floorTimes)));
  RecordProperty("collisionRatio", std::to_string(ratio));

  // Loose enough for a shared machine; the target is within 25 percent.
  EXPECT_LT(ratio, 3.0);

  // Positive control: both piles really were being solved, not skipped.
  EXPECT_GT(best(terrainTimes), 0.0);
  EXPECT_GT(best(floorTimes), 0.0);
}
