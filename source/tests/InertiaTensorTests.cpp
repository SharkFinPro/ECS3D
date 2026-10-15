#include <gtest/gtest.h>

#include "InertiaTensor.h"
#include "CapsuleMoments.h"
#include "TestScene.h"
#include "objects/Object.h"
#include "objects/components/RigidBody.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/CapsuleCollider.h"
#include "objects/components/collisions/ConvexMeshCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace {
  using fixtures::addChildObject;
  using fixtures::addObject;
  using fixtures::makeScene;
  using fixtures::transformOf;

  constexpr float tolerance = 1e-4f;

  void expectMatrix(const glm::mat3& actual, const glm::mat3& expected, const float allowed = tolerance)
  {
    for (int column = 0; column < 3; ++column)
    {
      for (int row = 0; row < 3; ++row)
      {
        EXPECT_NEAR(actual[column][row], expected[column][row], allowed) << "entry " << column << "," << row;
      }
    }
  }

  glm::mat3 diagonal(const float x, const float y, const float z)
  {
    return glm::mat3(x, 0, 0, 0, y, 0, 0, 0, z);
  }

  glm::mat3 tensorOf(const inertia::Distribution& shape, const glm::vec3& point = glm::vec3(0.0f))
  {
    const std::array shapes{ shape };

    return inertia::tensorAbout(shapes, point);
  }

  // Counter-clockwise from outside.
  const std::vector<glm::vec3> corner{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  const std::vector<uint32_t> cornerFaces{ 0, 2, 1, 0, 3, 2, 0, 1, 3, 1, 2, 3 };

  std::shared_ptr<ConvexMeshCollider> addHull(const std::shared_ptr<Object>& object)
  {
    auto collider = std::make_shared<ConvexMeshCollider>();
    object->addComponent(collider);

    return collider;
  }
}

TEST(InertiaTensor, ASolidSphereIsTwoFifthsOfItsRadiusSquaredAboutEveryAxis)
{
  const auto sphere = inertia::sphere({ 0, 0, 0 }, 1.5f);

  EXPECT_NEAR(sphere.volume, 4.0f / 3.0f * glm::pi<float>() * 3.375f, tolerance);
  expectMatrix(tensorOf(sphere), glm::mat3(0.4f * 2.25f));
}

TEST(InertiaTensor, ABoxIsItsHalfExtentsSquaredOverThreeAndFollowsItsAxes)
{
  const auto flat = inertia::box({ 0, 0, 0 }, glm::mat3(1.0f), { 1, 2, 3 });
  expectMatrix(tensorOf(flat), diagonal((4 + 9) / 3.0f, (1 + 9) / 3.0f, (1 + 4) / 3.0f));

  // Turned a quarter about z, its x extent lies along world y and its y extent along world -x.
  const auto turned = inertia::box({ 0, 0, 0 }, glm::mat3_cast(glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1))),
                                   { 1, 2, 3 });
  expectMatrix(tensorOf(turned), diagonal((1 + 9) / 3.0f, (4 + 9) / 3.0f, (1 + 4) / 3.0f));
}

TEST(InertiaTensor, ACapsuleHasTheTextbookMomentsAboutAndAcrossItsAxis)
{
  constexpr float radius = 0.5f;
  constexpr float halfLength = 0.75f;
  const auto moments = capsuleMoments::textbook(radius, 2.0f * halfLength);

  const auto upright = inertia::capsule({ 0, -halfLength, 0 }, { 0, halfLength, 0 }, radius);
  expectMatrix(tensorOf(upright), diagonal(moments.acrossAxis, moments.aboutAxis, moments.acrossAxis));

  const auto expectedVolume = glm::pi<float>() * radius * radius * 2.0f * halfLength +
                              4.0f / 3.0f * glm::pi<float>() * radius * radius * radius;
  EXPECT_NEAR(upright.volume, expectedVolume, tolerance);
}

