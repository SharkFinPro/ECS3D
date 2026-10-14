#include <gtest/gtest.h>

#include "ConvexMeshFixtures.h"
#include "TestPrinters.h"
#include "TestScene.h"
#include "collisions/SupportFaces.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/ConvexHull.h"
#include "objects/components/collisions/ConvexHullValidation.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
  using collisions::SupportFace;
  using convexFixtures::expectNear;
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  // Copies of the box helpers PhysicsSystem builds its resting logic from today, as the reference these faces
  // must match.
  struct ReferenceBox {
    glm::vec3 center;
    glm::mat3 axes;
    glm::vec3 halfExtents;
  };

  ReferenceBox referenceOf(const std::shared_ptr<BoxCollider>& collider)
  {
    return { collider->getPosition(), glm::mat3_cast(glm::quat(glm::radians(collider->getRotation()))),
             glm::abs(collider->getScale()) };
  }

  int nearestAxis(const glm::mat3& axes, const glm::vec3& direction)
  {
    int nearest = 0;
    for (int i = 1; i < 3; ++i)
    {
      if (std::fabs(glm::dot(axes[i], direction)) > std::fabs(glm::dot(axes[nearest], direction)))
      {
        nearest = i;
      }
    }

    return nearest;
  }

  glm::vec3 faceNormalToward(const glm::mat3& axes, const glm::vec3& direction)
  {
    const auto nearest = axes[nearestAxis(axes, direction)];

    return glm::dot(nearest, direction) < 0.0f ? -nearest : nearest;
  }

  std::array<glm::vec3, 4> faceCorners(const ReferenceBox& box, const glm::vec3& direction)
  {
    const int face = nearestAxis(box.axes, direction);
    const int u = (face + 1) % 3;
    const int v = (face + 2) % 3;

    const auto faceCenter = box.center + faceNormalToward(box.axes, direction) * box.halfExtents[face];
    const auto alongU = box.axes[u] * box.halfExtents[u];
    const auto alongV = box.axes[v] * box.halfExtents[v];

    return { faceCenter - alongU - alongV, faceCenter + alongU - alongV, faceCenter + alongU + alongV,
             faceCenter - alongU + alongV };
  }

  glm::vec3 clampToFootprint(const ReferenceBox& box, const glm::vec3& point, const glm::vec3& normal)
  {
    const int normalAxis = nearestAxis(box.axes, normal);

    auto clamped = box.center;
    for (int i = 0; i < 3; ++i)
    {
      const float coordinate = glm::dot(point - box.center, box.axes[i]);
      const float kept = i == normalAxis ? coordinate : std::clamp(coordinate, -box.halfExtents[i], box.halfExtents[i]);
      clamped += kept * box.axes[i];
    }

    return clamped;
  }

  void expectSameCorners(const std::vector<glm::vec3>& actual, const std::vector<glm::vec3>& expected)
  {
    ASSERT_EQ(actual.size(), expected.size());
    for (const auto& corner : expected)
    {
      const bool found = std::ranges::any_of(actual, [&corner](const glm::vec3& candidate) {
        return glm::distance(candidate, corner) < 1e-3f;
      });
      EXPECT_TRUE(found) << "no corner near (" << corner.x << ", " << corner.y << ", " << corner.z << ")";
    }
  }

  const SupportFace* matchingFace(const std::vector<SupportFace>& faces, const glm::vec3& normal)
  {
    const auto found = std::ranges::find_if(faces, [&normal](const SupportFace& face) {
      return glm::dot(face.normal, normal) > 1.0f - 1e-4f;
    });

    return found == faces.end() ? nullptr : &*found;
  }

  // Counter-clockwise seen from outside, with the center inside the polygon's plane on the normal's side.
  void expectWoundOutward(const SupportFace& face, const glm::vec3& interior)
  {
    EXPECT_NEAR(glm::length(face.normal), 1.0f, 1e-4f);
    EXPECT_GT(glm::dot(face.normal, face.center - interior), 0.0f);

    for (size_t i = 0; i < face.corners.size(); ++i)
    {
      const auto& a = face.corners[i];
      const auto& b = face.corners[(i + 1) % face.corners.size()];
      const auto& c = face.corners[(i + 2) % face.corners.size()];
      EXPECT_GT(glm::dot(glm::cross(b - a, c - b), face.normal), 0.0f);
      EXPECT_NEAR(glm::dot(a - face.center, face.normal), 0.0f, 1e-3f);
    }
  }

  struct Rig
  {
    fixtures::Scene scene = makeScene();
    std::shared_ptr<BoxCollider> box;
    std::shared_ptr<ConvexMeshCollider> hull;
  };

  void place(const std::shared_ptr<Object>& object, const int placement)
  {
    const auto transform = transformOf(object);
    if (placement == 0)
    {
      return;
    }

    transform->setPosition({ 1.0f, 2.0f, 3.0f });
    if (placement == 1 || placement == 3)
    {
      transform->setRotation({ 10.0f, 20.0f, 30.0f });
    }

    if (placement == 2 || placement == 3)
    {
      transform->setScale({ 1.5f, 2.0f, 0.75f });
    }
  }

  // placement: 0 none, 1 rotated about several axes, 2 scaled, 3 rotated and scaled, 4 under a rotated and
  // scaled parent.
  void build(Rig& rig, const int placement)
  {
    std::shared_ptr<Object> boxObject;
    std::shared_ptr<Object> hullObject;
    if (placement == 4)
    {
      const auto parent = addObject(rig.scene, "Parent", { 4.0f, -1.0f, 2.0f });
      transformOf(parent)->setRotation({ 0.0f, 30.0f, 15.0f });
      transformOf(parent)->setScale({ 2.0f, 1.0f, 1.5f });
      boxObject = addChildObject(rig.scene, "Box", parent);
      hullObject = addChildObject(rig.scene, "Hull", parent);
      place(boxObject, 3);
      place(hullObject, 3);
    }
    else
    {
      boxObject = addObject(rig.scene, "Box");
      hullObject = addObject(rig.scene, "Hull");
      place(boxObject, placement);
      place(hullObject, placement);
    }

    rig.box = fixtures::addBoxCollider(boxObject);
    rig.hull = std::make_shared<ConvexMeshCollider>();
    hullObject->addComponent(rig.hull);
  }

  constexpr int placementCount = 5;

  const std::array<glm::vec3, 8> directions{ {
    { 0.3f, 0.5f, 0.8f }, { -0.7f, 0.2f, -0.4f }, { 0.1f, -0.9f, 0.35f }, { -0.45f, -0.3f, 0.6f },
    { 0.9f, 0.15f, -0.2f }, { -0.1f, 0.7f, 0.55f }, { 0.0f, -1.0f, 0.0f }, { 0.2f, -0.1f, -0.95f }
  } };

  const std::array<glm::vec3, 6> points{ {
    { 0.0f, 0.0f, 0.0f }, { 5.0f, -3.0f, 2.0f }, { -4.0f, 1.0f, 7.0f }, { 1.2f, 2.3f, 3.4f },
    { 9.0f, 9.0f, -9.0f }, { -2.5f, -6.0f, 0.5f }
  } };

  const std::vector<glm::vec3> tetrahedronVertices{ { -1, -1, -1 }, { 1, -1, -1 }, { 0, -1, 1 }, { 0, 1, 0 } };
}

