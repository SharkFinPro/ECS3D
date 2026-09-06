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
  void expectNear(const glm::vec3& actual, const glm::vec3& expected)
  {
    constexpr float tolerance = 1e-4f;

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
  expectNear(hit.point, { 0, 0, 9 });

  // The normal points back out of the face the ray entered, not along the ray.
  expectNear(hit.normal, { 0, 0, -1 });
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

  // The bound is inclusive, so a face at exactly maxDistance is a hit rather than a coin toss on the
  // last bit of the comparison.
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
  expectNear(hit.point, { 0, 0, 0 });
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
  expectNear(hit.point, { 0, 0, 9 });
}

TEST(SceneQuery, RaycastRefusesADirectionOfNoLength)
{
  const auto scene = makeScene();
  addBox(scene, { 0, 0, 10 }, glm::vec3(1));

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

  expectNear(hit.point, { 1, 2, 3 });
  expectNear(hit.normal, { 4, 5, 6 });
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
}

TEST(SceneQuery, RaycastHitsASphereAtItsSurface)
{
  const auto scene = makeScene();
  const auto sphere = addSphere(scene, { 0, 0, 10 }, 2.0f);

  QueryHit hit;
  ASSERT_TRUE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));

  EXPECT_EQ(hit.object, sphere->getUUID());
  EXPECT_NEAR(hit.distance, 8.0f, 1e-4f);
  expectNear(hit.normal, { 0, 0, -1 });
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

  EXPECT_TRUE(sphereOverlaps(scene, { 0, 0, 0 }, 0.5f, ~(1u << 3), ignored->getUUID()).empty());

  const auto unmasked = sphereOverlaps(scene, { 0, 0, 0 }, 0.5f, allLayers, ignored->getUUID());
  ASSERT_EQ(unmasked.size(), 1u);
  EXPECT_EQ(unmasked.front(), masked->getUUID());
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

TEST(SceneQuery, AColliderWithoutATransformIsNotReachable)
{
  const auto scene = makeScene();
  const auto box = addBox(scene, { 0, 0, 10 }, glm::vec3(1));

  // Both queries resolve the box's world matrix through accessors that need the owning Transform, and
  // both guard on finding one first. Losing it must leave the object un-hittable rather than throwing
  // out of a script binding, where the exception would unwind through managed frames.
  box->removeComponent(box->getComponent<Transform>(ComponentType::transform));

  QueryHit hit;
  EXPECT_NO_THROW({
    EXPECT_FALSE(castRay(scene, { 0, 0, 0 }, { 0, 0, 1 }, 100.0f, hit));
    EXPECT_TRUE(sphereOverlaps(scene, { 0, 0, 10 }, 5.0f).empty());
  });
}