TEST(InertiaTensor, ACapsuleOnADiagonalSpinsAboutItsAxisAtTheAxialMoment)
{
  const auto axis = glm::normalize(glm::vec3(1, 2, -2));
  const auto moments = capsuleMoments::textbook(0.4f, 1.2f);
  const auto tensor = tensorOf(inertia::capsule(axis * -0.6f, axis * 0.6f, 0.4f));

  EXPECT_NEAR(glm::dot(axis, tensor * axis), moments.aboutAxis, tolerance);

  const auto across = glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0)));
  EXPECT_NEAR(glm::dot(across, tensor * across), moments.acrossAxis, tolerance);
}

TEST(InertiaTensor, ACapsuleWithNoCoreIsASphere)
{
  const auto capsule = inertia::capsule({ 1, 2, 3 }, { 1, 2, 3 }, 0.7f);

  EXPECT_NEAR(capsule.volume, inertia::sphere({ 1, 2, 3 }, 0.7f).volume, tolerance);
  expectMatrix(tensorOf(capsule, { 1, 2, 3 }), glm::mat3(0.4f * 0.49f));
}

TEST(InertiaTensor, AUnitCubeHullIsTheUnitBoxWhicheverWayItsTrianglesWind)
{
  const auto colliderHull = ConvexMeshCollider().getHull();
  const auto box = inertia::box({ 0, 0, 0 }, glm::mat3(1.0f), { 1, 1, 1 });

  const auto hull = inertia::hull(colliderHull.vertices, colliderHull.indices);
  EXPECT_NEAR(hull.volume, 8.0f, tolerance);
  expectMatrix(tensorOf(hull), tensorOf(box));
  expectMatrix(tensorOf(hull), diagonal(2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f));

  auto reversed = colliderHull.indices;
  for (size_t i = 0; i + 2 < reversed.size(); i += 3)
  {
    std::swap(reversed[i + 1], reversed[i + 2]);
  }
  const auto flipped = inertia::hull(colliderHull.vertices, reversed);
  EXPECT_NEAR(flipped.volume, 8.0f, tolerance);
  expectMatrix(tensorOf(flipped), tensorOf(box));
}

TEST(InertiaTensor, ATetrahedronHullHasItsClosedFormVolumeCentroidAndSecondMoment)
{
  const auto hull = inertia::hull(corner, cornerFaces);

  EXPECT_NEAR(hull.volume, 1.0f / 6.0f, tolerance);
  EXPECT_NEAR(glm::distance(hull.centroid, glm::vec3(0.25f)), 0.0f, tolerance);

  // Per unit volume the corner tetrahedron has E[x^2] = 1/10 and E[xy] = 1/20; about its centroid at 1/4 that
  // leaves 3/80 on the diagonal and -1/80 off it.
  const glm::mat3 covariance(3.0f / 80.0f, -1.0f / 80.0f, -1.0f / 80.0f,
                             -1.0f / 80.0f, 3.0f / 80.0f, -1.0f / 80.0f,
                             -1.0f / 80.0f, -1.0f / 80.0f, 3.0f / 80.0f);
  expectMatrix(hull.covariance, covariance);
}

TEST(InertiaTensor, AHullOfABoxsCornersMatchesTheBoxOffTheOrigin)
{
  const glm::vec3 half(2.0f, 1.0f, 0.5f);
  const glm::vec3 center(3.0f, -1.0f, 2.0f);
  std::vector<glm::vec3> corners;
  for (const auto& vertex : boxVertices)
  {
    corners.push_back(center + vertex * half);
  }

  const auto built = buildConvexHull(corners);
  ASSERT_TRUE(built.has_value());

  const auto hull = inertia::hull(built->vertices, built->indices);
  const auto box = inertia::box(center, glm::mat3(1.0f), half);

  EXPECT_NEAR(hull.volume, box.volume, tolerance);
  expectMatrix(tensorOf(hull, { 1, 1, 1 }), tensorOf(box, { 1, 1, 1 }), 1e-3f);
}

