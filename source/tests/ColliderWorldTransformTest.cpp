#include <gtest/gtest.h>

#include "TestPrinters.h"
#include "TestScene.h"
#include "WireTypes.h"
#include "objects/Object.h"
#include "objects/components/Transform.h"
#include "objects/components/collisions/BoxCollider.h"
#include "objects/components/collisions/SphereCollider.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <memory>
#include <utility>

namespace {
  // The support function puts every result through a matrix, so its coordinates are compared with a
  // tolerance. The accessors are plain arithmetic and are compared exactly.
  using fixtures::expectNear;

  // The Object is the collider's lifetime anchor: it holds the Transform the collider resolves through
  // and the raw owner pointer behind it.
  std::pair<std::shared_ptr<Object>, std::shared_ptr<BoxCollider>> makeBox(const glm::vec3& objectScale)
  {
    auto object = std::make_shared<Object>("Collider");
    object->getComponent<Transform>(ComponentType::transform)->setScale(objectScale);

    auto box = std::make_shared<BoxCollider>();
    object->addComponent(box);

    return { object, box };
  }

  std::pair<std::shared_ptr<Object>, std::shared_ptr<SphereCollider>> makeSphere(const glm::vec3& objectScale)
  {
    auto object = std::make_shared<Object>("Collider");
    object->getComponent<Transform>(ComponentType::transform)->setScale(objectScale);

    auto sphere = std::make_shared<SphereCollider>();
    object->addComponent(sphere);

    return { object, sphere };
  }
}

TEST(ColliderWorldTransform, AnUntouchedColliderIsTheSizeOfItsObject)
{
  const auto [object, box] = makeBox(glm::vec3(2, 3, 4));

  // The collider's own scale defaults to 1, which is neutral under the multiply the mesh does. Adding
  // instead would report every axis one larger than the box that actually collides.
  EXPECT_EQ(box->getScale(), glm::vec3(2, 3, 4));
}

TEST(ColliderWorldTransform, TheColliderScaleMultipliesTheObjectScale)
{
  const auto [object, box] = makeBox(glm::vec3(2));
  box->setScale(glm::vec3(3, 1, 0.5f));

  EXPECT_EQ(box->getScale(), glm::vec3(6, 2, 1));
}

TEST(ColliderWorldTransform, PositionAndRotationStillOffsetRatherThanMultiply)
{
  const auto [object, box] = makeBox(glm::vec3(2));
  const auto transform = object->getComponent<Transform>(ComponentType::transform);
  transform->setPosition(glm::vec3(10, 0, 0));
  transform->setRotation(glm::vec3(0, 90, 0));

  box->setPosition(glm::vec3(1, 2, 3));
  box->setRotation(glm::vec3(0, 45, 0));

  EXPECT_EQ(box->getPosition(), glm::vec3(11, 2, 3));
  EXPECT_EQ(box->getRotation(), glm::vec3(0, 135, 0));
}

TEST(ColliderWorldTransform, TheReportedScaleIsTheOneTheCollisionMeshUses)
{
  const auto [object, box] = makeBox(glm::vec3(2, 3, 4));
  box->setScale(glm::vec3(1.5f, 1, 1));

  // The mesh is the unit box [-1,1]^3 through the collider's world matrix, so a corner of it sits at the
  // world scale on every axis. This is the pairing the accessor got wrong: a consumer that trusted
  // getScale() sized the box differently from the geometry GJK actually walks.
  //
  // The directions are tilted off the axes on purpose. An axis-aligned direction leaves four corners
  // tied on the dot product, so the answer would pin the vertex iteration order rather than the math.
  const auto worldScale = box->getScale();

  expectNear(box->findFurthestPoint({ 1.0f, 0.1f, 0.1f }), worldScale);
  expectNear(box->findFurthestPoint({ -1.0f, 0.1f, 0.1f }),
             glm::vec3(-worldScale.x, worldScale.y, worldScale.z));
}

TEST(ColliderWorldTransform, StartingASceneClearsTheColliderMeshFromThePreviousRun)
{
  const auto [object, box] = makeBox(glm::vec3(1));
  const auto transform = object->getComponent<Transform>(ComponentType::transform);

  object->start();
  transform->setPosition(glm::vec3(5, 0, 0));

  // Populate the cache at the moved position, the way a tick mid-run would. Only the x component is
  // checked: +-1 on y/z all tie for furthest along +x, and which corner wins the tie is unspecified
  // (observed to differ by platform), but the x of any winning corner is unambiguous.
  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 6.0f, 1e-5f);

  object->stop();
  object->start();

  // Positive control: stop() really did revert the live position to the authored one.
  EXPECT_EQ(transform->getPosition(), glm::vec3(0, 0, 0));

  // The second run's first query must not reuse the mesh generated at the end of the first run - it has
  // to notice the transform's live value was reseeded even though nothing called a setter.
  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 1.0f, 1e-5f);
}