TEST(SupportFaces, BoxAndDefaultHullHaveTheSameFaces)
{
  for (int placement = 0; placement < placementCount; ++placement)
  {
    SCOPED_TRACE(placement);
    Rig rig;
    build(rig, placement);

    const auto boxFaces = collisions::supportFacesOf(*rig.box);
    const auto hullFaces = collisions::supportFacesOf(*rig.hull);
    ASSERT_EQ(boxFaces.size(), 6u);
    ASSERT_EQ(hullFaces.size(), 6u);

    for (const auto& boxFace : boxFaces)
    {
      const auto* hullFace = matchingFace(hullFaces, boxFace.normal);
      ASSERT_NE(hullFace, nullptr);
      expectNear("center", hullFace->center, boxFace.center);
      expectSameCorners(hullFace->corners, boxFace.corners);
      EXPECT_EQ(boxFace.corners.size(), 4u);
    }
  }
}

TEST(SupportFaces, FacesAreWoundOutward)
{
  for (int placement = 0; placement < placementCount; ++placement)
  {
    SCOPED_TRACE(placement);
    Rig rig;
    build(rig, placement);

    const auto boxCenter = rig.box->getPosition();
    for (const auto& face : collisions::supportFacesOf(*rig.box))
    {
      expectWoundOutward(face, boxCenter);
    }

    const auto hullCenter = rig.hull->getPosition();
    for (const auto& face : collisions::supportFacesOf(*rig.hull))
    {
      expectWoundOutward(face, hullCenter);
    }
  }
}

