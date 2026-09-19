#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Transform.h"
#include "SceneEditFixtures.h"

#include <cmath>
#include <glm/vec3.hpp>
#include <memory>
#include <vector>

namespace {
  using namespace sceneEditFixtures;
}

TEST(SceneEdit, ReparentingOntoANonIdentityParentPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 90.0f, 0.0f));

  // scene.object starts at the scene root with an identity transform (see makeScene), so its world
  // values equal its local ones before this reparent.
  transformOf(scene.object)->setPosition(glm::vec3(10.0f, 5.0f, -3.0f));
  transformOf(scene.object)->setRotation(glm::vec3(0.0f, 30.0f, 0.0f));
  transformOf(scene.object)->setScale(glm::vec3(1.5f, 1.5f, 1.5f));

  const auto worldPositionBefore = transformOf(scene.object)->getPosition();
  const auto worldRotationBefore = transformOf(scene.object)->getRotation();
  const auto worldScaleBefore = transformOf(scene.object)->getScale();

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.object->getParent(), parent);

  // World placement is unchanged by the reparent. Before the fix, this came out as the parent's own
  // position/scale/rotation composed on top of the object's now-stale local values instead.
  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(scene.object)->getScale(), worldScaleBefore);

  // The local values were rewritten to compensate for the new parent's own world transform.
  expectNear("local position", transformOf(scene.object)->getLocalPosition(),
             worldPositionBefore - transformOf(parent)->getPosition());
  expectNear("local rotation", transformOf(scene.object)->getLocalRotation(),
             worldRotationBefore - transformOf(parent)->getRotation());
  expectNear("local scale", transformOf(scene.object)->getLocalScale(),
             worldScaleBefore / transformOf(parent)->getScale());
}

TEST(SceneEdit, ReparentingBackToSceneRootPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 90.0f, 0.0f));

  const auto child = addChildObject(scene, "Child", parent);
  transformOf(child)->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
  transformOf(child)->setRotation(glm::vec3(0.0f, 10.0f, 0.0f));
  transformOf(child)->setScale(glm::vec3(0.5f, 0.5f, 0.5f));

  const auto worldPositionBefore = transformOf(child)->getPosition();
  const auto worldRotationBefore = transformOf(child)->getRotation();
  const auto worldScaleBefore = transformOf(child)->getScale();

  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID())), SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), nullptr);

  expectNear("world position", transformOf(child)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(child)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(child)->getScale(), worldScaleBefore);

  // No parent transform to compose with at the scene root, so local now equals world.
  expectNear("local position", transformOf(child)->getLocalPosition(), worldPositionBefore);
  expectNear("local rotation", transformOf(child)->getLocalRotation(), worldRotationBefore);
  expectNear("local scale", transformOf(child)->getLocalScale(), worldScaleBefore);
}

TEST(SceneEdit, ReparentingBetweenTwoNonIdentityParentsPreservesWorldPlacement)
{
  const auto scene = makeScene();

  const auto parentA = addObject(scene, "ParentA", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));
  const auto parentB = addObject(scene, "ParentB", glm::vec3(-5.0f, 20.0f, 3.0f), glm::vec3(3.0f, 1.0f, 1.0f));
  transformOf(parentB)->setRotation(glm::vec3(0.0f, 0.0f, 45.0f));

  const auto child = addChildObject(scene, "Child", parentA);
  transformOf(child)->setPosition(glm::vec3(1.0f, 1.0f, 1.0f));

  const auto worldPositionBefore = transformOf(child)->getPosition();
  const auto worldRotationBefore = transformOf(child)->getRotation();
  const auto worldScaleBefore = transformOf(child)->getScale();

  const auto parentBUUID = parentB->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(child->getUUID(), &parentBUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(child->getParent(), parentB);

  expectNear("world position", transformOf(child)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(child)->getRotation(), worldRotationBefore);
  expectNear("world scale", transformOf(child)->getScale(), worldScaleBefore);

  expectNear("local position", transformOf(child)->getLocalPosition(),
             worldPositionBefore - transformOf(parentB)->getPosition());
  expectNear("local rotation", transformOf(child)->getLocalRotation(),
             worldRotationBefore - transformOf(parentB)->getRotation());
  expectNear("local scale", transformOf(child)->getLocalScale(),
             worldScaleBefore / transformOf(parentB)->getScale());
}

TEST(SceneEdit, ReparentingOntoAParentWithAZeroWorldScaleAxisCompensatesTheOtherAxes)
{
  const auto scene = makeScene();

  // The y axis cannot be divided out below - it is left at the object's own pre-reparent local value.
  // A non-zero parent position/rotation is used too, so the position/rotation assertions below also
  // exercise the fix rather than passing by coincidence.
  const auto parent = addObject(scene, "Parent", glm::vec3(10.0f, -4.0f, 7.0f), glm::vec3(2.0f, 0.0f, 3.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 45.0f, 0.0f));

  transformOf(scene.object)->setPosition(glm::vec3(5.0f, 2.0f, -1.0f));
  transformOf(scene.object)->setRotation(glm::vec3(10.0f, 20.0f, 30.0f));
  transformOf(scene.object)->setScale(glm::vec3(4.0f, 6.0f, 8.0f));

  const auto worldPositionBefore = transformOf(scene.object)->getPosition();
  const auto worldRotationBefore = transformOf(scene.object)->getRotation();
  const auto worldScaleBefore = transformOf(scene.object)->getScale();
  const auto localScaleBefore = transformOf(scene.object)->getLocalScale();

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.object->getParent(), parent);

  // Position and rotation do not involve a division, so both stay fully preserved.
  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);

  // x and z divide cleanly against the parent's non-zero scale on those axes, so world scale is
  // preserved there too.
  EXPECT_NEAR(transformOf(scene.object)->getScale().x, worldScaleBefore.x, 1e-4f);
  EXPECT_NEAR(transformOf(scene.object)->getScale().z, worldScaleBefore.z, 1e-4f);

  const auto localScaleAfter = transformOf(scene.object)->getLocalScale();
  EXPECT_NEAR(localScaleAfter.x, worldScaleBefore.x / 2.0f, 1e-4f);
  EXPECT_NEAR(localScaleAfter.z, worldScaleBefore.z / 3.0f, 1e-4f);
  // y is not representable (dividing by the parent's zero world scale there is not finite): the
  // object's existing local scale is left as is.
  EXPECT_NEAR(localScaleAfter.y, localScaleBefore.y, 1e-4f);
}

