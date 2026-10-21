#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultProject.h"
#include "DefaultProjectParts.h"
#include "DefaultWelcomeTestHelpers.h"
#include "PhysicsSystem.h"
#include "TestScene.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/Collider.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

using namespace welcometest;

namespace {
  constexpr float dt = 1.0f / 50.0f;

  // The distance PhysicsSystem's gravity step moves a body in one tick (gravityStep in PhysicsSystem.cpp).
  constexpr float gravityStepPerTick = 9.81f * dt * 0.1f;

  bool isFinite(const glm::vec3& value)
  {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  }

  std::vector<std::shared_ptr<Object>> towerBlocks(const ObjectManager& objectManager)
  {
    std::vector<std::shared_ptr<Object>> blocks;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (object->getName() == "Tower Block")
      {
        blocks.push_back(object);
      }
    }

    return blocks;
  }

  bool isOnShelf(const std::shared_ptr<Object>& object)
  {
    const auto position = fixtures::positionOf(object);

    return object->getComponent<RigidBody>(ComponentType::rigidBody) && position.x > -19.0f && position.x < -15.0f
      && position.z > 17.5f && position.z < 26.5f;
  }

  std::vector<std::shared_ptr<Object>> shelfProps(const ObjectManager& objectManager)
  {
    std::vector<std::shared_ptr<Object>> props;
    for (const auto& object : objectManager.getAllObjects())
    {
      if (isOnShelf(object))
      {
        props.push_back(object);
      }
    }

    return props;
  }

  // Zero when the pair does not touch, which is also what exactly touching colliders report.
  float contactDepth(const std::shared_ptr<Object>& object, const std::shared_ptr<Object>& other)
  {
    auto first = object->getComponent<Collider>(ComponentType::collider);
    auto second = other->getComponent<Collider>(ComponentType::collider);
    const auto contact = collisions::findContact(*first, *second);

    return contact ? contact->depth() : 0.0f;
  }

  void tick(const ObjectManager& objectManager, const int count)
  {
    CollisionSystem collisionSystem;
    for (int i = 0; i < count; ++i)
    {
      PhysicsSystem::fixedUpdate(objectManager, dt);
      collisionSystem.fixedUpdate(objectManager, dt);
    }
  }

  // The hull's volume centroid, from tetrahedra to its triangles.
  glm::vec3 centroidOf(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices)
  {
    glm::vec3 weighted(0.0f);
    float volume = 0.0f;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
      const auto& a = vertices[indices[i]];
      const auto& b = vertices[indices[i + 1]];
      const auto& c = vertices[indices[i + 2]];
      const float tetrahedron = glm::dot(a, glm::cross(b, c)) / 6.0f;
      weighted += tetrahedron * (a + b + c) / 4.0f;
      volume += tetrahedron;
    }

    return weighted / volume;
  }

  // Convex outline of the points, counter-clockwise (monotone chain).
  std::vector<glm::vec2> outline(std::vector<glm::vec2> points)
  {
    std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
      return a.x < b.x || (a.x == b.x && a.y < b.y);
    });

    const auto turn = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
      return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    std::vector<glm::vec2> hull;
    for (int pass = 0; pass < 2; ++pass)
    {
      const size_t start = hull.size();
      for (const auto& point : points)
      {
        while (hull.size() >= start + 2 && turn(hull[hull.size() - 2], hull.back(), point) <= 0.0f)
        {
          hull.pop_back();
        }

        hull.push_back(point);
      }

      hull.pop_back();
      std::reverse(points.begin(), points.end());
    }

    return hull;
  }

  // Distance from the centroid's footprint to the nearest edge of the resting face; negative when outside it.
  float restingMargin(const std::shared_ptr<Object>& prop, const ConvexHull& hull)
  {
    const auto transform = fixtures::transformOf(prop);
    const auto orientation = transform->getOrientation();
    const auto scale = transform->getScale();

    std::vector<glm::vec3> world;
    for (const auto& vertex : hull.vertices)
    {
      world.push_back(orientation * (vertex * scale));
    }

    const auto centroid = centroidOf(world, hull.indices);
    const float lowest = std::min_element(world.begin(), world.end(), [](const glm::vec3& a, const glm::vec3& b) {
      return a.y < b.y;
    })->y;

    std::vector<glm::vec2> face;
    for (const auto& vertex : world)
    {
      if (vertex.y < lowest + 1e-3f)
      {
        face.emplace_back(vertex.x, vertex.z);
      }
    }

    const auto polygon = outline(face);
    if (polygon.size() < 3)
    {
      return -1.0f;
    }

    float margin = std::numeric_limits<float>::max();
    for (size_t i = 0; i < polygon.size(); ++i)
    {
      const auto& p = polygon[i];
      const auto edge = polygon[(i + 1) % polygon.size()] - p;
      const glm::vec2 inward = glm::normalize(glm::vec2(-edge.y, edge.x));
      margin = std::min(margin, glm::dot(glm::vec2(centroid.x, centroid.z) - p, inward));
    }

    return margin;
  }
}

