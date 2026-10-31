#include <gtest/gtest.h>

#include "TestScene.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneManager.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Component.h"
#include "objects/components/Script.h"
#include <Protocol.h>
#include "RecordEditsFixtures.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <uuid.h>
#include <vector>

namespace {
  using namespace recordEditsFixtures;

  // Undo of a structural edit can rebuild objects, so sibling lists are compared by uuid, not by pointer.
  std::vector<uuids::uuid> uuidsOf(const std::vector<std::shared_ptr<Object>>& objects)
  {
    std::vector<uuids::uuid> result;
    for (const auto& object : objects)
    {
      result.push_back(object->getUUID());
    }

    return result;
  }

  void applyPayload(const Scene& scene, const std::optional<nlohmann::json>& payload)
  {
    ASSERT_TRUE(payload.has_value());
    ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, *payload),
              replication::SceneEditResult::applied);
  }
}

// --- reorderObject: the command is derived from the pre-edit view, and undoing and redoing it lands the
// object back on the sibling slots the view read.

TEST(RecordEdits, ASameParentReorderRecordsBothSlotsAndUndoesAndRedoesThem)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  const auto b = addChildObject(scene, "B", scene.object);
  const auto c = addChildObject(scene, "C", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto edit = replication::buildReorderObject(a->getUUID(), &parentUUID, 2);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, parentUUID, 2));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ b->getUUID(), c->getUUID(), a->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ a->getUUID(), b->getUUID(), c->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()),
            (std::vector<uuids::uuid>{ b->getUUID(), c->getUUID(), a->getUUID() }));
}

TEST(RecordEdits, AReorderToAnotherParentRecordsTheSourceSlotAndUndoesAndRedoesIt)
{
  const auto scene = makeScene();
  const auto source = addObject(scene, "Source");
  const auto dest = addObject(scene, "Dest");
  const auto first = addChildObject(scene, "First", source);
  const auto moved = addChildObject(scene, "Moved", source);
  const auto destA = addChildObject(scene, "DestA", dest);
  const auto destB = addChildObject(scene, "DestB", dest);
  const auto destUUID = dest->getUUID();

  const auto edit = replication::buildReorderObject(moved->getUUID(), &destUUID, 1);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(moved->getUUID(), source->getUUID(), 1,
                                                        destUUID, 1));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(dest->getChildren()),
            (std::vector<uuids::uuid>{ destA->getUUID(), moved->getUUID(), destB->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(source->getChildren()), (std::vector<uuids::uuid>{ first->getUUID(), moved->getUUID() }));
  EXPECT_EQ(uuidsOf(dest->getChildren()), (std::vector<uuids::uuid>{ destA->getUUID(), destB->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(source->getChildren()), (std::vector<uuids::uuid>{ first->getUUID() }));
  EXPECT_EQ(uuidsOf(dest->getChildren()),
            (std::vector<uuids::uuid>{ destA->getUUID(), moved->getUUID(), destB->getUUID() }));
}

TEST(RecordEdits, AReorderOntoTheSceneRootNamesNoAfterParentAndUndoesBackUnderTheOldOne)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);

  const auto edit = replication::buildReorderObject(child->getUUID(), nullptr, 0);
  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(child->getUUID(), scene.object->getUUID(), 0,
                                                        std::nullopt, 0));

  ASSERT_EQ(replication::applySceneEdit(*scene.objectManager, edit), replication::SceneEditResult::applied);
  ASSERT_EQ(uuidsOf(scene.objectManager->getObjects()),
            (std::vector<uuids::uuid>{ child->getUUID(), scene.object->getUUID() }));

  edits::EditHistory history;
  history.record(*command);

  const auto undoOutcome = history.undo(*scene.objectManager);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, undoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.objectManager->getObjects()), (std::vector<uuids::uuid>{ scene.object->getUUID() }));
  EXPECT_EQ(uuidsOf(scene.object->getChildren()), (std::vector<uuids::uuid>{ child->getUUID() }));

  const auto redoOutcome = history.redo(*scene.objectManager);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_NO_FATAL_FAILURE(applyPayload(scene, redoOutcome.jsonPayload));
  EXPECT_EQ(uuidsOf(scene.objectManager->getObjects()),
            (std::vector<uuids::uuid>{ child->getUUID(), scene.object->getUUID() }));
}

