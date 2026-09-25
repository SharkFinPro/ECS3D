#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "EditHistoryFixtures.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <uuid.h>
#include <vector>

namespace {
  using namespace editHistoryFixtures;

  // Parent P with children D and C, C with its own child G - the fixture the parent+child batch-delete
  // tests share. Everything starts at the identity transform; the tests only check tree shape/order.
  struct ParentAndChild {
    std::shared_ptr<Object> p;
    std::shared_ptr<Object> d;
    std::shared_ptr<Object> c;
    std::shared_ptr<Object> g;
  };

  ParentAndChild buildParentAndChild(Scene& scene)
  {
    ParentAndChild tree;
    tree.p = scene.object;
    tree.d = addChildObject(scene, "D", tree.p);
    tree.c = addChildObject(scene, "C", tree.p);
    tree.g = addChildObject(scene, "G", tree.c);
    return tree;
  }
}

// --- Recording and undoing/redoing a grouped batch delete of a parent and its own child together.

TEST(EditHistoryBatch, RecordAndUndoAndRedoADeleteOfAParentAndItsOwnChild)
{
  auto scene = makeScene();
  const auto tree = buildParentAndChild(scene);

  // Selection order [P, C]: P first, then C added to the selection - the shape ObjectGUIManager's
  // queueDeletion sends.
  const auto batch = replication::buildBatch({
    replication::buildRemoveObject(tree.p->getUUID()),
    replication::buildRemoveObject(tree.c->getUUID())
  });

  // Derived from the pre-batch view, simulating the batch on a scratch copy as it goes - see
  // RecordEdits.h's commandsForSceneEdit.
  const auto commands = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  ASSERT_EQ(commands->size(), 2u);

  // Applied to the live scene exactly as the server would: P is removed (D and C promoted to root), then
  // C is removed (G promoted to root in turn).
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, batch), replication::SceneEditResult::applied);
  ASSERT_EQ(scene.objectManager->getObjects(), (std::vector<std::shared_ptr<Object>>{ tree.d, tree.g }));
  ASSERT_EQ(scene.objectManager->getObjectByUUID(tree.p->getUUID()), nullptr);
  ASSERT_EQ(scene.objectManager->getObjectByUUID(tree.c->getUUID()), nullptr);

  edits::EditHistory history;
  history.recordBatch(*commands);
  EXPECT_EQ(history.undoDepth(), 1u); // one entry, not two, for the two commands

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(undoOutcome.jsonPayload->at("op"), "batch"); // one sceneEdit, not two round trips
  EXPECT_EQ(history.undoDepth(), 0u);
  EXPECT_EQ(history.redoDepth(), 1u);

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);

  // P is back with its original uuid, at the scene root, with D and C restored as its children (in their
  // original order) and G restored under C.
  ASSERT_EQ(scene.objectManager->getObjects(), (std::vector<std::shared_ptr<Object>>{ tree.p }));
  const auto restoredP = scene.objectManager->getObjectByUUID(tree.p->getUUID());
  ASSERT_NE(restoredP, nullptr);
  EXPECT_EQ(restoredP->getChildren(), (std::vector<std::shared_ptr<Object>>{ tree.d, tree.c }));
  const auto restoredC = scene.objectManager->getObjectByUUID(tree.c->getUUID());
  ASSERT_NE(restoredC, nullptr);
  EXPECT_EQ(restoredC->getChildren(), (std::vector<std::shared_ptr<Object>>{ tree.g }));
  EXPECT_NE(scene.objectManager->getObjectByUUID(tree.g->getUUID()), nullptr);

  // Redo: one sceneEdit batch reapplies both removals, leaving G and D promoted exactly as the original
  // batch did.
  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(redoOutcome.jsonPayload->at("op"), "batch");
  EXPECT_EQ(history.undoDepth(), 1u);
  EXPECT_EQ(history.redoDepth(), 0u);

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjects(), (std::vector<std::shared_ptr<Object>>{ tree.d, tree.g }));
  EXPECT_EQ(scene.objectManager->getObjectByUUID(tree.p->getUUID()), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(tree.c->getUUID()), nullptr);
}