TEST(DefaultWelcome, NoPairOfCollidersStartsOverlapping)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  std::vector<std::shared_ptr<Object>> withColliders;
  for (const auto& object : objectManager.getAllObjects())
  {
    if (object->getComponent<Collider>(ComponentType::collider))
    {
      withColliders.push_back(object);
    }
  }

  ASSERT_GT(withColliders.size(), 30u);

  for (size_t i = 0; i < withColliders.size(); ++i)
  {
    for (size_t j = i + 1; j < withColliders.size(); ++j)
    {
      EXPECT_LT(contactDepth(withColliders[i], withColliders[j]), 0.01f)
        << withColliders[i]->getName() << " / " << withColliders[j]->getName();
    }
  }
}

TEST(DefaultWelcome, ItSettlesAndTheTowerStands)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  const auto blocks = towerBlocks(objectManager);
  ASSERT_EQ(blocks.size(), 24u);

  std::vector<glm::vec3> startPositions;
  std::vector<glm::quat> startOrientations;
  for (const auto& block : blocks)
  {
    startPositions.push_back(fixtures::positionOf(block));
    startOrientations.push_back(fixtures::transformOf(block)->getOrientation());
  }

  tick(objectManager, 300);

  for (const auto& object : objectManager.getAllObjects())
  {
    SCOPED_TRACE(object->getName());
    const auto position = fixtures::positionOf(object);
    EXPECT_TRUE(isFinite(position));
    EXPECT_GT(position.y, -50.0f);
  }

  for (size_t i = 0; i < blocks.size(); ++i)
  {
    SCOPED_TRACE(i);
    const auto moved = fixtures::positionOf(blocks[i]) - startPositions[i];
    EXPECT_LT(glm::length(glm::vec2(moved.x, moved.z)), 0.05f);

    // Each box rests one gravity step low on the one below, so the allowance grows with the level.
    const float level = std::round(startPositions[i].y - 0.5f);
    EXPECT_LT(-moved.y, (level + 1.0f) * gravityStepPerTick + 0.01f);
    EXPECT_GT(moved.y, -1.0f);

    const float alignment = std::min(1.0f, std::abs(glm::dot(fixtures::transformOf(blocks[i])->getOrientation(),
                                                              startOrientations[i])));
    EXPECT_LT(glm::degrees(2.0f * std::acos(alignment)), 2.0f);
  }
}

TEST(DefaultWelcome, TheTowerBlocksTouchTheGround)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto ground = objectNamed(objectManager, "Ground");
  ASSERT_NE(ground, nullptr);

  size_t grounded = 0;
  for (const auto& block : towerBlocks(objectManager))
  {
    if (fixtures::positionOf(block).y < 1.0f)
    {
      EXPECT_LT(contactDepth(block, ground), 0.01f);
      EXPECT_NEAR(fixtures::positionOf(block).y, 0.5f, 1e-4f);
      ++grounded;
    }
  }

  EXPECT_EQ(grounded, 4u);
}

TEST(DefaultWelcome, TheShelfPropsTouchTheShelfAndStayPut)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();
  const auto shelf = objectNamed(objectManager, "Shape Shelf");
  ASSERT_NE(shelf, nullptr);

  const auto props = shelfProps(objectManager);
  ASSERT_EQ(props.size(), 9u);

  std::vector<float> startHeights;
  for (const auto& prop : props)
  {
    EXPECT_LT(contactDepth(prop, shelf), 0.01f) << prop->getName();
    startHeights.push_back(fixtures::positionOf(prop).y);
  }

  // A prop that started in the air would have dropped by now; one that started sunk would have been pushed up.
  tick(objectManager, 50);

  for (size_t i = 0; i < props.size(); ++i)
  {
    EXPECT_NEAR(fixtures::positionOf(props[i]).y, startHeights[i], 0.02f) << props[i]->getName();
  }
}

TEST(DefaultWelcome, EveryShelfPropRestsWithItsCenterOfMassOverItsFace)
{
  LoadedProject loaded;
  load(loaded, buildDefaultProject());
  const auto& objectManager = *loaded.sceneManager.getCurrentScene()->getObjectManager();

  size_t checked = 0;
  for (const auto& prop : shelfProps(objectManager))
  {
    for (const auto& shape : defaultproject::libraryShapes())
    {
      if (shape.name == prop->getName())
      {
        // 0.05 at unit scale, and a prop is half size.
        EXPECT_GE(restingMargin(prop, shape.hull()), 0.025f - 1e-3f) << shape.name;
        ++checked;
      }
    }
  }

  EXPECT_EQ(checked, 9u);
}