TEST(SceneEdit, ReparentingOntoAParentWithADenormalWorldScaleAxisKeepsThatAxisAndCompensatesOthers)
{
  const auto scene = makeScene();

  // float's largest finite value is about 3.4e38, so any divisor smaller than (numerator / 3.4e38)
  // makes the division overflow to +/-infinity regardless of the numerator's exact value. For an
  // ordinary object scale that threshold sits around 1e-38; 1e-40f is comfortably below it and is
  // itself in the denormal range (below the smallest normal float, ~1.18e-38), so this is chosen by
  // that reasoning rather than by trial and error, and is a different case from the exact-zero one
  // covered above.
  const auto parent = addObject(scene, "Parent", glm::vec3(10.0f, -4.0f, 7.0f), glm::vec3(2.0f, 1e-40f, 3.0f));
  transformOf(parent)->setRotation(glm::vec3(0.0f, 45.0f, 0.0f));

  transformOf(scene.object)->setPosition(glm::vec3(5.0f, 2.0f, -1.0f));
  transformOf(scene.object)->setRotation(glm::vec3(10.0f, 20.0f, 30.0f));
  transformOf(scene.object)->setScale(glm::vec3(4.0f, 6.0f, 8.0f));

  const auto worldPositionBefore = transformOf(scene.object)->getPosition();
  const auto worldRotationBefore = transformOf(scene.object)->getRotation();
  const auto worldScaleBefore = transformOf(scene.object)->getScale();
  const auto localScaleBefore = transformOf(scene.object)->getLocalScale();

  const auto parentUUID = parent->getUUID();
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &parentUUID)),
            SceneEditResult::applied);
  EXPECT_EQ(scene.object->getParent(), parent);

  expectNear("world position", transformOf(scene.object)->getPosition(), worldPositionBefore);
  expectNear("world rotation", transformOf(scene.object)->getRotation(), worldRotationBefore);

  // x and z divide cleanly against the parent's ordinary scale on those axes.
  EXPECT_NEAR(transformOf(scene.object)->getScale().x, worldScaleBefore.x, 1e-4f);
  EXPECT_NEAR(transformOf(scene.object)->getScale().z, worldScaleBefore.z, 1e-4f);

  const auto localScaleAfter = transformOf(scene.object)->getLocalScale();
  EXPECT_NEAR(localScaleAfter.x, worldScaleBefore.x / 2.0f, 1e-4f);
  EXPECT_NEAR(localScaleAfter.z, worldScaleBefore.z / 3.0f, 1e-4f);

  // y overflows to infinity rather than dividing cleanly, so the guard keeps the existing local
  // value there instead of writing a non-finite scale into the live transform.
  EXPECT_TRUE(std::isfinite(localScaleAfter.y));
  EXPECT_NEAR(localScaleAfter.y, localScaleBefore.y, 1e-4f);
}

TEST(SceneEdit, ReparentsAnObjectWithNoTransformWithoutThrowing)
{
  const auto scene = makeScene();

  const auto parent = addObject(scene, "Parent", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(2.0f, 2.0f, 2.0f));

  const auto noTransform = std::make_shared<Object>(std::vector<std::shared_ptr<Component>>{}, "NoTransform");
  scene.objectManager->addObject(noTransform);
  ASSERT_EQ(noTransform->getComponent<Transform>(ComponentType::transform), nullptr);

  const auto parentUUID = parent->getUUID();
  SceneEditResult result = SceneEditResult::failed;
  EXPECT_NO_THROW(result = applyEdit(scene, replication::buildReparentObject(noTransform->getUUID(), &parentUUID)));
  EXPECT_EQ(result, SceneEditResult::applied);
  EXPECT_EQ(noTransform->getParent(), parent);
  EXPECT_EQ(noTransform->getComponent<Transform>(ComponentType::transform), nullptr);
}

TEST(SceneEdit, RejectsReparentOntoDescendantAndLeavesTransformUnchanged)
{
  const auto scene = makeScene();

  transformOf(scene.object)->setPosition(glm::vec3(4.0f, 5.0f, 6.0f));
  transformOf(scene.object)->setScale(glm::vec3(2.0f, 2.0f, 2.0f));

  const auto child = addChildObject(scene, "Child", scene.object);
  transformOf(child)->setPosition(glm::vec3(1.0f, 0.0f, 0.0f));

  const auto descendantUUID = child->getUUID();

  // Positive control: dropping an object onto its own descendant is still rejected, and neither the
  // hierarchy nor the transform this fix rewrites are touched by the attempt.
  EXPECT_EQ(applyEdit(scene, replication::buildReparentObject(scene.object->getUUID(), &descendantUUID)),
            SceneEditResult::rejected);
  EXPECT_EQ(scene.object->getParent(), nullptr);
  expectNear("local position", transformOf(scene.object)->getLocalPosition(), glm::vec3(4.0f, 5.0f, 6.0f));
  expectNear("local scale", transformOf(scene.object)->getLocalScale(), glm::vec3(2.0f, 2.0f, 2.0f));
}
