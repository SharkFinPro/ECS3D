#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/FaceManifold.h"
#include "collisions/NarrowPhase.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
  using fixtures::addObject;
  using fixtures::transformOf;

  constexpr float parity = 1e-5f;

  enum class Kind { box, hull };

  struct Pose {
    const char* name;
    glm::vec3 position;
    glm::vec3 rotation;
    size_t manifoldPoints;
  };

  // A unit cube resting on the top face (y = 1) of the ground cube at the origin, 0.05 to 0.2 into it.
  const std::vector<Pose> poses{
    { "flat", { 0, 1.9f, 0 }, { 0, 0, 0 }, 4 },
    { "shifted over the edge", { 0.7f, 1.9f, 0.3f }, { 0, 0, 0 }, 4 },
    { "tilted half a degree", { 0.2f, 1.9f, -0.1f }, { 0, 0, 0.5f }, 2 },
    { "tilted a twentieth of a degree", { 0.2f, 1.9f, -0.1f }, { 0, 0, 0.05f }, 4 },
    { "tilted three degrees", { -0.3f, 1.9f, 0.1f }, { 3, 0, 0 }, 2 },
    { "on a 45 degree edge", { 0.1f, 2.36f, 0 }, { 0, 0, 45 }, 2 },
    { "on a corner", { 0.1f, 2.62f, 0.2f }, { 35, 0, 45 }, 1 },
    { "yawed 30 degrees", { 0.4f, 1.9f, 0.2f }, { 0, 30, 0 }, 4 },
    { "yawed and tilted", { 0.3f, 1.92f, 0.1f }, { 3, 30, 2 }, 2 }
  };

  std::shared_ptr<Collider> attach(const std::shared_ptr<Object>& object, const Kind kind)
  {
    if (kind == Kind::box)
    {
      return fixtures::addBoxCollider(object);
    }

    const auto hull = std::make_shared<ConvexMeshCollider>();
    object->addComponent(hull);

    return hull;
  }

  std::shared_ptr<Collider> colliderAt(const fixtures::Scene& scene, const Kind kind, const glm::vec3& position,
                                       const glm::vec3& rotation)
  {
    const auto object = addObject(scene, "Body", position);
    transformOf(object)->setRotation(rotation);

    return attach(object, kind);
  }

  struct Pair {
    fixtures::Scene scene;
    std::shared_ptr<Collider> ground;
    std::shared_ptr<Collider> body;
  };

  std::unique_ptr<Pair> makePair(const Kind groundKind, const Kind bodyKind, const Pose& pose)
  {
    auto pair = std::make_unique<Pair>();
    pair->ground = colliderAt(pair->scene, groundKind, { 0, 0, 0 }, { 0, 0, 0 });
    pair->body = colliderAt(pair->scene, bodyKind, pose.position, pose.rotation);

    return pair;
  }

  bool hasPointNear(std::span<const glm::vec3> points, const glm::vec3& expected, const float within)
  {
    return std::ranges::any_of(points, [&expected, within](const glm::vec3& point) {
      return glm::distance(point, expected) < within;
    });
  }

  void expectSameContact(const collisions::Contact& actual, const collisions::Contact& expected)
  {
    fixtures::expectNear("mtv", actual.minimumTranslationVector, expected.minimumTranslationVector, parity);
    fixtures::expectNear("point", actual.point, expected.point, parity);

    const auto actualPoints = actual.contactPoints();
    const auto expectedPoints = expected.contactPoints();
    ASSERT_EQ(actualPoints.size(), expectedPoints.size());

    for (const auto& point : expectedPoints)
    {
      EXPECT_TRUE(hasPointNear(actualPoints, point, parity)) << point.x << ", " << point.y << ", " << point.z;
    }
  }

  void expectParityWithBoxes(const Kind groundKind, const Kind bodyKind, const Pose& pose)
  {
    SCOPED_TRACE(std::string(pose.name) + (groundKind == Kind::hull ? ", hull ground" : ", box ground") +
                 (bodyKind == Kind::hull ? ", hull body" : ", box body"));

    const auto boxes = makePair(Kind::box, Kind::box, pose);
    const auto mixed = makePair(groundKind, bodyKind, pose);

    const auto boxBodyFirst = collisions::findContact(*boxes->body, *boxes->ground);
    const auto boxGroundFirst = collisions::findContact(*boxes->ground, *boxes->body);
    ASSERT_TRUE(boxBodyFirst.has_value());
    ASSERT_TRUE(boxGroundFirst.has_value());
    ASSERT_EQ(boxBodyFirst->contactPoints().size(), pose.manifoldPoints) << "the control must have its manifold";

    const auto bodyFirst = collisions::findContact(*mixed->body, *mixed->ground);
    const auto groundFirst = collisions::findContact(*mixed->ground, *mixed->body);
    ASSERT_TRUE(bodyFirst.has_value());
    ASSERT_TRUE(groundFirst.has_value());

    expectSameContact(*bodyFirst, *boxBodyFirst);
    expectSameContact(*groundFirst, *boxGroundFirst);
  }
}