TEST(InertiaTensor, AShapeAwayFromThePointAddsTheParallelAxisTerm)
{
  const auto sphere = inertia::sphere({ 3, 0, 0 }, 1.0f);

  // Nothing is added about its own axis through the point, nine about the two across the offset.
  expectMatrix(tensorOf(sphere, { 0, 0, 0 }), diagonal(0.4f, 9.4f, 9.4f));
  expectMatrix(tensorOf(sphere, { 3, 0, 0 }), glm::mat3(0.4f));
}

TEST(InertiaTensor, TwoShapesAreWeightedByTheirShareOfTheVolume)
{
  const std::array shapes{ inertia::sphere({ 2, 0, 0 }, 1.0f), inertia::sphere({ -1, 0, 0 }, 2.0f) };

  // Volumes are 1 : 8. Each sphere contributes its share of r^2 / 5 on every axis and of the squared offset
  // along x, so the total covariance is 11/15 on every axis and 4/3 more along x.
  expectMatrix(inertia::tensorAbout(shapes, { 0, 0, 0 }), diagonal(22.0f / 15.0f, 14.0f / 5.0f, 14.0f / 5.0f));
}

TEST(InertiaTensor, ABodyWithNoColliderIsABoxOfItsScale)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Plain", { 0, 0, 0 }, { 3, 1, 2 });
  transformOf(object)->setRotation({ 0, 90, 0 });

  // A quarter turn about y swaps the x and z extents.
  expectMatrix(inertia::worldTensor(object.get(), *transformOf(object)),
               diagonal((1 + 9) / 3.0f, (4 + 9) / 3.0f, (4 + 1) / 3.0f));
}

TEST(InertiaTensor, ABodysSphereColliderGivesItASphereTensorFromItsWorldRadius)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Ball", { 0, 0, 0 }, { 2, 2, 2 });
  fixtures::addSphereCollider(object, 0.5f);

  // The world radius is 1, where the box of its scale would give 8 / 3.
  expectMatrix(inertia::worldTensor(object.get(), *transformOf(object)), glm::mat3(0.4f));
}

TEST(InertiaTensor, AHullColliderMatchesABoxColliderPlacedTheSameWay)
{
  struct Pose
  {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
  };

  const std::array poses{ Pose{ { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 } },
                          Pose{ { 0.5f, -1, 2 }, { 20, 35, 50 }, { 1, 2, 0.5f } } };
  for (const auto& pose : poses)
  {
    const auto scene = makeScene();
    const auto boxObject = addObject(scene, "Box", { 0, 0, 0 }, { 1.5f, 1, 2 });
    transformOf(boxObject)->setRotation({ 10, 20, 30 });
    const auto box = fixtures::addBoxCollider(boxObject);
    box->setPosition(pose.position);
    box->setRotation(pose.rotation);
    box->setScale(pose.scale);

    const auto hullObject = addObject(scene, "Hull", { 0, 0, 0 }, { 1.5f, 1, 2 });
    transformOf(hullObject)->setRotation({ 10, 20, 30 });
    const auto hull = addHull(hullObject);
    hull->setPosition(pose.position);
    hull->setRotation(pose.rotation);
    hull->setScale(pose.scale);

    expectMatrix(inertia::worldTensor(boxObject.get(), *transformOf(boxObject)),
                 inertia::worldTensor(hullObject.get(), *transformOf(hullObject)), 1e-3f);
  }
}

