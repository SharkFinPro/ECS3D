#include <gtest/gtest.h>

#include "TerrainRestingFixtures.h"
#include "queries/SceneQueries.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainOps.h"
#include "terrain/TerrainSampling.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <uuid.h>

using namespace terrainResting;

namespace {
  bool castDown(World& world, const glm::vec3& origin, uuids::uuid& object, glm::vec3& point)
  {
    glm::vec3 normal;
    float distance = 0.0f;

    return SceneQueries::raycast(*world.scene.objectManager, origin, { 0, -1, 0 }, 200.0f, 0xFFFFFFFFu, {}, object,
                                 point, normal, distance);
  }

  struct RayGrid {
    int hits = 0;
    int rays = 0;
    float worst = 0.0f;
    float worstAnalytic = 0.0f;
    float lowest = 1e9f;
    float highest = -1e9f;
  };

  // Rays down over a grid that avoids the voxel lattice, each compared with the zero crossing of the sampled field along
  // the same vertical and with the analytic height.
  RayGrid castGrid(World& world, const Rolling& rolling)
  {
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

    RayGrid grid;
    for (float x = 30.7f; x < 210.0f; x += 11.3f)
    {
      for (float z = 30.7f; z < 210.0f; z += 11.3f)
      {
        ++grid.rays;
        uuids::uuid object;
        glm::vec3 point;
        if (!castDown(world, world.worldOf({ x, 60.0f, z }), object, point))
        {
          continue;
        }

        ++grid.hits;
        const float hitY = world.localOf(point).y;
        grid.worst = std::max(grid.worst, std::fabs(hitY - surfaceAt(x, z)));
        grid.worstAnalytic = std::max(grid.worstAnalytic, std::fabs(hitY - rolling.height(x, z)));
        grid.lowest = std::min(grid.lowest, hitY);
        grid.highest = std::max(grid.highest, hitY);
        EXPECT_EQ(object, world.object->getUUID());
      }
    }

    return grid;
  }
}

TEST(TerrainResting, DownwardRaycastsOverRollingTerrainAgreeWithTheSampledSurface)
{
  const Rolling rolling{ 5.0f, 80.0f };
  World world(rolling.field(), glm::vec3(7.0f, 3.0f, -5.0f));

  const RayGrid grid = castGrid(world, rolling);

  std::cout << "[gate] raycast grid: " << grid.hits << " of " << grid.rays
            << " rays hit, worst error against the sampled surface " << grid.worst / voxel
            << " voxel, against the analytic height " << grid.worstAnalytic / voxel << " voxel\n";
  RecordProperty("worstSampledErrorVoxels", std::to_string(grid.worst / voxel));

  EXPECT_EQ(grid.hits, grid.rays);
  EXPECT_LT(grid.worst, 0.05f * voxel);
  EXPECT_LT(grid.worstAnalytic, 0.05f * voxel);
  // Positive control: the surface is not flat, so agreeing with it means something.
  EXPECT_GT(grid.highest - grid.lowest, 5.0f);

  world.terrain->setCollisionEnabled(false);
  uuids::uuid object;
  glm::vec3 point;
  EXPECT_FALSE(castDown(world, world.worldOf({ 100, 60, 100 }), object, point));
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

  world.tickOnce();
  const float fell = box.body->getVelocity().y;
  world.run(2);
  const float above = box.position().y;
  world.run(2 * ticksPerSecond);

  terrain::Sampler sampler(*world.volume);
  const float gap = gapAbove(world, box, sampler);
  const float depth = world.depth(box, [&sampler](const glm::vec3 p) { return sampler.metricDistance(p); });
  std::cout << "[gate] dig under a box: velocity after 1 tick " << fell << ", landed at " << box.position().y
            << " (floor " << -craterRadius << "), gap " << gap / penetrationSlop << " slop, depth "
            << depth / penetrationSlop << " slop, speed " << glm::length(box.body->getVelocity()) / tick << "\n";

  EXPECT_LT(fell, -0.01f);
  EXPECT_LT(above, 1.0f);
  EXPECT_LT(box.position().y, -craterRadius + 3.0f);
  EXPECT_GT(box.position().y, -craterRadius);
  EXPECT_LT(depth, 5.0f * penetrationSlop);
  EXPECT_LT(gap, 0.05f);
  EXPECT_LT(glm::length(box.body->getVelocity()), 1e-3f);
}

namespace {
  struct Clearing {
    float firstTick = 0.0f;
    int ticksToClear = 0;
    float deepestLater = -1e9f;
  };

  // How deep the box is in the ground each tick after an edit: on the first, how long until it is clear, and how deep it
  // stays once it has had ten ticks.
  Clearing watchClearing(World& world, const Body& box, const Field& sampled)
  {
    Clearing clearing;
    for (int i = 0; i < 150; ++i)
    {
      world.tickOnce();
      const float depth = world.depth(box, sampled);
      if (i == 0)
      {
        clearing.firstTick = depth;
      }
      if (depth >= 0.1f)
      {
        clearing.ticksToClear = i + 1;
      }
      if (i >= 10)
      {
        clearing.deepestLater = std::max(clearing.deepestLater, depth);
      }
    }

    return clearing;
  }
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
  const Clearing clearing = watchClearing(world, box, sampled);

