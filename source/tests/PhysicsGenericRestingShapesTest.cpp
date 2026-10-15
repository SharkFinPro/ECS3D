#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "PhysicsGenericRestingFixtures.h"
#include "PhysicsSystem.h"
#include "PhysicsTestHelpers.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {
  using namespace restingFixtures;
  using fixtures::transformOf;

  constexpr float dt = 0.1f;
  constexpr int ticks = 400;
  constexpr int rollTicks = 100;
  // Long enough to land and settle; a body left on this slope much longer creeps down it and drops through.
  constexpr int slopeTicks = 30;
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
    EXPECT_LT(rest.spin, PhysicsSystem::restAngularSpeed);
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

namespace {
  // A prism of the given number of sides and radius one, its axis along x and two units long, so it rolls along z.
  ConvexHull drum(const int sides)
  {
    std::vector<glm::vec3> points;
    for (int side = 0; side < sides; ++side)
    {
      const float angle = glm::radians(360.0f * static_cast<float>(side) / static_cast<float>(sides));
      points.emplace_back(-1.0f, std::cos(angle), std::sin(angle));
      points.emplace_back(1.0f, std::cos(angle), std::sin(angle));
    }

    return *buildConvexHull(points);
  }

  // Rolls without slipping along +z from z = -8 at a speed of rollSpeed units per tick.
  float distanceRolled(const ConvexHull& hull)
  {
    constexpr float rollSpeed = 0.3f;
    Rig rig(Shape::hull, Shape::box, { { 0, 2.0f, -8 }, { 0, 0, 0 }, { 0, 0, rollSpeed },
                                      { glm::degrees(rollSpeed / dt), 0, 0 } }, hull);
    for (int tick = 0; tick < rollTicks; ++tick)
    {
      rig.step(dt);
    }

    return transformOf(rig.body)->getPosition().z + 8.0f;
  }
}

TEST(PhysicsGenericRestingShapes, AFinelyFacetedDrumKeepsRollingWhereACubeLandsFlat)
{
  // A ball of the same radius and start stops within about 3.5 units; a hull has no rolling resistance, so a drum
  // rolls on until it leaves the ground, 18 units away, within the hundred ticks.
  EXPECT_GT(distanceRolled(drum(32)), 10.0f);
  EXPECT_GT(distanceRolled(drum(16)), 10.0f);

  // Positive controls: a drum of eight sides meets its facets at 45 degrees and lands on one, as a cube does.
  EXPECT_LT(distanceRolled(drum(8)), 3.0f);

  Rig cube(Shape::hull, Shape::box, { { 0, 2.0f, -8 }, { 0, 0, 0 }, { 0, 0, 0.3f }, { glm::degrees(0.3f / dt), 0, 0 } });
  for (int tick = 0; tick < rollTicks; ++tick)
  {
    cube.step(dt);
  }
  EXPECT_LT(transformOf(cube.body)->getPosition().z + 8.0f, 3.0f);
  EXPECT_GT(std::fabs(glm::dot(glm::mat3_cast(transformOf(cube.body)->getOrientation())[1], glm::vec3(0, 1, 0))), 0.9999f);
}

namespace {
  // A slab 20 by 20, rising 10 percent along x, its top the support's slanted face.
  ConvexHull slopedSlab()
  {
    const std::vector<glm::vec3> points{
      { -10, 0.0f, -10 }, { -10, 0.0f, 10 }, { 10, 2.0f, -10 }, { 10, 2.0f, 10 },
      { -10, -2, -10 }, { -10, -2, 10 }, { 10, -2, -10 }, { 10, -2, 10 }
    };

    return *buildConvexHull(points);
  }

  void expectRestsOnTheSlope(const Shape bodyShape)
  {
    Rig rig(bodyShape, Shape::hull, { { 0, 5, 0 }, { 10, 0, 5 } }, {}, slopedSlab());
    for (int tick = 0; tick < slopeTicks; ++tick)
    {
      rig.step(dt);
    }

    const glm::vec3 slopeNormal = glm::normalize(glm::vec3(-0.1f, 1.0f, 0.0f));
    const auto axes = glm::mat3_cast(transformOf(rig.body)->getOrientation());
    EXPECT_GT(std::max({ glm::dot(axes[0], slopeNormal), glm::dot(axes[1], slopeNormal), glm::dot(axes[2], slopeNormal) }),
              std::cos(glm::radians(0.5f)));
    EXPECT_LT(glm::length(rig.rigidBody->getAngularVelocity()), PhysicsSystem::restAngularSpeed);
  }
}