TEST(InertiaTensor, ACompoundBodyCombinesItsChildrenAboutTheOwnersPosition)
{
  const auto scene = makeScene();
  const auto owner = addObject(scene, "Owner", { 0, 0, 0 });
  fixtures::addRigidBody(owner);

  const auto small = addChildObject(scene, "Small", owner);
  transformOf(small)->setPosition({ 2, 0, 0 });
  fixtures::addSphereCollider(small, 1.0f);

  const auto large = addChildObject(scene, "Large", owner);
  transformOf(large)->setPosition({ -1, 0, 0 });
  fixtures::addSphereCollider(large, 2.0f);

  const auto expected = diagonal(22.0f / 15.0f, 14.0f / 5.0f, 14.0f / 5.0f);
  expectMatrix(inertia::worldTensor(owner.get(), *transformOf(owner)), expected);

  // Another body's collider and a trigger do not move with this body, so they add nothing.
  const auto separate = addChildObject(scene, "Separate", owner);
  transformOf(separate)->setPosition({ 0, 5, 0 });
  fixtures::addSphereCollider(separate, 3.0f);
  fixtures::addRigidBody(separate);

  const auto trigger = addChildObject(scene, "Trigger", owner);
  transformOf(trigger)->setPosition({ 0, 0, 5 });
  fixtures::addSphereCollider(trigger, 3.0f)->setIsTrigger(true);

  expectMatrix(inertia::worldTensor(owner.get(), *transformOf(owner)), expected);

  // Positive control: the child body's own tensor is its sphere's.
  expectMatrix(inertia::worldTensor(separate.get(), *transformOf(separate)), glm::mat3(0.4f * 9.0f));
}

TEST(InertiaTensor, AnOffsetColliderMovesTheBodysTensorAwayFromItsOwnersPosition)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Offset", { 0, 0, 0 });
  fixtures::addSphereCollider(object, 1.0f)->setPosition({ 0, 3, 0 });

  expectMatrix(inertia::worldTensor(object.get(), *transformOf(object)), diagonal(9.4f, 0.4f, 9.4f));
}

TEST(InertiaTensor, AScaledTurnedAndOffsetCapsuleColliderUsesItsWorldSegmentAndRadius)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Capsule", { 0, 0, 0 }, { 2, 3, 2 });
  transformOf(object)->setRotation({ 0, 0, 90 });
  const auto collider = fixtures::addCapsuleCollider(object, 0.5f, 4.0f);
  collider->setPosition({ 1, 2, 3 });

  // The radius is 0.5 * 2 = 1 and the half height 4 / 2 * 3 = 6, so the core is 10 long and, turned a quarter
  // about z, lies along x. About its own center that is the textbook capsule; the offset adds 14 - d d^T.
  const auto moments = capsuleMoments::textbook(1.0f, 10.0f);
  const glm::vec3 offset(1, 2, 3);
  const auto expected = diagonal(moments.aboutAxis, moments.acrossAxis, moments.acrossAxis) +
                        glm::mat3(14.0f) - glm::outerProduct(offset, offset);

  expectMatrix(inertia::worldTensor(object.get(), *transformOf(object)), expected, 1e-3f);
}

TEST(InertiaTensor, ACapsuleShorterThanItsDiameterIsASphere)
{
  const auto scene = makeScene();
  const auto object = addObject(scene, "Stubby", { 0, 0, 0 });
  fixtures::addCapsuleCollider(object, 1.0f, 1.0f);

  expectMatrix(inertia::worldTensor(object.get(), *transformOf(object)), glm::mat3(0.4f));
}

TEST(InertiaTensor, AChildColliderUnderATurnedAndScaledParentUsesItsWorldPlacement)
{
  const auto scene = makeScene();
  const auto parent = addObject(scene, "Parent", { 0, 0, 0 }, { 2, 2, 2 });
  transformOf(parent)->setRotation({ 0, 0, 90 });
  fixtures::addRigidBody(parent);

  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition({ 1, 0, 0 });
  const auto box = fixtures::addBoxCollider(child);
  box->setScale({ 1, 2, 3 });

  // The child sits at (0, 2, 0) with half extents (2, 4, 6) along its own axes, which the parent's quarter turn
  // about z maps to (4, 2, 6) in the world: covariance diag(16/3, 4/3, 12) about its center, plus 4 along y.
  expectMatrix(inertia::worldTensor(parent.get(), *transformOf(parent)), diagonal(52.0f / 3.0f, 52.0f / 3.0f, 32.0f / 3.0f),
               1e-3f);
}