TEST(SupportFaces, FaceTowardMatchesTheBoxHelpers)
{
  for (int placement = 0; placement < placementCount; ++placement)
  {
    SCOPED_TRACE(placement);
    Rig rig;
    build(rig, placement);

    const auto reference = referenceOf(rig.box);
    const auto boxFaces = collisions::supportFacesOf(*rig.box);
    const auto hullFaces = collisions::supportFacesOf(*rig.hull);

    for (const auto& direction : directions)
    {
      const auto expectedNormal = faceNormalToward(reference.axes, direction);
      const auto* boxFace = collisions::faceToward(boxFaces, direction);
      const auto* hullFace = collisions::faceToward(hullFaces, direction);
      ASSERT_NE(boxFace, nullptr);
      ASSERT_NE(hullFace, nullptr);

      expectNear("box normal", boxFace->normal, expectedNormal);
      expectNear("hull normal", hullFace->normal, expectedNormal);

      const auto expectedCorners = faceCorners(reference, direction);
      const std::vector<glm::vec3> expected(expectedCorners.begin(), expectedCorners.end());
      expectSameCorners(boxFace->corners, expected);
      expectSameCorners(hullFace->corners, expected);
    }
  }
}

TEST(SupportFaces, ClampToFaceMatchesTheBoxFootprint)
{
  for (int placement = 0; placement < placementCount; ++placement)
  {
    SCOPED_TRACE(placement);
    Rig rig;
    build(rig, placement);

    const auto reference = referenceOf(rig.box);
    const auto boxFaces = collisions::supportFacesOf(*rig.box);
    const auto hullFaces = collisions::supportFacesOf(*rig.hull);

    for (const auto& direction : directions)
    {
      const auto* boxFace = collisions::faceToward(boxFaces, direction);
      const auto* hullFace = collisions::faceToward(hullFaces, direction);

      for (const auto& point : points)
      {
        const auto expected = clampToFootprint(reference, point, direction);
        expectNear("box", collisions::clampToFace(*boxFace, point), expected);
        expectNear("hull", collisions::clampToFace(*hullFace, point), expected);
      }
    }
  }
}

TEST(SupportFaces, ClampToFaceMovesAnOutsidePointAndKeepsAnInsideOne)
{
  Rig rig;
  build(rig, 0);
  const auto faces = collisions::supportFacesOf(*rig.hull);
  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);

  expectNear("inside", collisions::clampToFace(*top, { 0.25f, 3.0f, -0.5f }), { 0.25f, 3.0f, -0.5f });
  expectNear("outside", collisions::clampToFace(*top, { 4.0f, 3.0f, -0.5f }), { 1.0f, 3.0f, -0.5f });
}

TEST(SupportFaces, TetrahedronHasFourTriangles)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Tetrahedron");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(hull->setHull(convexFixtures::tetrahedron()));
  object->addComponent(hull);

  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 4u);

  glm::vec3 centroid{ 0.0f };
  for (const auto& vertex : tetrahedronVertices)
  {
    centroid += vertex / 4.0f;
  }

  for (const auto& face : faces)
  {
    EXPECT_EQ(face.corners.size(), 3u);
    expectWoundOutward(face, centroid);
  }

  const auto* bottom = collisions::faceToward(faces, { 0.0f, -1.0f, 0.0f });
  ASSERT_NE(bottom, nullptr);
  expectNear("bottom normal", bottom->normal, { 0.0f, -1.0f, 0.0f });
  expectSameCorners(bottom->corners, { tetrahedronVertices[0], tetrahedronVertices[1], tetrahedronVertices[2] });
  expectNear("outside the triangle", collisions::clampToFace(*bottom, { 3.0f, -1.5f, -1.0f }),
             { 1.0f, -1.5f, -1.0f });
  expectNear("inside the triangle", collisions::clampToFace(*bottom, { 0.0f, -1.5f, -0.5f }),
             { 0.0f, -1.5f, -0.5f });
}