  const float gap = gapAbove(world, box, sampler);
  const float distanceToSurface = sampler.metricDistance(world.localOf(box.position()));
  std::cout << "[gate] fill into a box: depth " << clearing.firstTick << " after the first tick, clear of the ground after "
            << clearing.ticksToClear << " ticks, rests at " << box.position().x - center << ", " << box.position().y << ", "
            << box.position().z - center << " (mound top " << moundRadius << "), gap " << gap / penetrationSlop
            << " slop, depth " << clearing.deepestLater / penetrationSlop << " slop, speed "
            << glm::length(box.body->getVelocity()) / tick << "\n";

  EXPECT_LE(clearing.ticksToClear, 5);
  EXPECT_LT(clearing.deepestLater, 5.0f * penetrationSlop);
  EXPECT_LT(gap, 0.05f);
  EXPECT_GT(distanceToSurface, 0.5f);
  EXPECT_LT(glm::length(box.body->getVelocity()), 1e-3f);

  // Positive control: the edit really put the box inside the ground, and it moved out of it.
  EXPECT_GT(clearing.firstTick, 1.0f);
  EXPECT_GT(glm::distance(box.position(), before), 3.0f);
}

TEST(TerrainResting, AHullDroppedPointDownIntoADimpleSettlesInTheDimple)
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

  const float tilt = glm::degrees(std::acos(std::clamp((hull.orientation() * glm::vec3(0, 1, 0)).y, -1.0f, 1.0f)));
  std::cout << "[gate] hull in a dimple: final tilt " << tilt << " degrees\n";

  EXPECT_LE(rest.settleTime, static_cast<float>(settleSeconds));
  EXPECT_LT(rest.drift, millimeter);
  EXPECT_LT(rest.deepest, 5.0f * penetrationSlop);
  // A body balanced on one vertex is unstable, so it topples onto an edge and rests there in the bowl.
  EXPECT_GT(tilt, 10.0f);
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

namespace {
  struct Costs {
    std::vector<double> terrainTimes;
    std::vector<double> floorTimes;
    std::vector<double> terrainWhole;
    std::vector<double> floorWhole;
  };

  // Times the two piles one after the other, so a slow moment on the machine hits both.
  Costs timeBoth(World& onTerrain, World& onFloor)
  {
    Costs costs;
    for (int i = 0; i < repetitions; ++i)
    {
      const Timing a = timeTicks(onTerrain);
      const Timing b = timeTicks(onFloor);
      costs.terrainTimes.push_back(a.collisions);
      costs.floorTimes.push_back(b.collisions);
      costs.terrainWhole.push_back(a.whole);
      costs.floorWhole.push_back(b.whole);
    }

    return costs;
  }

  double best(const std::vector<double>& values)
  {
    return *std::ranges::min_element(values);
  }

  void printCosts(const Costs& costs, const double ratio, const double wholeRatio)
  {
    std::cout << "[gate] cost of 200 resting boxes per tick: terrain " << best(costs.terrainTimes) << " ms, box floor "
              << best(costs.floorTimes) << " ms, collision ratio " << ratio << " (whole step " << wholeRatio
              << ", target 1.25)\n";
    for (int i = 0; i < repetitions; ++i)
    {
      std::cout << "[gate]   run " << i << ": terrain " << costs.terrainTimes[static_cast<size_t>(i)] << " ms, floor "
                << costs.floorTimes[static_cast<size_t>(i)] << " ms\n";
    }
  }
}

// Release builds compile only this suite's own sources without optimization (source/tests/CMakeLists.txt); the simulation
// library that does the timed work stays optimized.
TEST(TerrainResting, TheCostOfTwoHundredRestingBoxesIsMeasuredAgainstABoxFloor)
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

  const Costs costs = timeBoth(onTerrain, onFloor);
  const double ratio = best(costs.terrainTimes) / best(costs.floorTimes);
  printCosts(costs, ratio, best(costs.terrainWhole) / best(costs.floorWhole));
  RecordProperty("terrainMsPerTick", std::to_string(best(costs.terrainTimes)));
  RecordProperty("boxFloorMsPerTick", std::to_string(best(costs.floorTimes)));
  RecordProperty("collisionRatio", std::to_string(ratio));

  // Loose enough for a shared machine; the target is within 25 percent.
  EXPECT_LT(ratio, 3.0);

  // Positive control: both piles really were being solved, not skipped.
  EXPECT_GT(best(costs.terrainTimes), 0.0);
  EXPECT_GT(best(costs.floorTimes), 0.0);
}