TEST(RecordEdits, AMalformedReorderIsNotRecorded)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto valid = replication::buildReorderObject(child->getUUID(), &parentUUID, 0);
  ASSERT_TRUE(edits::commandForSceneEdit(valid, *scene.objectManager).has_value());

  auto badParent = valid;
  badParent["parent"] = "not-a-uuid";
  EXPECT_FALSE(edits::commandForSceneEdit(badParent, *scene.objectManager).has_value());

  auto nonStringParent = valid;
  nonStringParent["parent"] = 7;
  EXPECT_FALSE(edits::commandForSceneEdit(nonStringParent, *scene.objectManager).has_value());

  auto missingIndex = valid;
  missingIndex.erase("index");
  EXPECT_FALSE(edits::commandForSceneEdit(missingIndex, *scene.objectManager).has_value());

  auto negativeIndex = valid;
  negativeIndex["index"] = -1;
  EXPECT_FALSE(edits::commandForSceneEdit(negativeIndex, *scene.objectManager).has_value());

  auto fractionalIndex = valid;
  fractionalIndex["index"] = 1.5;
  EXPECT_FALSE(edits::commandForSceneEdit(fractionalIndex, *scene.objectManager).has_value());

  auto stringIndex = valid;
  stringIndex["index"] = "0";
  EXPECT_FALSE(edits::commandForSceneEdit(stringIndex, *scene.objectManager).has_value());

  const auto unknownObject = replication::buildReorderObject(someOtherUUID(), &parentUUID, 0);
  EXPECT_FALSE(edits::commandForSceneEdit(unknownObject, *scene.objectManager).has_value());
}

// An object a script spawned mid-pass is findable by uuid but is not in the root list yet, so the view
// cannot say where it sits; the recorded index falls back to the end of the list.
TEST(RecordEdits, ARemovalOfAnObjectNotYetInTheRootListRecordsTheEndOfTheList)
{
  const auto scene = makeScene();

  const ObjectManager::ScriptPassGuard guard(*scene.objectManager);
  const auto spawned = std::make_shared<Object>("Spawned");
  scene.objectManager->addObject(spawned);
  ASSERT_EQ(scene.objectManager->getObjects().size(), 1u);
  ASSERT_EQ(scene.objectManager->getObjectByUUID(spawned->getUUID()), spawned);

  const auto command = edits::commandForSceneEdit(replication::buildRemoveObject(spawned->getUUID()),
                                                  *scene.objectManager);

  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::removeObject(spawned->getUUID(), std::nullopt, 1,
                                                       spawned->serialize()));

  // Positive control: an object that is in the list records its real index.
  const auto listed = edits::commandForSceneEdit(replication::buildRemoveObject(scene.object->getUUID()),
                                                 *scene.objectManager);
  ASSERT_TRUE(listed.has_value());
  EXPECT_EQ(*listed, edits::EditCommand::removeObject(scene.object->getUUID(), std::nullopt, 0,
                                                      scene.object->serialize()));
}

// --- Scene-edit refusals that are not about one op's own fields.

TEST(RecordEdits, ANonObjectEditIsNotRecorded)
{
  const auto scene = makeScene();

  EXPECT_FALSE(edits::commandForSceneEdit(nlohmann::json::array(), *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(nlohmann::json::array(), *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(nlohmann::json("batch"), *scene.objectManager).has_value());

  // Positive control: an object-shaped edit goes through both entry points.
  const auto edit = replication::buildRemoveObject(scene.object->getUUID());
  EXPECT_TRUE(edits::commandForSceneEdit(edit, *scene.objectManager).has_value());
  EXPECT_TRUE(edits::commandsForSceneEdit(edit, *scene.objectManager).has_value());
}

TEST(RecordEdits, OpsMissingTheFieldTheirCommandNeedsAreNotRecorded)
{
  const auto scene = makeScene();
  const auto objectUUID = uuidString(scene.object->getUUID());

  const nlohmann::json renameWithoutName = { { "op", "renameObject" }, { "object", objectUUID } };
  const nlohmann::json renameWithNumber = { { "op", "renameObject" }, { "object", objectUUID },
                                            { "name", 4 } };
  const nlohmann::json addWithoutKey = { { "op", "addComponent" }, { "object", objectUUID } };
  const nlohmann::json removeWithoutType = { { "op", "removeComponent" }, { "object", objectUUID } };
  const nlohmann::json instantiateWithoutPrefab = { { "op", "instantiatePrefab" } };
  const nlohmann::json reparentWithBadParent = { { "op", "reparentObject" }, { "object", objectUUID },
                                                 { "parent", "not-a-uuid" } };

  EXPECT_FALSE(edits::commandForSceneEdit(renameWithoutName, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(renameWithNumber, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(addWithoutKey, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(removeWithoutType, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(instantiateWithoutPrefab, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandForSceneEdit(reparentWithBadParent, *scene.objectManager).has_value());

  // Positive control: the same ops with their field present are recorded.
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildRenameObject(scene.object->getUUID(), "Named"),
                                         *scene.objectManager).has_value());
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildAddComponent(scene.object->getUUID(), "RigidBody"),
                                         *scene.objectManager).has_value());
}

TEST(RecordEdits, ASingleNonBatchOpYieldsAOneElementVectorEqualToItsOwnCommand)
{
  const auto scene = makeScene();

  const auto edit = replication::buildRenameObject(scene.object->getUUID(), "Renamed");
  const auto single = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(single.has_value());

  const auto commands = edits::commandsForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(commands.has_value());
  ASSERT_EQ(commands->size(), 1u);
  EXPECT_EQ(commands->front(), *single);

  // A non-batch op with nothing to record gives nothing, not an empty vector.
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildRenameObject(someOtherUUID(), "Renamed"),
                                           *scene.objectManager).has_value());
}

namespace
{
  nlohmann::json signedReorder(const std::shared_ptr<Object>& object, const int index)
  {
    return {
      { "op", "reorderObject" },
      { "object", uuids::to_string(object->getUUID()) },
      { "index", index }
    };
  }
}

TEST(RecordEdits, AReorderWithASignedIndexIsRecordedLikeAnUnsignedOne)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto edit = signedReorder(a, 1);
  ASSERT_TRUE(edit.at("index").is_number_integer());
  ASSERT_FALSE(edit.at("index").is_number_unsigned());

  const auto command = edits::commandForSceneEdit(edit, *scene.objectManager);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::reorderObject(a->getUUID(), parentUUID, 0, std::nullopt, 1));
}