class ManifoldParityPose : public ::testing::TestWithParam<size_t> {};

TEST_P(ManifoldParityPose, AHullBodyOnABoxGroundMakesTheContactABoxBodyDoes)
{
  expectParityWithBoxes(Kind::box, Kind::hull, poses[GetParam()]);
}

TEST_P(ManifoldParityPose, ABoxBodyOnAHullGroundMakesTheContactABoxGroundDoes)
{
  expectParityWithBoxes(Kind::hull, Kind::box, poses[GetParam()]);
}

TEST_P(ManifoldParityPose, AHullBodyOnAHullGroundMakesTheContactTwoBoxesDo)
{
  expectParityWithBoxes(Kind::hull, Kind::hull, poses[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(Poses, ManifoldParityPose, ::testing::Range<size_t>(0, poses.size()),
                         [](const ::testing::TestParamInfo<size_t>& info) {
                           std::string name = poses[info.param].name;
                           std::ranges::replace(name, ' ', '_');
                           return name;
                         });

TEST(ConvexMeshManifoldParity, ScaledAndRotatedHullsStillMatchScaledBoxes)
{
  const auto build = [](const Kind kind) {
    auto pair = std::make_unique<Pair>();
    pair->ground = attach(addObject(pair->scene, "Ground", { 0, 0, 0 }, { 2, 0.5f, 3 }), kind);

    const auto body = addObject(pair->scene, "Body", { 0.5f, 0.95f, 0.2f }, { 0.7f, 0.5f, 0.9f });
    transformOf(body)->setRotation({ 0, 25, 0 });
    pair->body = attach(body, kind);

    return pair;
  };

  const auto boxes = build(Kind::box);
  const auto hulls = build(Kind::hull);

  const auto expected = collisions::findContact(*boxes->body, *boxes->ground);
  const auto actual = collisions::findContact(*hulls->body, *hulls->ground);
  ASSERT_TRUE(expected.has_value());
  ASSERT_TRUE(actual.has_value());
  ASSERT_EQ(expected->pointCount, 4);

  expectSameContact(*actual, *expected);
}

TEST(ConvexMeshManifoldParity, AGeneralHullKeepsItsOwnDepthAndStaysInsideTheBoxFootprint)
{
  // A tetrahedron's base is a triangle at y = -1 before the transform, so on the cube it rests 0.05 deep.
  const auto scene = fixtures::makeScene();
  const auto ground = fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  const auto object = addObject(scene, "Tetrahedron", { 0.8f, 1.95f, 0 });
  const auto tetrahedron = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(tetrahedron->setHull(convexFixtures::tetrahedron()));
  object->addComponent(tetrahedron);

  const auto contact = collisions::findContact(*ground, *tetrahedron);
  ASSERT_TRUE(contact.has_value());
  ASSERT_EQ(contact->pointCount, 3);

  // The base corner at x 1.8 hangs past the cube's edge at x = 1 and is pulled back onto it, still at y 0.95.
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 1.0f, 0.95f, -1.0f }, 1e-3f));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { -0.2f, 0.95f, -1.0f }, 1e-3f));
  EXPECT_TRUE(hasPointNear(contact->contactPoints(), { 0.8f, 0.95f, 1.0f }, 1e-3f));

  // The control: asked the other way round the tetrahedron's base is the reference, which the box's own
  // face corners are clamped into, at the box face's depth.
  const auto reversed = collisions::findContact(*tetrahedron, *ground);
  ASSERT_TRUE(reversed.has_value());
  ASSERT_EQ(reversed->pointCount, 4);

  for (const auto& point : reversed->contactPoints())
  {
    EXPECT_NEAR(point.y, 1.0f, 1e-3f);
  }
}

TEST(ConvexMeshManifoldParity, AnApexDownTetrahedronTouchesAtOnePoint)
{
  const auto scene = fixtures::makeScene();
  const auto ground = fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  const auto object = addObject(scene, "Tetrahedron", { 0.25f, 1.95f, 0.5f });
  transformOf(object)->setRotation({ 0, 0, 180 });
  const auto tetrahedron = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(tetrahedron->setHull(convexFixtures::tetrahedron()));
  object->addComponent(tetrahedron);

  const auto contact = collisions::findContact(*tetrahedron, *ground);
  ASSERT_TRUE(contact.has_value());

  EXPECT_NEAR(contact->depth(), 0.05f, 1e-3f);
  fixtures::expectNear("point", contact->point, { 0.25f, 0.95f, 0.5f }, 1e-3f);
}