TEST(ColliderWorldTransform, UnpackingNewGeometryDirtiesAnAlreadyCachedMesh)
{
  const auto [object, box] = makeBox(glm::vec3(1));

  // Populate the cache at the collider's authored (offset-free) position. Only the x component is
  // checked: +-1 on y/z all tie for furthest along +x, and which corner wins is unspecified.
  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 1.0f, 1e-5f);

  auto moved = std::make_shared<BoxCollider>();
  moved->setPosition(glm::vec3(5, 0, 0));

  net::Message message(net::MessageType::undefined);
  moved->pack(message);

  net::MessageReader reader(message);
  // pack writes the type discriminator first; unpack expects the reader positioned after it.
  static_cast<void>(reader.read<ComponentType>());

  // Unpacking onto a collider whose mesh is already cached and clean - the transform's update id does
  // not change here, so only the dirty flag protects the cache.
  box->unpack(reader);

  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 6.0f, 1e-5f);
}

TEST(ColliderWorldTransform, UnpackingANewOffsetDirtiesAnAlreadyCachedBoundingBox)
{
  const auto [object, box] = makeBox(glm::vec3(1));

  // Populate the bounding box cache at the collider's authored (offset-free) position.
  const auto initialBox = box->getBoundingBox();
  EXPECT_NEAR(initialBox.maxX, 1.0f, 1e-5f);

  auto moved = std::make_shared<BoxCollider>();
  moved->setPosition(glm::vec3(5, 0, 0));

  net::Message message(net::MessageType::undefined);
  moved->pack(message);

  net::MessageReader reader(message);
  static_cast<void>(reader.read<ComponentType>());

  // The transform's update id does not change here - getBoundingBox has to notice the collider's own
  // geometry changed some other way, or it keeps serving the box computed before the unpack.
  box->unpack(reader);

  const auto movedBox = box->getBoundingBox();
  EXPECT_NEAR(movedBox.maxX, 6.0f, 1e-5f);
}

TEST(ColliderWorldTransform, TheMeshCacheSurvivesTwoHundredFiftySixTransformUpdates)
{
  const auto [object, box] = makeBox(glm::vec3(1));
  const auto transform = object->getComponent<Transform>(ComponentType::transform);

  // Populate the mesh cache at the collider's authored (offset-free) position. Only x is checked: +-1 on
  // y/z all tie for furthest along +x, and which corner wins the tie is unspecified.
  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 1.0f, 1e-5f);

  // 255 updates that leave the transform in an intermediate spot, then one more that lands somewhere
  // the cache has never seen. If the update id were narrow enough to wrap back to the value the cache
  // was stamped with after exactly 256 updates, this last move would be missed and the stale mesh from
  // the cache-populating query above would still be served.
  for (int i = 0; i < 255; ++i)
  {
    transform->setPosition(glm::vec3(50, 0, 0));
  }
  transform->setPosition(glm::vec3(100, 0, 0));

  EXPECT_NEAR(box->findFurthestPoint({ 1, 0, 0 }).x, 101.0f, 1e-5f);
}

TEST(ColliderWorldTransform, BoundingBoxRebuildsWhenASphereRadiusChangesWithTheTransformUntouched)
{
  const auto [object, sphere] = makeSphere(glm::vec3(1));
  sphere->setRadius(1.0f);

  const auto initialBox = sphere->getBoundingBox();
  EXPECT_NEAR(initialBox.maxX, 1.0f, 1e-5f);

  // Same object, same transform - only the sphere's own local radius changes.
  sphere->setRadius(3.0f);

  const auto grownBox = sphere->getBoundingBox();
  EXPECT_NEAR(grownBox.maxX, 3.0f, 1e-5f);
}

TEST(ColliderWorldTransform, AUniformlyScaledSphereRadiusIsScaleTimesLocalRadius)
{
  const auto [object, sphere] = makeSphere(glm::vec3(2));
  sphere->setRadius(1.5f);

  // Positive control: pins that getRadius() actually reflects the transform's scale rather than being
  // stuck at the local radius, before the non-uniform case below asks it to pick just one axis.
  EXPECT_FLOAT_EQ(sphere->getRadius(), 3.0f);
}

TEST(ColliderWorldTransform, ANonUniformlyScaledSphereRadiusUsesTheLargestAxis)
{
  const auto [object, sphere] = makeSphere(glm::vec3(2, 5, 3));
  sphere->setRadius(1.5f);

  // The sphere collides as a single radius, not an ellipsoid, so it has to pick one axis rather than
  // multiplying component-wise the way the box collider does. That axis is the largest one, matching the
  // GJK support function - a gizmo (or any other consumer) that scaled per axis here would draw an
  // ellipsoid that no longer matches what physics collides against.
  EXPECT_FLOAT_EQ(sphere->getRadius(), 7.5f);
}