TEST(SupportFaces, CoplanarTrianglesFromAnyTriangulationMergeIntoOneQuad)
{
  std::vector<glm::vec3> cube;
  for (uint32_t i = 0; i < 8; ++i)
  {
    cube.emplace_back((i & 1u) ? 1.0f : -1.0f, (i & 2u) ? 1.0f : -1.0f, (i & 4u) ? 1.0f : -1.0f);
  }

  const auto built = buildConvexHull(cube);
  ASSERT_TRUE(built.has_value());
  EXPECT_EQ(built->indices.size(), 36u);

  const auto scene = makeScene();
  const auto object = addObject(scene, "Cube");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(hull->setHull(*built));
  object->addComponent(hull);

  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 6u);
  for (const auto& face : faces)
  {
    EXPECT_EQ(face.corners.size(), 4u);
  }

  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);
  expectSameCorners(top->corners, { { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } });
  expectNear("top center", top->center, { 0.0f, 1.0f, 0.0f });
}

TEST(SupportFaces, TrianglesWithNoAreaAreIgnored)
{
  auto cube = makeUnitCubeHull();
  cube.vertices.emplace_back(0.0f, -1.0f, -1.0f);
  cube.indices.insert(cube.indices.end(), { 0, 0, 1,  0, 8, 1,  2, 2, 2 });

  const auto scene = makeScene();
  const auto object = addObject(scene, "Odd cube");
  const auto hull = std::make_shared<ConvexMeshCollider>();
  ASSERT_TRUE(hull->setHull(cube));
  object->addComponent(hull);

  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 6u);
  for (const auto& face : faces)
  {
    EXPECT_EQ(face.corners.size(), 4u);
  }
}

TEST(SupportFaces, MirroredHullKeepsOutwardFaces)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Mirrored", { 1.0f, 2.0f, 3.0f }, { -1.0f, 2.0f, 1.0f });
  const auto hull = std::make_shared<ConvexMeshCollider>();
  object->addComponent(hull);

  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 6u);
  for (const auto& face : faces)
  {
    expectWoundOutward(face, { 1.0f, 2.0f, 3.0f });
  }
}

TEST(SupportFaces, SpheresAndCapsulesHaveNoFaces)
{
  const auto scene = makeScene();
  const auto boxObject = addObject(scene, "Box");
  const auto box = fixtures::addBoxCollider(boxObject);
  const auto sphere = fixtures::addSphereCollider(addObject(scene, "Sphere"), 1.0f);
  const auto capsule = fixtures::addCapsuleCollider(addObject(scene, "Capsule"), 0.5f, 2.0f);

  EXPECT_EQ(collisions::supportFacesOf(*box).size(), 6u);
  EXPECT_TRUE(collisions::supportFacesOf(*sphere).empty());
  EXPECT_TRUE(collisions::supportFacesOf(*capsule).empty());
  EXPECT_EQ(collisions::faceToward(collisions::supportFacesOf(*sphere), { 0.0f, 1.0f, 0.0f }), nullptr);
}

namespace {
  std::shared_ptr<ConvexMeshCollider> hullOn(const fixtures::Scene& scene, const ConvexHull& hull,
                                             const glm::vec3& position = glm::vec3(0.0f),
                                             const glm::vec3& scale = glm::vec3(1.0f))
  {
    const auto object = addObject(scene, "Hull", position, scale);
    const auto collider = std::make_shared<ConvexMeshCollider>();
    EXPECT_TRUE(collider->setHull(hull));
    object->addComponent(collider);

    return collider;
  }

  void expectSixQuads(ConvexMeshCollider& hull)
  {
    const auto faces = collisions::supportFacesOf(hull);
    ASSERT_EQ(faces.size(), 6u);
    for (const auto& face : faces)
    {
      EXPECT_EQ(face.corners.size(), 4u);
    }
  }

  // Four vertices of the cube's y = 1 face are 2, 3, 6 and 7.
  ConvexHull cubeWithTopTriangle(const glm::vec3& extraVertex, const std::array<uint32_t, 3>& triangle)
  {
    auto cube = makeUnitCubeHull();
    cube.vertices.push_back(extraVertex);
    cube.indices.insert(cube.indices.end(), triangle.begin(), triangle.end());

    return cube;
  }
}

TEST(SupportFaces, AZeroAreaSliverInsideAFaceKeepsTheFace)
{
  const auto scene = makeScene();
  const auto hull = hullOn(scene, cubeWithTopTriangle({ 0.0f, 1.0f, 0.0f }, { 2, 8, 7 }));

  expectSixQuads(*hull);
}