TEST(PhysicsGenericRestingShapes, ABoxAndAHullLieFlushOnASlantedHullFace)
{
  expectRestsOnTheSlope(Shape::box);
  expectRestsOnTheSlope(Shape::hull);
}

TEST(PhysicsGenericRestingShapes, AWidePlateComesFlatOnASmallerSupportFace)
{
  fixtures::Scene scene;
  const auto post = fixtures::addObject(scene, "Post", { 0, 0, 0 });
  fixtures::transformOf(post)->setScale({ 0.5f, 1, 0.5f });
  fixtures::addBoxCollider(post);

  const auto plate = fixtures::addObject(scene, "Plate", { 0, 3, 0 });
  fixtures::transformOf(plate)->setScale({ 2, 0.2f, 2 });
  fixtures::transformOf(plate)->setRotation({ 10, 0, 5 });
  attach(plate, Shape::hull);
  const auto body = physicsFixtures::addBody(plate, true);

  CollisionSystem collisions;
  for (int tick = 0; tick < ticks; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisions.fixedUpdate(*scene.objectManager, dt);
  }

  const auto axes = glm::mat3_cast(transformOf(plate)->getOrientation());
  EXPECT_GT(std::fabs(axes[1].y), 0.9999f);
  EXPECT_NEAR(transformOf(plate)->getPosition().y, 1.2f, 0.02f);
  EXPECT_LT(glm::length(body->getAngularVelocity()), PhysicsSystem::restAngularSpeed);
}

TEST(PhysicsGenericRestingShapes, AHullColliderWithARotationOffsetLandsFlush)
{
  fixtures::Scene scene;
  const auto ground = fixtures::addObject(scene, "Ground", { 0, 0, 0 });
  fixtures::transformOf(ground)->setScale({ 10, 1, 10 });
  fixtures::addBoxCollider(ground);

  const auto object = fixtures::addObject(scene, "Body", { 0, 5, 0 });
  fixtures::transformOf(object)->setRotation({ 20, 0, 10 });
  const auto collider = std::make_shared<ConvexMeshCollider>();
  collider->setRotation({ 0, 30, 0 });
  object->addComponent(collider);
  physicsFixtures::addBody(object, true);

  CollisionSystem collisions;
  for (int tick = 0; tick < ticks; ++tick)
  {
    PhysicsSystem::fixedUpdate(*scene.objectManager, dt);
    collisions.fixedUpdate(*scene.objectManager, dt);
  }

  const auto axes = glm::mat3_cast(glm::quat(glm::radians(collider->getRotation())));
  EXPECT_GT(std::max({ std::fabs(axes[0].y), std::fabs(axes[1].y), std::fabs(axes[2].y) }), 0.9999f);
}

namespace {
  enum class BodyKind { box, sphere, capsule };

  // The rotation of a body, tilted a little off flush, after one response resting it on a static box below.
  glm::vec3 rotationAfterRestingOnABox(const BodyKind kind)
  {
    const auto scene = fixtures::makeScene();
    const auto body = fixtures::addObject(scene, "Body", { 0, 0, 0 });
    switch (kind)
    {
      case BodyKind::box:
        fixtures::addBoxCollider(body);
        break;
      case BodyKind::sphere:
        fixtures::addSphereCollider(body, 0.5f);
        break;
      case BodyKind::capsule:
        fixtures::addCapsuleCollider(body, 0.5f, 2.0f);
        break;
    }
    const auto rigidBody = physicsFixtures::addBody(body, false);
    const auto ground = fixtures::addObject(scene, "Ground", { 0, -1, 0 });
    fixtures::addBoxCollider(ground);

    transformOf(body)->setRotation({ 0, 0, 0.016f });
    PhysicsSystem::handleCollision(*rigidBody, physicsFixtures::ownColliderOf(*rigidBody), ground,
                                   { 0, 0.01f, 0 }, physicsFixtures::flatUnderside, dt);

    return transformOf(body)->getRotation();
  }
}

TEST(PhysicsGenericRestingShapes, ASphereOrCapsuleHasNoFaceToLayFlush)
{
  EXPECT_EQ(rotationAfterRestingOnABox(BodyKind::sphere), glm::vec3(0, 0, 0.016f));
  EXPECT_EQ(rotationAfterRestingOnABox(BodyKind::capsule), glm::vec3(0, 0, 0.016f));

  // Positive control: the box under the same response is laid flush.
  EXPECT_LT(std::fabs(rotationAfterRestingOnABox(BodyKind::box).z), 1e-3f);
}
