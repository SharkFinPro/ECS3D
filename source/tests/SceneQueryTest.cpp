#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"
#include "queries/SceneQueries.h"

#include <glm/vec3.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <uuid.h>

namespace {
  constexpr uint32_t allLayers = 0xFFFFFFFFu;

  // Everything a query writes, so a test can assert the outputs were left alone as readily as it
  // asserts what they became.
  struct QueryHit {
    uuids::uuid object;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };
    float distance = 0.0f;
  };

  struct Scene {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<ObjectManager> objectManager;
  };

  Scene makeScene()
  {
    Scene scene;
    registerDataComponents(*scene.componentRegistry);
    scene.objectManager = std::make_unique<ObjectManager>(scene.componentRegistry);

    return scene;
  }

  // A box collider is the unit box [-1,1]^3 through its world matrix, so at scale 1 an object at z = 10
  // spans z = 9 to 11 and a ray down +z meets its face at 9. Every distance below is built on that.
  std::shared_ptr<Object> addBox(const Scene& scene, const glm::vec3& position, const glm::vec3& scale)
  {
    auto object = std::make_shared<Object>("Box");
    scene.objectManager->addObject(object);

    const auto transform = object->getComponent<Transform>(ComponentType::transform);
    transform->setPosition(position);
    transform->setScale(scale);

    object->addComponent(std::make_shared<BoxCollider>());

    return object;
  }

  std::shared_ptr<Object> addSphere(const Scene& scene, const glm::vec3& position, const float radius)
  {
    auto object = std::make_shared<Object>("Sphere");
    scene.objectManager->addObject(object);

    object->getComponent<Transform>(ComponentType::transform)->setPosition(position);

    const auto sphere = std::make_shared<SphereCollider>();
    object->addComponent(sphere);
    sphere->setRadius(radius);

    return object;
  }

  void setColliderLayer(const std::shared_ptr<Object>& object, const uint32_t layer)
  {
    object->getComponent<Collider>(ComponentType::collider)->setLayer(layer);
  }

  // Named for what it wraps rather than "raycast": an unqualified helper sharing a name with the thing
  // under test invites the wrong overload, and glm sits in every argument's associated namespaces.
  bool castRay(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
               const float maxDistance, QueryHit& hit,
               const uint32_t layerMask = allLayers, const uuids::uuid& ignoreObject = {})
  {
    return SceneQueries::raycast(*scene.objectManager, origin, direction, maxDistance, layerMask,
                                 ignoreObject, hit.object, hit.point, hit.normal, hit.distance);
  }

  std::vector<uuids::uuid> sphereOverlaps(const Scene& scene, const glm::vec3& center, const float radius,
                                          const uint32_t layerMask = allLayers,
                                          const uuids::uuid& ignoreObject = {})
  {
    std::vector<uuids::uuid> results;
    SceneQueries::overlapSphere(*scene.objectManager, center, radius, layerMask, ignoreObject, results);

    return results;
  }

  // Every coordinate here comes out of a matrix inverse, so compare with a tolerance rather than exactly.
  // The trace names which call failed - a test asserting both a point and a normal would otherwise
  // report the same line inside this helper for either.
  void expectNear(const char* what, const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-4f;
    SCOPED_TRACE(what);

    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }
}

TEST(SceneQuery, RaycastFindsNothingInAnEmptyScene)
{
  const auto scene = makeScene();

  QueryHit hit;
  EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));
}

TEST(SceneQuery, RaycastReportsTheFaceItEntersThrough)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_EQ(hit.object, box->getUUID());
  EXPECT_NEAR(hit.distance, 9.0f, 1e-4f);
  expectNear("point", hit.point, { 0, 0, 9 });

  // The normal points back out of the face the ray entered, not along the ray.
  expectNear("normal", hit.normal, { 0, 0, -1 });
}

TEST(SceneQuery, RaycastReportsTheNearestOfSeveralHits)
{
  const auto scene = makeScene();
  const auto behind = addBox(scene, { 0, 0, 20 }, glm::vec3(1));
  const auto infront = addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  // The far one is registered first on purpose: the nearest has to win on distance, not on the order
  // getAllObjects happens to walk.
  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_EQ(hit.object, infront->getUUID());
  EXPECT_NE(hit.object, behind->getUUID());
  EXPECT_NEAR(hit.distance, 9.0f, 1e-4f);
}

TEST(SceneQuery, RaycastStopsAtMaxDistanceButReachesItExactly)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  QueryHit hit;
  EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 8.5f, hit));

  // The bound is inclusive, so a face at exactly maxDistance is a hit. That the comparison lands on the
  // bit rather than near it is not luck: every coordinate in these fixtures is a small integer or a
  // power of two, so the matrix inverse and the reciprocal are exact. Moving a box somewhere that is
  // not representable would turn this into a coin toss.
  EXPECT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 9.0f, hit));
}

