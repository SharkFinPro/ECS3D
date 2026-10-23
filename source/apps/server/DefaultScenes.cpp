#include "DefaultProjectParts.h"
#include <nlohmann/json.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <random>
#include <string>
#include <utility>

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

json scene(const std::string& name, const std::string& uuid, json objects)
{
  return {
    { "name", name },
    { "objects", std::move(objects) },
    { "uuid", uuid }
  };
}

}
