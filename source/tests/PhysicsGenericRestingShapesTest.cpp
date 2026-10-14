#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "PhysicsGenericRestingFixtures.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
  using namespace restingFixtures;
  using fixtures::transformOf;

  constexpr float dt = 0.1f;
  constexpr int ticks = 400;
  constexpr int settledWindow = 40;
  constexpr float groundTop = 1.0f;

  // A regular octagonal prism of radius one, standing on its axis.
  ConvexHull octagonalPrism()
  {
    std::vector<glm::vec3> points;
    for (int corner = 0; corner < 8; ++corner)
    {
      const float angle = glm::radians(45.0f * static_cast<float>(corner));
      points.emplace_back(std::cos(angle), -0.5f, std::sin(angle));
      points.emplace_back(std::cos(angle), 0.5f, std::sin(angle));
    }

    return *buildConvexHull(points);
  }

  struct Rest {
    float lowest = 0.0f;
    size_t touching = 0;
    float drift = 0.0f;
    float turned = 0.0f;
    float spin = 0.0f;
  };

  // Runs the drop, then measures the last settledWindow ticks: how far the body moved and turned in them, and how
  // many of its vertices lie on the ground.
  Rest dropAndMeasure(const ConvexHull& hull, const Launch& launch)
  {
    Rig rig(Shape::hull, Shape::box, launch, hull);
    const auto transform = transformOf(rig.body);
    const auto collider = rig.body->getComponent<ConvexMeshCollider>(ComponentType::collider);

    glm::vec3 position{ 0 };
    glm::vec3 rotation{ 0 };
    for (int tick = 0; tick < ticks; ++tick)
    {
      if (tick == ticks - settledWindow)
      {
        position = transform->getPosition();
        rotation = transform->getRotation();
      }
      rig.step(dt);
    }

    Rest rest;
    rest.drift = glm::length(transform->getPosition() - position);
    rest.turned = glm::length(transform->getRotation() - rotation);
    rest.spin = glm::length(rig.rigidBody->getAngularVelocity());

    const auto& vertices = collider->getWorldVertices();
    rest.lowest = vertices.front().y;
    for (const auto& vertex : vertices)
    {
      rest.lowest = std::min(rest.lowest, vertex.y);
    }
    rest.touching = static_cast<size_t>(std::ranges::count_if(vertices, [&rest](const glm::vec3& vertex)
    {
      return vertex.y < rest.lowest + 1e-3f;
    }));

    return rest;
  }

  void expectLyingOnAFace(const Rest& rest, const size_t faceCorners)
  {
    EXPECT_NEAR(rest.lowest, groundTop, 0.05f);
    EXPECT_GE(rest.touching, faceCorners);
    EXPECT_LT(rest.drift, 1e-4f);
    EXPECT_LT(rest.turned, 1e-3f);
    EXPECT_EQ(rest.spin, 0.0f);
  }
}

TEST(PhysicsGenericRestingShapes, TetrahedronSettlesOnAFaceAfterATiltedDrop)
{
  for (const glm::vec3 rotation : { glm::vec3(20, 0, 10), glm::vec3(130, 0, 20), glm::vec3(60, 30, 170) })
  {
    SCOPED_TRACE(rotation.x);
    expectLyingOnAFace(dropAndMeasure(convexFixtures::tetrahedron(), { { 0, 5, 0 }, rotation }), 3);
  }
}

TEST(PhysicsGenericRestingShapes, OctagonalPrismSettlesOnItsSideAfterATiltedDrop)
{
  for (const glm::vec3 rotation : { glm::vec3(0, 0, 100), glm::vec3(8, 0, 80), glm::vec3(0, 30, 90) })
  {
    SCOPED_TRACE(rotation.z);
    expectLyingOnAFace(dropAndMeasure(octagonalPrism(), { { 0, 5, 0 }, rotation }), 4);
  }
}

TEST(PhysicsGenericRestingShapes, OctagonalPrismSettlesOnAnEightCornerCapAfterATiltedDrop)
{
  for (const glm::vec3 rotation : { glm::vec3(20, 0, 10), glm::vec3(0, 0, 4), glm::vec3(35, 10, 45) })
  {
    SCOPED_TRACE(rotation.x);
    expectLyingOnAFace(dropAndMeasure(octagonalPrism(), { { 0, 5, 0 }, rotation }), 8);
  }
}