TEST(InertiaTensor, ABodyGathersGrandchildrenButNotAnotherBodysColliders)
{
  const auto scene = makeScene();
  const auto owner = addObject(scene, "Owner", { 0, 0, 0 });
  fixtures::addRigidBody(owner);

  const auto bare = addChildObject(scene, "Bare", owner);
  transformOf(bare)->setPosition({ 1, 0, 0 });
  const auto grandchild = addChildObject(scene, "Grandchild", bare);
  transformOf(grandchild)->setPosition({ 1, 0, 0 });
  fixtures::addSphereCollider(grandchild, 1.0f);

  const auto other = addChildObject(scene, "Other", owner);
  transformOf(other)->setPosition({ 0, 5, 0 });
  fixtures::addRigidBody(other);
  fixtures::addSphereCollider(other, 3.0f);
  const auto otherPart = addChildObject(scene, "OtherPart", other);
  transformOf(otherPart)->setPosition({ 0, 0, 5 });
  fixtures::addSphereCollider(otherPart, 3.0f);

  // Only the grandchild's sphere, at x = 2, belongs to the owner.
  expectMatrix(inertia::worldTensor(owner.get(), *transformOf(owner)), diagonal(0.4f, 4.4f, 4.4f));

  // Positive control: the other body does take its own part, two equal spheres five apart.
  expectMatrix(inertia::worldTensor(other.get(), *transformOf(other)), diagonal(16.1f, 16.1f, 3.6f), 1e-3f);
}

TEST(InertiaTensor, AMirroredBoxOrHullHasTheTensorOfItsAbsoluteSize)
{
  const auto scene = makeScene();
  const auto expected = diagonal((4 + 1) / 3.0f, (1 + 1) / 3.0f, (1 + 4) / 3.0f);

  const auto box = addObject(scene, "Box", { 0, 0, 0 }, { -1, 2, 1 });
  fixtures::addBoxCollider(box);
  expectMatrix(inertia::worldTensor(box.get(), *transformOf(box)), expected);

  const auto hull = addObject(scene, "Hull", { 0, 0, 0 }, { -1, 2, 1 });
  addHull(hull);
  expectMatrix(inertia::worldTensor(hull.get(), *transformOf(hull)), expected);
}

TEST(InertiaTensor, DegenerateInputsGiveAZeroTensorRatherThanNaN)
{
  const auto scene = makeScene();
  const glm::mat3 zero(0.0f);

  const auto flat = addObject(scene, "Flat", { 0, 0, 0 }, { 0, 0, 0 });
  fixtures::addBoxCollider(flat);
  expectMatrix(inertia::worldTensor(flat.get(), *transformOf(flat)), zero);

  const std::vector<glm::vec3> square{ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
  const std::vector<uint32_t> faces{ 0, 1, 2, 0, 2, 3 };
  EXPECT_EQ(inertia::hull(square, faces).volume, 0.0f);

  const std::array empty{ inertia::hull(square, faces), inertia::sphere({ 0, 0, 0 }, 0.0f) };
  expectMatrix(inertia::tensorAbout(empty, { 0, 0, 0 }), zero);
  expectMatrix(inertia::tensorAbout(std::span<const inertia::Distribution>(), { 0, 0, 0 }), zero);

  // A volume that overflows a float is no shape: the body keeps the box of its scale.
  const auto huge = addObject(scene, "Huge", { 0, 0, 0 });
  fixtures::addSphereCollider(huge, 1e20f);
  expectMatrix(inertia::worldTensor(huge.get(), *transformOf(huge)), glm::mat3(2.0f / 3.0f));
}