TEST(SceneQuery, RaycastFromInsideABoxReportsContactAtTheOrigin)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 0 }, glm::vec3(1));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  // Documented behavior, and the one gameplay leans on: a body already penetrating a collider still
  // detects it rather than casting out through the far side.
  EXPECT_EQ(hit.object, box->getUUID());
  EXPECT_NEAR(hit.distance, 0.0f, 1e-4f);
  expectNear("point", hit.point, { 0, 0, 0 });
}

TEST(SceneQuery, RaycastFromInsideASphereReportsContactAtTheOrigin)
{
  const auto scene = makeScene();
  addSphere(scene, { 0, 0, 0 }, 2.0f);

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_NEAR(hit.distance, 0.0f, 1e-4f);
}

TEST(SceneQuery, RaycastIgnoresTheObjectItIsToldTo)
{
  const auto scene = makeScene();
  const auto infront = addBox(scene, { 0, 0, 10 }, glm::vec3(1));
  const auto behind = addBox(scene, { 0, 0, 20 }, glm::vec3(1));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit, allLayers, infront->getUUID()));

  // The caster excluding its own collider, without which every ground check hits the caster's own feet.
  EXPECT_EQ(hit.object, behind->getUUID());
  EXPECT_NEAR(hit.distance, 19.0f, 1e-4f);
}

TEST(SceneQuery, RaycastSkipsAColliderWhoseLayerIsNotInTheMask)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 10 }, glm::vec3(1));
  setColliderLayer(box, 3);

  QueryHit hit;
  EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit, ~(1u << 3)));
  EXPECT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit, 1u << 3));
}

TEST(SceneQuery, RaycastNormalizesTheDirectionItIsGiven)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 5 }, 100.0f, hit));

  // The distance is in world units either way, so maxDistance keeps meaning the same thing whether or
  // not the caller normalized first.
  EXPECT_NEAR(hit.distance, 9.0f, 1e-4f);
  expectNear("point", hit.point, { 0, 0, 9 });
}

TEST(SceneQuery, RaycastRefusesADirectionOfNoLength)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  // A sphere as well as a box, because only the sphere can tell whether the guard is doing the work.
  // Without it the direction normalizes to NaN, and every comparison in the box slab test is false, so
  // a box misses by accident - while raySphere falls through its own checks and reports a NaN hit.
  addSphere(scene, { 0, 0, 10 }, 2.0f);

  QueryHit hit;
  EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 0 }, 100.0f, hit));
}

TEST(SceneQuery, RaycastLeavesItsOutputsAloneWhenItMisses)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  QueryHit hit{ {}, { 1, 2, 3 }, { 4, 5, 6 }, 7.0f };

  // Fired away from the box. The header promises the outputs are written only on a hit, and a caller
  // that keeps its previous result across a miss depends on that.
  ASSERT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, -1 }, 100.0f, hit));

  expectNear("point", hit.point, { 1, 2, 3 });
  expectNear("normal", hit.normal, { 4, 5, 6 });
  EXPECT_FLOAT_EQ(hit.distance, 7.0f);
}

TEST(SceneQuery, RaycastSeesAScaledBoxAtItsScaledSize)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, { 1, 1, 4 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  // Four times deeper along z, so the near face moves from 9 to 6 - the query has to see the box the
  // collision mesh is actually built from.
  EXPECT_NEAR(hit.distance, 6.0f, 1e-4f);

  // And the normal has to survive the non-uniform scale. It goes through the inverse-transpose of the
  // linear part precisely so a squashed box does not report a skewed face; at an identity scale every
  // wrong formula gives the same answer, so this is the case that separates them.
  expectNear("normal", hit.normal, { 0, 0, -1 });
}

TEST(SceneQuery, RaycastReportsTheFaceNormalOfARotatedBox)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 10 }, glm::vec3(1));
  box->getComponent<Transform>(ComponentType::transform)->setRotation({ 0, 45, 0 });

  // Offset off the axis on purpose. Fired down the centre line the ray would meet the rotated box
  // exactly on a corner, where two slabs tie and the answer pins which axis the loop looked at first
  // rather than the geometry.
  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0.5f, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  // Turned 45 degrees about y, the ray enters through the +x face of the box's own frame. In that
  // frame the entry is at (10.5 - sqrt(2)) along the ray, and the face normal comes back rotated with
  // it. Nothing else here rotates a box, so this is the only test that exercises the rotation order
  // boxWorldMatrix has to share with the collision mesh - or the inverse-transpose on the normal.
  EXPECT_NEAR(hit.distance, 10.5f - std::sqrt(2.0f), 1e-4f);

  const float halfRootTwo = std::sqrt(2.0f) / 2.0f;
  expectNear("normal", hit.normal, { halfRootTwo, 0, -halfRootTwo });
}

