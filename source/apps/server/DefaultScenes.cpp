#include "DefaultProjectParts.h"
#include "DefaultTerrainCaves.h"
#include "DefaultTerrainTiles.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace defaultproject {

json buildScene1()
{
  return json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -10, 0 }, { 10, 1, 10 }),
    block({ 5, 5, 0 }),
    rigidBlock({ 15, -15, 0 }, { 10, 0.25f, 10 }, { 0, 0, 30 }),
    sphere({ 2, 0, 0 }),
    sphere({ 0, 2, 0 }),
    sphere({ 0, -2, 2 }),
    // Two players so an editor (slot 0) and a client (slot 1) each have their own camera POV when testing.
    // Each camera's direction aims back toward the scene (origin) from opposite sides.
    player({ 5, 0, 5 }, 0, glm::normalize(glm::vec3(-5, 0, -5))),
    player({ -5, 0, 5 }, 1, glm::normalize(glm::vec3(5, 0, -5)))
  });
}

json buildScene2()
{
  return json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -10, 0 }, { 10, 1, 10 }),
    rigidBlock({ 18, -5, 0 }, { 10, 1, 10 }, { 0, 0, 30 }),
    rigidBlock({ -18, -5, 0 }, { 10, 1, 10 }, { 0, 0, -30 }),
    block({ -22, 10, -3 }),
    sphere({ 2, 0, 3 }),
    player({ 5, 0, 5 }, 0, glm::normalize(glm::vec3(-5, 0, -5)))
  });
}

// Procedurally-generated tower of random spheres/blocks.
json buildScene3()
{
  constexpr int gridSize = 6;
  constexpr int gridHeight = 15;
  constexpr int ballSpacing = 5;

  json objects = json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -25, 25.0f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 25, 25.0f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -9, 0 }, { 100, 10, 100 })
  });

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.75f, 0.75f);
  std::uniform_real_distribution<float> sphereSize(0.25f, 1.5f);
  std::uniform_int_distribution<int> shouldUseSphere(0, 1);
  std::uniform_real_distribution<float> rot(0.0f, 360.0f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        constexpr float bottomY = 6.0f;
        constexpr float offsetXZ = (gridSize - 1) * ballSpacing / 2.0f;

        const float x = static_cast<float>(j) * ballSpacing + dist(gen) - offsetXZ;
        const float y = static_cast<float>(i) * ballSpacing + bottomY;
        const float z = static_cast<float>(k) * ballSpacing + dist(gen) - offsetXZ;

        if (shouldUseSphere(gen))
        {
          objects.push_back(sphere({ x, y, z }, glm::vec3(sphereSize(gen))));

          continue;
        }

        objects.push_back(block({ x, y, z },
          { sphereSize(gen), sphereSize(gen), sphereSize(gen) },
          { rot(gen), rot(gen), rot(gen) }));
      }
    }
  }

  return objects;
}

// Procedurally-generated grid of falling spheres.
json buildFallingBalls()
{
  constexpr int gridSize = 5;
  constexpr int gridHeight = 5;
  constexpr int ballSpacing = 3;

  json objects = json::array({
    light({ 0, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f }, 0.25f, 0.0f, 0.0f),
    light({ -10, -0.375f, 3 }, { 0.0f, 1.0f, 1.0f }, 0.0f, 0.75f, 0.75f),
    light({ 10, -0.375f, 3 }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.75f, 0.75f),
    rigidBlock({ 0, -15, 0 }, { 100, 3, 100 })
  });

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

  for (int i = 0; i < gridHeight; i++)
  {
    for (int j = 0; j < gridSize; j++)
    {
      for (int k = 0; k < gridSize; k++)
      {
        objects.push_back(sphere({
          static_cast<float>(j) * ballSpacing + dist(gen) - (gridSize * ballSpacing / 2.0f),
          static_cast<float>(i) * ballSpacing,
          static_cast<float>(k) * ballSpacing + dist(gen)
        }));
      }
    }
  }

  return objects;
}