TEST(ConvexMeshManifoldParity, AHullFaceWithMoreThanFourCornersKeepsFourSpreadOutPoints)
{
  // A regular octagonal prism (radius 1, half-height 1) lying flat on the cube, 0.05 into it.
  std::vector<glm::vec3> points;
  for (int i = 0; i < 8; ++i)
  {
    const float angle = glm::radians(45.0f * static_cast<float>(i));
    points.emplace_back(std::cos(angle), -1.0f, std::sin(angle));
    points.emplace_back(std::cos(angle), 1.0f, std::sin(angle));
  }

  const auto scene = fixtures::makeScene();
  const auto ground = fixtures::addBoxCollider(addObject(scene, "Ground", { 0, 0, 0 }));
  const auto object = addObject(scene, "Prism", { 0.3f, 1.95f, 0.1f });
  const auto prism = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(prism->setHull(*buildConvexHull(points)));
  object->addComponent(prism);

  const auto contact = collisions::findContact(*ground, *prism);
  ASSERT_TRUE(contact.has_value());
  ASSERT_EQ(contact->pointCount, 4);

  for (const auto& point : contact->contactPoints())
  {
    EXPECT_NEAR(point.y, 0.95f, 1e-3f);
    EXPECT_LE(std::fabs(point.x), 1.0f + 1e-3f);
    EXPECT_LE(std::fabs(point.z), 1.0f + 1e-3f);
  }
}

TEST(ConvexMeshManifoldParity, HullFaceNormalsPointOutwardWhicheverWayTheTrianglesWind)
{
  const std::vector<glm::vec3> vertices{ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 2, 0 }, { 0, 0, 2 } };
  const std::vector<uint32_t> outward{ 0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3 };
  std::vector<uint32_t> inward = outward;
  for (size_t i = 0; i < inward.size(); i += 3)
  {
    std::swap(inward[i + 1], inward[i + 2]);
  }

  const glm::vec3 center{ 0.5f, 0.5f, 0.5f };
  for (const std::vector<uint32_t>* indices : std::initializer_list<const std::vector<uint32_t>*>{ &outward, &inward })
  {
    const auto normals = collisions::hullFaceNormals(vertices, *indices);
    ASSERT_EQ(normals.size(), 4u);

    for (size_t face = 0; face < normals.size(); ++face)
    {
      EXPECT_NEAR(glm::length(normals[face]), 1.0f, parity);
      EXPECT_LT(glm::dot(normals[face], center - vertices[(*indices)[face * 3]]), 0.0f) << face;
    }
  }

  const std::vector<uint32_t> degenerate{ 0, 0, 1, 0, 1, 2 };
  EXPECT_EQ(collisions::hullFaceNormals(vertices, degenerate).size(), 1u);
}

TEST(ConvexMeshManifoldParity, ANearlyCollinearTriangleHasNoFaceNormal)
{
  const std::vector<glm::vec3> vertices{ { 0, 0, 0 }, { 100, 0, 0 }, { 200, 1e-5f, 0 }, { 0, 100, 0 } };

  EXPECT_TRUE(collisions::hullFaceNormals(vertices, std::vector<uint32_t>{ 0, 1, 2 }).empty());

  // The control: the same first edge with a real third vertex has one.
  EXPECT_EQ(collisions::hullFaceNormals(vertices, std::vector<uint32_t>{ 0, 1, 3 }).size(), 1u);
}

TEST(ConvexMeshManifoldParity, ADomedFaceClampsAgainstItsOuterOutlineNotItsInteriorVertex)
{
  // The hull's top is a four-triangle fan about a center vertex 0.0002 above the corners, so its touching
  // set holds the center beside the four corners.
  const std::vector<glm::vec3> hullVertices{ { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 },
                                             { 0, 1.0002f, 0 }, { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 } };
  const std::vector<glm::vec3> hullNormals{ glm::normalize(glm::vec3(2e-4f, 1, 0)),
                                            glm::normalize(glm::vec3(-2e-4f, 1, 0)),
                                            glm::normalize(glm::vec3(0, 1, 2e-4f)),
                                            glm::normalize(glm::vec3(0, 1, -2e-4f)), { 0, -1, 0 } };

  // A small cube resting on it, all four bottom corners inside the outline but off its diagonals.
  std::vector<glm::vec3> boxVertices;
  for (const float x : { 0.4f, 0.6f })
  {
    for (const float y : { 1.0f, 1.2f })
    {
      for (const float z : { 0.1f, 0.3f })
      {
        boxVertices.emplace_back(x, y, z);
      }
    }
  }
  const std::vector<glm::vec3> boxNormals{ { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
                                           { 0, 0, -1 } };

  const auto points = collisions::referenceFaceManifold({ hullVertices, hullNormals, 1e-3f },
                                                        { boxVertices, boxNormals, 1e-3f }, { 0, 1, 0 }, 4);

  ASSERT_EQ(points.size(), 4u);
  for (const float x : { 0.4f, 0.6f })
  {
    for (const float z : { 0.1f, 0.3f })
    {
      EXPECT_TRUE(hasPointNear(points, { x, 1.0f, z }, 1e-4f)) << x << ", " << z;
    }
  }
}