TEST(SceneQuery, RaycastSeesASphereAtItsScaledRadius)
{
  const auto scene = makeScene();
  const auto sphere = addSphere(scene, { 0, 0, 10 }, 2.0f);
  sphere->getComponent<Transform>(ComponentType::transform)->setScale({ 1, 1, 3 });

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  // A sphere stays a sphere under a non-uniform scale: the radius takes the largest axis, so 2 becomes
  // 6 and the near surface moves from 8 to 4. Every other sphere here sits at a scale of 1, where a
  // wrong rule is invisible.
  EXPECT_NEAR(hit.distance, 4.0f, 1e-4f);
}

TEST(SceneQuery, RaycastHitsASphereAtItsSurface)
{
  const auto scene = makeScene();
  const auto sphere = addSphere(scene, { 0, 0, 10 }, 2.0f);

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_EQ(hit.object, sphere->getUUID());
  EXPECT_NEAR(hit.distance, 8.0f, 1e-4f);
  expectNear("normal", hit.normal, { 0, 0, -1 });
}

TEST(SceneQuery, OverlapSphereCollectsWhatItTouchesAndNothingElse)
{
  const auto scene = makeScene();
  const auto touching = addBox(scene, { 0, 0, 0 }, glm::vec3(1));
  addBox(scene, { 0, 0, 50 }, glm::vec3(1));

  const auto results = sphereOverlaps(scene, { 0, 0, 0 }, 0.5f);

  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results.front(), touching->getUUID());
}

TEST(SceneQuery, OverlapSphereCountsAContactThatOnlyGrazes)
{
  const auto scene = makeScene();
  addSphere(scene, { 0, 0, 0 }, 1.0f);

  // Touching exactly at the surface: the comparison is inclusive, so a resting contact registers rather
  // than flickering on the last bit of the radius.
  EXPECT_EQ(sphereOverlaps(scene, { 3, 0, 0 }, 2.0f).size(), 1u);
  EXPECT_TRUE(sphereOverlaps(scene, { 3.01f, 0, 0 }, 2.0f).empty());
}

TEST(SceneQuery, OverlapSphereRespectsTheLayerMaskAndTheIgnoredObject)
{
  const auto scene = makeScene();
  const auto masked = addBox(scene, { 0, 0, 0 }, glm::vec3(1));
  const auto ignored = addBox(scene, { 1, 0, 0 }, glm::vec3(1));
  setColliderLayer(masked, 3);

  // Asserted apart, so a failure says which of the two rules stopped working rather than that one did.
  const auto byMask = sphereOverlaps(scene, { 0, 0, 0 }, 0.5f, ~(1u << 3));
  ASSERT_EQ(byMask.size(), 1u);
  EXPECT_EQ(byMask.front(), ignored->getUUID());

  const auto byIgnore = sphereOverlaps(scene, { 0, 0, 0 }, 0.5f, allLayers, ignored->getUUID());
  ASSERT_EQ(byIgnore.size(), 1u);
  EXPECT_EQ(byIgnore.front(), masked->getUUID());

  EXPECT_TRUE(sphereOverlaps(scene, { 0, 0, 0 }, 0.5f, ~(1u << 3), ignored->getUUID()).empty());
}

TEST(SceneQuery, OverlapSphereAppendsToTheResultsItIsGiven)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 0 }, glm::vec3(1));

  std::vector<uuids::uuid> results{ box->getUUID() };
  SceneQueries::overlapSphere(*scene.objectManager, { 0, 0, 0 }, 0.5f, allLayers, {}, results);

  // It appends rather than replacing. The only caller in the tree hands it a fresh vector, so this pins
  // the contract for the next one rather than describing something anything relies on.
  EXPECT_EQ(results.size(), 2u);
}

TEST(SceneQuery, ABoxColliderWithoutATransformIsNotReachable)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  // Both queries resolve a collider's world placement through accessors that throw without an owning
  // Transform. Losing it must leave the object un-hittable rather than throwing out of a script
  // binding, where the exception would unwind through managed frames.
  box->removeComponent(box->getComponent<Transform>(ComponentType::transform));

  QueryHit hit;
  EXPECT_NO_THROW({
    EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));
    EXPECT_TRUE(sphereOverlaps(scene, { 0, 0, 10 }, 5.0f).empty());
  });
}

TEST(SceneQuery, ASphereColliderWithoutATransformIsNotReachable)
{
  const auto scene = makeScene();
  const auto sphere = addSphere(scene, { 0, 0, 10 }, 2.0f);

  // The same invariant for the other shape, which is where it did not hold: the sphere branch called
  // getPosition and getRadius with no guard at all, so a collider whose object had lost its Transform
  // threw straight out of the query instead of being skipped.
  sphere->removeComponent(sphere->getComponent<Transform>(ComponentType::transform));

  QueryHit hit;
  EXPECT_NO_THROW({
    EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));
    EXPECT_TRUE(sphereOverlaps(scene, { 0, 0, 10 }, 5.0f).empty());
  });
}