TEST(RecordEdits, ANegativeSignedReorderIndexIsRefusedByTheRecorderAndTheApplier)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);

  EXPECT_TRUE(edits::commandForSceneEdit(signedReorder(a, 0), *scene.objectManager).has_value());

  const auto negative = signedReorder(a, -1);
  EXPECT_FALSE(edits::commandForSceneEdit(negative, *scene.objectManager).has_value());
  EXPECT_NE(replication::applySceneEdit(*scene.objectManager, negative), replication::SceneEditResult::applied);
}

TEST(RecordEdits, ABatchWithASignedIndexReorderIsRecorded)
{
  const auto scene = makeScene();
  const auto a = addChildObject(scene, "A", scene.object);
  addChildObject(scene, "B", scene.object);

  const auto batch = replication::buildBatch({ signedReorder(a, 1) });
  const auto recorded = edits::commandsForSceneEdit(batch, *scene.objectManager);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(recorded->size(), 1u);
}

TEST(RecordEdits, ParseIndexFieldAcceptsNonNegativeIntegersOnly)
{
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(std::uint64_t{ 3 })), std::optional<std::size_t>(3));
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(3)), std::optional<std::size_t>(3));
  EXPECT_EQ(replication::parseIndexField(nlohmann::json(0)), std::optional<std::size_t>(0));
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json(-1)).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json(1.5)).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json("1")).has_value());
  EXPECT_FALSE(replication::parseIndexField(nlohmann::json()).has_value());
}

TEST(RecordEdits, ABatchThatCannotBeFullyRecordedIsNotRecorded)
{
  const auto scene = makeScene();
  const auto child = addChildObject(scene, "Child", scene.object);
  const auto parentUUID = scene.object->getUUID();

  const auto rename = replication::buildRenameObject(scene.object->getUUID(), "Renamed");
  const auto createdUUID = someOtherUUID();
  const auto addObjectOp = replication::buildAddObject("New", nullptr, &createdUUID);

  const auto valid = replication::buildBatch({ rename, addObjectOp });
  const auto recorded = edits::commandsForSceneEdit(valid, *scene.objectManager);
  ASSERT_TRUE(recorded.has_value());
  EXPECT_EQ(recorded->size(), 2u);

  nlohmann::json withoutOps = valid;
  withoutOps.erase("ops");
  EXPECT_FALSE(edits::commandsForSceneEdit(withoutOps, *scene.objectManager).has_value());

  nlohmann::json opsNotAnArray = valid;
  opsNotAnArray["ops"] = "rename";
  EXPECT_FALSE(edits::commandsForSceneEdit(opsNotAnArray, *scene.objectManager).has_value());

  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({}), *scene.objectManager).has_value());

  // One op the view cannot derive a command for.
  const auto unknownTarget = replication::buildBatch(
    { rename, replication::buildRenameObject(someOtherUUID(), "Ghost") });
  EXPECT_FALSE(edits::commandsForSceneEdit(unknownTarget, *scene.objectManager).has_value());

  // A creating op that names no uuid has no faithful undo, so it spoils the whole batch.
  const auto unnamedCreate = replication::buildBatch({ rename, replication::buildAddObject("New") });
  EXPECT_FALSE(edits::commandsForSceneEdit(unnamedCreate, *scene.objectManager).has_value());

  // One op that records but that the authority would refuse: index 5 is past the end of the list.
  const auto pastTheEnd = replication::buildReorderObject(child->getUUID(), &parentUUID, 5);
  ASSERT_TRUE(edits::commandForSceneEdit(pastTheEnd, *scene.objectManager).has_value());
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({ rename, pastTheEnd }),
                                           *scene.objectManager).has_value());

  // A batch inside a batch is not an op either.
  EXPECT_FALSE(edits::commandsForSceneEdit(replication::buildBatch({ rename, valid }),
                                           *scene.objectManager).has_value());
}