TEST(SupportFaces, ANeedleTriangleOnAFaceKeepsTheFace)
{
  const auto scene = makeScene();
  const auto hull = hullOn(scene, cubeWithTopTriangle({ 1e-4f, 1.0f, -1e-4f }, { 2, 8, 7 }));

  expectSixQuads(*hull);
}

TEST(SupportFaces, AHullFarFromTheOriginStillMerges)
{
  const auto scene = makeScene();
  const auto hull = hullOn(scene, makeUnitCubeHull(), { 5000.0f, 0.0f, 0.0f }, glm::vec3(0.5f));

  expectSixQuads(*hull);
  const auto faces = collisions::supportFacesOf(*hull);
  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);
  expectNear("top normal", top->normal, { 0.0f, 1.0f, 0.0f }, 5e-3f);
}

TEST(SupportFaces, FacesFiveDegreesApartDoNotMerge)
{
  const float ridge = 1.0f + std::tan(glm::radians(5.0f));
  const std::vector<glm::vec3> roof{
    { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 },
    { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 },
    { 0, ridge, -1 }, { 0, ridge, 1 }
  };
  const auto built = buildConvexHull(roof);
  ASSERT_TRUE(built.has_value());

  const auto scene = makeScene();
  const auto hull = hullOn(scene, *built);
  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 7u);

  const auto up = std::ranges::count_if(faces, [](const SupportFace& face) { return face.normal.y > 0.9f; });
  EXPECT_EQ(up, 2);
}

TEST(SupportFaces, AFaceWithMoreThanFourCornersIsOnePolygon)
{
  std::vector<glm::vec3> prism;
  for (int i = 0; i < 6; ++i)
  {
    const float angle = glm::radians(60.0f * static_cast<float>(i));
    prism.emplace_back(std::cos(angle), -1.0f, std::sin(angle));
    prism.emplace_back(std::cos(angle), 1.0f, std::sin(angle));
  }
  const auto built = buildConvexHull(prism);
  ASSERT_TRUE(built.has_value());

  const auto scene = makeScene();
  const auto hull = hullOn(scene, *built);
  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 8u);

  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);
  EXPECT_EQ(top->corners.size(), 6u);
  expectWoundOutward(*top, { 0.0f, 0.0f, 0.0f });
  expectNear("top center", top->center, { 0.0f, 1.0f, 0.0f });
}

TEST(SupportFaces, ClampToFaceNearAHullCornerLandsOnTheCorner)
{
  const auto scene = makeScene();
  const auto hull = hullOn(scene, makeUnitCubeHull());
  const auto faces = collisions::supportFacesOf(*hull);
  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);

  expectNear("corner", collisions::clampToFace(*top, { 3.0f, 2.0f, 3.0f }), { 1.0f, 2.0f, 1.0f });
  expectNear("other corner", collisions::clampToFace(*top, { -3.0f, 0.0f, 3.0f }), { -1.0f, 0.0f, 1.0f });
}

TEST(SupportFaces, NegativeScaleOnEveryAxisKeepsOutwardFaces)
{
  const auto scene = makeScene();
  const glm::vec3 position{ 1.0f, 2.0f, 3.0f };
  const auto hull = hullOn(scene, makeUnitCubeHull(), position, { -1.0f, -2.0f, -0.5f });

  const auto faces = collisions::supportFacesOf(*hull);
  ASSERT_EQ(faces.size(), 6u);
  for (const auto& face : faces)
  {
    expectWoundOutward(face, position);
  }

  const auto* top = collisions::faceToward(faces, { 0.0f, 1.0f, 0.0f });
  ASSERT_NE(top, nullptr);
  expectNear("top center", top->center, { 1.0f, 4.0f, 3.0f });
}

TEST(SupportFaces, AnExactTieGoesToTheFirstFaceLikeNearestAxis)
{
  Rig rig;
  build(rig, 0);

  const auto faces = collisions::supportFacesOf(*rig.box);
  const auto* tied = collisions::faceToward(faces, { 1.0f, -1.0f, 0.0f });
  ASSERT_NE(tied, nullptr);
  expectNear("tie", tied->normal, { 1.0f, 0.0f, 0.0f });

  const auto reference = referenceOf(rig.box);
  expectNear("reference", faceNormalToward(reference.axes, { 1.0f, -1.0f, 0.0f }), tied->normal);
}