namespace {

// How far a slot's body may be jittered in x and z, and the bounding radius a body may reach so that no two
// neighbors overlap at spawn even when their jitters oppose.
constexpr float stackJitter = 0.75f;
constexpr float bodyReach = basin::stackSpacing / 2.0f - stackJitter - 0.05f;

// A prefab the basin's stacks instantiate at a uniform scale, with its bounding radius at scale one.
struct ScaledBody
{
  const json* body;
  float radius;
};

float hullRadius(const ConvexHull& hull)
{
  float radius = 0.0f;
  for (const auto& vertex : hull.vertices)
  {
    radius = std::max(radius, glm::length(vertex));
  }

  return radius;
}

const std::vector<ScaledBody>& scaledBodies()
{
  static const std::vector<ScaledBody> bodies = []
  {
    // The capsule's collider is four units tall, so no point of it is farther than two from the center.
    std::vector<ScaledBody> list = { { &capsuleBody(), 2.0f }, { &cylinderBody(), hullRadius(cylinderHull()) } };
    for (const auto& shape : libraryShapes())
    {
      list.push_back({ &libraryBody(shape.name), hullRadius(shape.hull()) });
    }

    return list;
  }();

  return bodies;
}

float massOf(const json& body)
{
  for (const auto& component : body.at("components"))
  {
    if (component.at("type") == "RigidBody")
    {
      return component.at("mass").get<float>();
    }
  }

  throw std::runtime_error("no RigidBody in " + body.at("name").get<std::string>());
}

// One of the thirteen kinds: a Sphere, a Block, or a prefab at a uniform scale that keeps its density.
json randomBody(std::mt19937& gen, const glm::vec3& position)
{
  const auto& bodies = scaledBodies();
  std::uniform_int_distribution<size_t> pick(0, bodies.size() + 1);
  std::uniform_real_distribution<float> rot(0.0f, 360.0f);
  const glm::vec3 rotation(rot(gen), rot(gen), rot(gen));
  const size_t kind = pick(gen);

  if (kind == 0)
  {
    return sphere(position, glm::vec3(std::uniform_real_distribution<float>(0.25f, 1.5f)(gen)));
  }

  if (kind == 1)
  {
    // A block's reach is its corner, so its per-axis size stops short of the other kinds'.
    std::uniform_real_distribution<float> size(0.25f, bodyReach / std::sqrt(3.0f));

    return block(position, { size(gen), size(gen), size(gen) }, rotation);
  }

  const auto& [body, radius] = bodies[kind - 2];
  const float largest = bodyReach / radius;
  const float scale = std::uniform_real_distribution<float>(0.45f * largest, largest)(gen);

  return withMass(instanceOf(*body, position, glm::vec3(scale), rotation), massOf(*body) * scale * scale * scale);
}

// The highest ground, in world units, under a square of the given half width.
float highestGround(const float centerX, const float centerZ, const float halfWidth)
{
  float highest = basin::worldGroundY(centerX, centerZ);
  for (float dz = -halfWidth; dz <= halfWidth; dz += basin::voxelSize)
  {
    for (float dx = -halfWidth; dx <= halfWidth; dx += basin::voxelSize)
    {
      highest = std::max(highest, basin::worldGroundY(centerX + dx, centerZ + dz));
    }
  }

  return highest;
}

// A columns x columns x layers stack of random bodies centered over (centerX, centerZ), whose lowest layer is at
// bottomY.
void addStackAt(json& objects, std::mt19937& gen, const float centerX, const float centerZ, const float bottomY,
                const int columns, const int layers)
{
  std::uniform_real_distribution<float> jitter(-stackJitter, stackJitter);
  const float half = static_cast<float>(columns - 1) * basin::stackSpacing / 2.0f;

  for (int layer = 0; layer < layers; ++layer)
  {
    for (int j = 0; j < columns; ++j)
    {
      for (int k = 0; k < columns; ++k)
      {
        objects.push_back(randomBody(gen, {
          centerX + static_cast<float>(j) * basin::stackSpacing + jitter(gen) - half,
          bottomY + static_cast<float>(layer) * basin::stackSpacing,
          centerZ + static_cast<float>(k) * basin::stackSpacing + jitter(gen) - half
        }));
      }
    }
  }
}

// A stack starting six units above the highest ground under it.
void addStack(json& objects, std::mt19937& gen, const float centerX, const float centerZ, const int columns,
              const int layers)
{
  const float half = static_cast<float>(columns - 1) * basin::stackSpacing / 2.0f;
  const float bottomY = highestGround(centerX, centerZ, half + stackJitter + bodyReach) + 6.0f;

  addStackAt(objects, gen, centerX, centerZ, bottomY, columns, layers);
}

}

// Nine terrain tiles that join into one basin, a tower of mixed bodies over the arena and a smaller stack over each
// outer tile, plus a stack on top of and inside each of its caves and arches (basin::caves). The tower and tile stack
// sizes are the basin constants; the placement is random per launch.
json buildTerrainBasin()
{
  json objects = json::array();

  for (int row = 0; row < basin::tilesPerSide; ++row)
  {
    for (int col = 0; col < basin::tilesPerSide; ++col)
    {
      objects.push_back(makeObject("Terrain Tile " + std::to_string(row) + " " + std::to_string(col), json::array({
        transform(basin::tileOrigin(row, col)),
        terrainCollider(basinTerrains[static_cast<size_t>(row * basin::tilesPerSide + col)],
                        basin::tileOpenSides(row, col))
      })));
    }
  }

  objects.push_back(light({ 0.0f, 300.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 0.35f, 0.8f, 0.4f));
  objects.push_back(player({ 0.0f, 3.0f, -60.0f }, 0, glm::vec3(0.0f, 0.0f, -1.0f), 180.0f));

  std::random_device rd;
  std::mt19937 gen(rd());
  addStack(objects, gen, 0.0f, 0.0f, basin::towerColumns, basin::towerLayers);

  for (int row = 0; row < basin::tilesPerSide; ++row)
  {
    for (int col = 0; col < basin::tilesPerSide; ++col)
    {
      if (row == 1 && col == 1)
      {
        continue;
      }

      const glm::vec3 origin = basin::tileOrigin(row, col);
      addStack(objects, gen, origin.x + static_cast<float>(basin::tileStride / 2) * basin::voxelSize,
               origin.z + static_cast<float>(basin::tileStride / 2) * basin::voxelSize, basin::stackColumns,
               basin::stackLayers);
    }
  }

  for (const auto& stack : basin::caves::stacks())
  {
    addStackAt(objects, gen, stack.worldX, stack.worldZ, stack.surfaceY + basin::caves::stackLift, stack.columns, stack.layers);
  }

  return objects;
}

json scene(const std::string& name, const std::string& uuid, json objects)
{
  return {
    { "name", name },
    { "objects", std::move(objects) },
    { "uuid", uuid }
  };
}

}
