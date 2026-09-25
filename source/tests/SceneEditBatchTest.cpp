#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "SceneEditFixtures.h"
#include "ObjectManagerFixtures.h"

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace sceneEditFixtures;
}

TEST(SceneEditBatch, RemovingTwoUnrelatedObjectsRemovesBothInOneCall)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");
  const auto b = addObject(scene, "B");

  const auto batch = replication::buildBatch({
    replication::buildRemoveObject(a->getUUID()),
    replication::buildRemoveObject(b->getUUID())
  });

  EXPECT_EQ(applyEdit(scene, batch), SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(b->getUUID()), nullptr);
}

TEST(SceneEditBatch, ASecondOpNamingAnUnknownObjectLeavesTheSceneUntouched)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");

  // Positive control: the first op alone, on its own, applies fine - so the batch failure below is really
  // about the second op, not some other problem with the first.
  {
    const auto soloScene = makeScene();
    const auto soloA = addObject(soloScene, "A");
    EXPECT_EQ(applyEdit(soloScene, replication::buildRemoveObject(soloA->getUUID())),
              SceneEditResult::applied);
  }

  const auto batch = replication::buildBatch({
    replication::buildRemoveObject(a->getUUID()),
    replication::buildRemoveObject(objectManagerFixtures::unknownUUID())
  });

  EXPECT_EQ(applyEdit(scene, batch), SceneEditResult::unknownObject);

  // Nothing applied: the first op's target is still there, since the dry run against the scratch copy
  // caught the second op's refusal before either op touched the real scene.
  EXPECT_NE(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
}

TEST(SceneEditBatch, EmptyOpsIsMalformed)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");

  const nlohmann::json batch = { { "op", "batch" }, { "ops", nlohmann::json::array() } };

  EXPECT_EQ(applyEdit(scene, batch), SceneEditResult::malformedEdit);
  EXPECT_NE(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
}

TEST(SceneEditBatch, NonArrayOpsIsMalformed)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");

  const nlohmann::json batch = { { "op", "batch" }, { "ops", "not an array" } };

  EXPECT_EQ(applyEdit(scene, batch), SceneEditResult::malformedEdit);
  EXPECT_NE(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
}

TEST(SceneEditBatch, ANestedBatchIsMalformed)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");

  const auto inner = replication::buildBatch({ replication::buildRemoveObject(a->getUUID()) });
  const auto outer = replication::buildBatch({ inner });

  EXPECT_EQ(applyEdit(scene, outer), SceneEditResult::malformedEdit);
  EXPECT_NE(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
}

TEST(SceneEditBatch, TwoDuplicateObjectOpsWithClientUuidsCreateBothCopies)
{
  const auto scene = makeScene();
  const auto a = addObject(scene, "A");
  const auto b = addObject(scene, "B");

  const auto duplicateA = someOtherUUID();
  const auto duplicateB = anotherUUID();

  const auto batch = replication::buildBatch({
    replication::buildDuplicateObject(a->getUUID(), &duplicateA),
    replication::buildDuplicateObject(b->getUUID(), &duplicateB)
  });

  EXPECT_EQ(applyEdit(scene, batch), SceneEditResult::applied);
  EXPECT_NE(scene.objectManager->getObjectByUUID(duplicateA), nullptr);
  EXPECT_NE(scene.objectManager->getObjectByUUID(duplicateB), nullptr);
}

TEST(SceneEditBatch, ScratchCopyHasTheSameUuidsAndShapeAndIsIndependentOfTheSource)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);
  const auto grandchild = addChildObject(scene, "Grandchild", child);

  const auto copy = makeScratchCopy(*scene.objectManager);
  ASSERT_NE(copy, nullptr);

  std::vector<uuids::uuid> sourceUUIDs;
  for (const auto& root : scene.objectManager->getObjects())
  {
    objectManagerFixtures::collectUUIDs(root, sourceUUIDs);
  }

  std::vector<uuids::uuid> copyUUIDs;
  for (const auto& root : copy->getObjects())
  {
    objectManagerFixtures::collectUUIDs(root, copyUUIDs);
  }

  ASSERT_EQ(sourceUUIDs.size(), copyUUIDs.size());
  for (const auto& uuid : sourceUUIDs)
  {
    EXPECT_NE(std::ranges::find(copyUUIDs, uuid), copyUUIDs.end());
  }

  ASSERT_EQ(copy->getObjects().size(), scene.objectManager->getObjects().size());
  const auto copiedRoot = copy->getObjectByUUID(scene.object->getUUID());
  ASSERT_NE(copiedRoot, nullptr);
  ASSERT_EQ(copiedRoot->getChildren().size(), 1u);
  EXPECT_EQ(copiedRoot->getChildren().front()->getUUID(), child->getUUID());
  EXPECT_NE(copiedRoot->getChildren().front(), child); // a distinct Object, not the same one

  const auto copiedGrandchild = copy->getObjectByUUID(grandchild->getUUID());
  ASSERT_NE(copiedGrandchild, nullptr);
  EXPECT_EQ(copiedGrandchild->getParent(), copiedRoot->getChildren().front());

  // Mutating the copy must not touch the source: remove the copy's child and confirm the source keeps it.
  const auto copiedChild = copy->getObjectByUUID(child->getUUID());
  ASSERT_NE(copiedChild, nullptr);
  copy->removeObject(copiedChild);
  copy->deleteObjectsMarkedForDeletion();
  EXPECT_EQ(copy->getObjectByUUID(child->getUUID()), nullptr);
  EXPECT_NE(scene.objectManager->getObjectByUUID(child->getUUID()), nullptr);
}
