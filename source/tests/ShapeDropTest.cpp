#include <gtest/gtest.h>

#include "CollisionSystem.h"
#include "DefaultProject.h"
#include "PhysicsSystem.h"
#include "TestScene.h"
#include "collisions/SupportFaces.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/RigidBody.h"
#include "objects/components/RotationConvention.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
  // The server's tick.
  constexpr float dt = 1.0f / 50.0f;
  constexpr int maxTicks = 600;
  constexpr float dropHeight = 3.0f;
  constexpr float restSpeed = 0.01f;
  constexpr float flushToleranceDegrees = 2.0f;

  const std::vector<std::string> shapeNames = {
    "D4", "D6", "D8", "D12", "D20", "Hex Prism", "Drum", "Gem", "Boulder"
  };

  const std::vector<glm::vec3> orientations = {
    { 0.0f, 0.0f, 0.0f }, { 20.0f, 0.0f, 35.0f }, { 60.0f, 45.0f, 10.0f }
  };

  const nlohmann::json& prefabBody(const std::string& name)
  {
    static const auto project = buildDefaultProject();

    for (const auto& prefab : project.at("assets").at("prefabs"))
    {
      if (prefab.at("name") == name)
      {
        return prefab.at("body");
      }
    }

    throw std::runtime_error("no prefab named " + name);
  }

  float lowestWorldY(ConvexMeshCollider& collider)
  {
    float lowest = std::numeric_limits<float>::max();
    for (const auto& v : collider.getWorldVertices())
    {
      lowest = std::min(lowest, v.y);
    }

    return lowest;
  }
}

class ShapeDrop : public ::testing::TestWithParam<std::tuple<size_t, size_t>> {};

TEST_P(ShapeDrop, ComesToRestFlatOnAFace)
{
  const auto& name = shapeNames[std::get<0>(GetParam())];
  const auto& rotation = orientations[std::get<1>(GetParam())];

  const auto scene = fixtures::makeScene();

  // Static floor with its top at y = 0.
  const auto floor = fixtures::addObject(scene, "Floor", { 0, -1, 0 }, { 200, 1, 200 });
  fixtures::addBoxCollider(floor);

  const auto shape = scene.objectManager->instantiate(prefabBody(name));
  ASSERT_NE(shape, nullptr);
  const auto body = shape->getComponent<RigidBody>(ComponentType::rigidBody);
  const auto collider = std::dynamic_pointer_cast<ConvexMeshCollider>(
    shape->getComponent<Collider>(ComponentType::collider));
  ASSERT_NE(body, nullptr);
  ASSERT_NE(collider, nullptr);

  const auto transform = fixtures::transformOf(shape);
  transform->setRotation(rotation);
  transform->setPosition({ 0.0f, dropHeight - lowestWorldY(*collider), 0.0f });
  ASSERT_NEAR(lowestWorldY(*collider), dropHeight, 1e-3f);

  CollisionSystem collisionSystem;
  for (int tick = 0; tick < maxTicks; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisionSystem.fixedUpdate(*scene.objectManager, dt);
  }

  EXPECT_LT(glm::length(body->getVelocity()), restSpeed);
  EXPECT_LT(glm::length(body->getAngularVelocity()), PhysicsSystem::restAngularSpeed);

  // It landed on the floor rather than coming to rest in the air or sinking through.
  EXPECT_NEAR(lowestWorldY(*collider), 0.0f, 0.05f);

  const auto faces = collisions::supportFacesOf(*collider);
  const auto* down = collisions::faceToward(faces, { 0.0f, -1.0f, 0.0f });
  ASSERT_NE(down, nullptr);

  const float cosine = std::clamp(glm::dot(down->normal, glm::vec3(0.0f, -1.0f, 0.0f)), -1.0f, 1.0f);
  EXPECT_LT(glm::degrees(std::acos(cosine)), flushToleranceDegrees);
}

INSTANTIATE_TEST_SUITE_P(Library, ShapeDrop,
  ::testing::Combine(::testing::Range<size_t>(0, shapeNames.size()),
                     ::testing::Range<size_t>(0, orientations.size())),
  [](const ::testing::TestParamInfo<std::tuple<size_t, size_t>>& info)
  {
    std::string name = shapeNames[std::get<0>(info.param)];
    std::erase(name, ' ');

    return name + "_Pose" + std::to_string(std::get<1>(info.param));
  });