TEST(EditHistoryBatch, RecordAndUndoAndRedoABatchDuplicateOfTwoObjects)
{
  auto scene = makeScene();
  const auto a = addObject(scene, "A");
  const auto b = addObject(scene, "B");

  const auto duplicateA = someOtherUUID();
  const auto duplicateB = anotherUUID();
  const auto batch = replication::buildBatch({
    replication::buildDuplicateObject(a->getUUID(), &duplicateA),
    replication::buildDuplicateObject(b->getUUID(), &duplicateB)
  });

  const auto commands = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  ASSERT_EQ(commands->size(), 2u);

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, batch), replication::SceneEditResult::applied);
  ASSERT_NE(scene.objectManager->getObjectByUUID(duplicateA), nullptr);
  ASSERT_NE(scene.objectManager->getObjectByUUID(duplicateB), nullptr);

  edits::EditHistory history;
  history.recordBatch(*commands);
  EXPECT_EQ(history.undoDepth(), 1u);
  EXPECT_EQ(*history.nextUndoLabel(*scene.objectManager), "Duplicate 2 Objects");

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(duplicateA), nullptr);
  EXPECT_EQ(scene.objectManager->getObjectByUUID(duplicateB), nullptr);
  // The originals are untouched - a batch duplicate only ever removes/creates the copies.
  EXPECT_NE(scene.objectManager->getObjectByUUID(a->getUUID()), nullptr);
  EXPECT_NE(scene.objectManager->getObjectByUUID(b->getUUID()), nullptr);

  EXPECT_EQ(*history.nextRedoLabel(*scene.objectManager), "Duplicate 2 Objects");
  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_NE(scene.objectManager->getObjectByUUID(duplicateA), nullptr);
  EXPECT_NE(scene.objectManager->getObjectByUUID(duplicateB), nullptr);
}

TEST(EditHistoryBatch, LabelIsDeleteNObjectsForAnAllRemoveObjectGroup)
{
  auto scene = makeScene();
  const auto tree = buildParentAndChild(scene);

  const auto batch = replication::buildBatch({
    replication::buildRemoveObject(tree.p->getUUID()),
    replication::buildRemoveObject(tree.c->getUUID())
  });

  const auto commands = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());

  edits::EditHistory history;
  history.recordBatch(*commands);

  EXPECT_EQ(*history.nextUndoLabel(*scene.objectManager), "Delete 2 Objects");
}

// A single-command group behaves exactly like record(): no scratch copy, no batch wrapping, so the
// payload it sends is unchanged from before recordBatch existed.
TEST(EditHistoryBatch, ASingleCommandGroupBehavesExactlyLikeRecord)
{
  auto scene = makeScene();
  const auto child = addObject(scene, "Child");

  edits::EditHistory history;
  history.recordBatch({ edits::EditCommand::removeObject(child->getUUID(), std::nullopt, 1,
                                                          child->serialize()) });

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveObject(child->getUUID())),
            replication::SceneEditResult::applied);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(undoOutcome.jsonPayload->at("op"), "restoreObject"); // not wrapped in a "batch" op
  EXPECT_EQ(replication::applySceneEdit(*scene.objectManager, *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_NE(scene.objectManager->getObjectByUUID(child->getUUID()), nullptr);
}

TEST(EditHistoryBatch, RecordBatchWithNoCommandsIsIgnored)
{
  edits::EditHistory history;
  history.recordBatch({});
  EXPECT_FALSE(history.canUndo());
}

TEST(EditHistoryBatch, RecordBatchRefusesAMixOfPayloadForms)
{
  auto scene = makeScene();
  const auto transform = transformOf(scene.object);
  const auto before = transform->serialize();
  transform->setPosition({ 1, 0, 0 });
  const auto after = transform->serialize();

  edits::EditHistory history;
  EXPECT_THROW(history.recordBatch({
    edits::EditCommand::componentEdit(scene.object->getUUID(), before, after), // networkMessage form
    edits::EditCommand::renameObject(scene.object->getUUID(), "Object", "Renamed") // sceneEdit form
  }), std::invalid_argument);
}

// After a batch is recorded, one of its targets is removed out from under it before undo runs - the whole
// grouped entry refuses (nothing is sent) rather than undoing only part of the group.
TEST(EditHistoryBatch, UndoOfABatchRefusesWhenOneTargetIsGoneAndSendsNothing)
{
  auto scene = makeScene();
  const auto a = addObject(scene, "A");
  const auto b = addObject(scene, "B");

  const auto duplicateA = someOtherUUID();
  const auto duplicateB = anotherUUID();
  const auto batch = replication::buildBatch({
    replication::buildDuplicateObject(a->getUUID(), &duplicateA),
    replication::buildDuplicateObject(b->getUUID(), &duplicateB)
  });

  const auto commands = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, batch), replication::SceneEditResult::applied);

  edits::EditHistory history;
  history.recordBatch(*commands);

  // Something else deletes the first-recorded duplicate's whole subtree before undo runs.
  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, replication::buildRemoveSubtree(duplicateA)),
            replication::SceneEditResult::applied);
  ASSERT_EQ(scene.objectManager->getObjectByUUID(duplicateA), nullptr);
  ASSERT_NE(scene.objectManager->getObjectByUUID(duplicateB), nullptr);

  const auto outcome = history.undo(*scene.objectManager);
  EXPECT_FALSE(outcome.ok());
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_FALSE(history.canUndo());

  // Untouched by the refused undo: the surviving duplicate is still there.
  EXPECT_NE(scene.objectManager->getObjectByUUID(duplicateB), nullptr);
}
