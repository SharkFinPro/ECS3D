#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneScriptEdits.h"
#include "Replication.h"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  using replication::SceneEditResult;
  using edits::HistoryResult;

  const auto sceneUUID = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();
  const auto otherSceneUUID = uuids::uuid::from_string("77777777-7777-7777-7777-777777777777").value();

  const auto alphaFields = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":3}])");
  const auto gammaFields = nlohmann::json::parse(R"([{"name":"on","type":"bool","value":true}])");
  const auto otherFields = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":9}])");

  std::shared_ptr<ComponentRegistry> makeRegistry()
  {
    auto registry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*registry);

    return registry;
  }

  // One object plus the scene scripts Alpha (fields), Beta and Gamma (fields), in that order.
  std::shared_ptr<SceneAsset> makeScene(const uuids::uuid& uuid = sceneUUID)
  {
    auto scene = std::make_shared<SceneAsset>(uuid, "Main", makeRegistry());
    scene->getObjectManager()->addObject(std::make_shared<Object>("Thing"));

    const std::vector<std::pair<std::string, nlohmann::json>> entries = {
      { "Alpha", alphaFields }, { "Beta", nlohmann::json::array() }, { "Gamma", gammaFields }
    };

    for (const auto& [name, fields] : entries)
    {
      auto script = std::make_shared<Script>(name);
      script->setFields(fields);
      EXPECT_TRUE(scene->addScript(script));
    }

    return scene;
  }

  // The whole list as serialized: order, class names and field blobs. Field arrays are ordered as authored,
  // so comparing them exactly is sound.
  nlohmann::json scriptsOf(const std::shared_ptr<SceneAsset>& scene)
  {
    return scene->serialize().at("scripts");
  }

  struct Session {
    std::shared_ptr<SceneAsset> scene = makeScene();
    edits::EditHistory history;

    [[nodiscard]] const ObjectManager& objects() const
    {
      return *scene->getObjectManager();
    }

    // Records the command for the edit against the pre-edit scene, then applies it, as the editor and the
    // authority do between them.
    void edit(const nlohmann::json& edit)
    {
      const auto command = edits::commandForSceneEdit(edit, objects(), nullptr, scene.get());
      ASSERT_TRUE(command.has_value()) << edit.dump();
      history.record(*command);

      ASSERT_EQ(replication::applySceneScriptEdit(*scene, edit), SceneEditResult::applied) << edit.dump();
    }

    // What a connection other than this history's owner would do to the scene.
    void applyExternally(const nlohmann::json& edit) const
    {
      ASSERT_EQ(replication::applySceneScriptEdit(*scene, edit), SceneEditResult::applied) << edit.dump();
    }

    edits::HistoryOutcome undo()
    {
      return history.undo(objects(), nullptr, scene.get());
    }

    edits::HistoryOutcome redo()
    {
      return history.redo(objects(), nullptr, scene.get());
    }

    void applyPayload(const edits::HistoryOutcome& outcome) const
    {
      ASSERT_TRUE(outcome.ok());
      ASSERT_TRUE(outcome.jsonPayload.has_value());
      ASSERT_TRUE(replication::isSceneScriptEdit(*outcome.jsonPayload));
      ASSERT_EQ(replication::applySceneScriptEdit(*scene, *outcome.jsonPayload), SceneEditResult::applied)
        << outcome.jsonPayload->dump();
    }
  };

  // Records and applies the edit, undoes it, redoes it and undoes it again, checking the list each time.
  void expectRoundTrip(const nlohmann::json& edit, const std::string& expectedLabel)
  {
    Session session;
    const auto original = scriptsOf(session.scene);

    session.edit(edit);
    const auto edited = scriptsOf(session.scene);
    ASSERT_NE(edited, original);

    EXPECT_EQ(session.history.nextUndoLabel(session.objects()), expectedLabel);

    session.applyPayload(session.undo());
    EXPECT_EQ(scriptsOf(session.scene), original);

    session.applyPayload(session.redo());
    EXPECT_EQ(scriptsOf(session.scene), edited);

    session.applyPayload(session.undo());
    EXPECT_EQ(scriptsOf(session.scene), original);
  }
}

TEST(SceneScriptUndoTest, AddRoundTripsWithItsIndexAndFields)
{
  expectRoundTrip(replication::buildAddSceneScript("Delta", 1, &otherFields), "Add Scene Script Delta");
}

TEST(SceneScriptUndoTest, AddWithoutIndexOrFieldsRoundTrips)
{
  expectRoundTrip(replication::buildAddSceneScript("Delta"), "Add Scene Script Delta");
}

TEST(SceneScriptUndoTest, AddPastTheEndRecordsWhereItLanded)
{
  Session session;
  const auto edit = replication::buildAddSceneScript("Delta", 50);

  // addScript clamps on redo, so only the recorded command itself shows whether the raw index was kept.
  const auto command = edits::commandForSceneEdit(edit, session.objects(), nullptr, session.scene.get());
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::addSceneScript(sceneUUID, "Delta", 3, nlohmann::json(nullptr)));
  EXPECT_NE(*command, edits::EditCommand::addSceneScript(sceneUUID, "Delta", 50, nlohmann::json(nullptr)));

  session.edit(edit);
  session.applyPayload(session.undo());
  session.applyPayload(session.redo());

  EXPECT_EQ(session.scene->getScripts().back()->getClassName(), "Delta");
  EXPECT_EQ(session.scene->getScripts().size(), 4u);
}

TEST(SceneScriptUndoTest, RemoveRoundTripsRestoringIndexAndFields)
{
  expectRoundTrip(replication::buildRemoveSceneScript("Alpha"), "Remove Scene Script Alpha");
}

TEST(SceneScriptUndoTest, RemoveOfAMiddleEntryRoundTrips)
{
  expectRoundTrip(replication::buildRemoveSceneScript("Beta"), "Remove Scene Script Beta");
}

TEST(SceneScriptUndoTest, ReorderRoundTrips)
{
  expectRoundTrip(replication::buildReorderSceneScript("Alpha", 2), "Reorder Scene Script Alpha");
}

TEST(SceneScriptUndoTest, SetFieldsRoundTrips)
{
  expectRoundTrip(replication::buildSetSceneScriptFields("Alpha", otherFields), "Edit Scene Script Alpha");
}

TEST(SceneScriptUndoTest, RedoLabelsNameTheSameAction)
{
  Session session;
  session.edit(replication::buildRemoveSceneScript("Gamma"));
  session.applyPayload(session.undo());

  EXPECT_EQ(session.history.nextRedoLabel(session.objects()), "Remove Scene Script Gamma");
  EXPECT_FALSE(session.history.nextUndoLabel(session.objects()).has_value());
}

TEST(SceneScriptUndoTest, UndoOfAnAddRefusedWhenTheScriptIsGone)
{
  Session session;
  session.edit(replication::buildSetSceneScriptFields("Beta", gammaFields));
  session.edit(replication::buildAddSceneScript("Delta"));
  session.applyExternally(replication::buildRemoveSceneScript("Delta"));

  const auto outcome = session.undo();

  EXPECT_EQ(outcome.result, HistoryResult::targetMissing);
  EXPECT_EQ(outcome.conflict, sceneUUID);
  EXPECT_FALSE(outcome.jsonPayload.has_value());
  EXPECT_FALSE(session.history.canUndo());
}

TEST(SceneScriptUndoTest, UndoOfARemoveRefusedWhenTheClassWasReAdded)
{
  Session session;
  session.edit(replication::buildRemoveSceneScript("Beta"));
  session.applyExternally(replication::buildAddSceneScript("Beta"));

  const auto outcome = session.undo();

  EXPECT_EQ(outcome.result, HistoryResult::targetChanged);
  EXPECT_EQ(outcome.conflict, sceneUUID);
  EXPECT_FALSE(session.history.canUndo());
}

TEST(SceneScriptUndoTest, UndoOfAReorderRefusedWhenTheEntryMovedAgain)
{
  Session session;
  session.edit(replication::buildReorderSceneScript("Alpha", 2));
  session.applyExternally(replication::buildReorderSceneScript("Alpha", 0));

  const auto outcome = session.undo();

  EXPECT_EQ(outcome.result, HistoryResult::targetChanged);
  EXPECT_EQ(outcome.conflict, sceneUUID);
  EXPECT_FALSE(session.history.canUndo());
}

TEST(SceneScriptUndoTest, UndoOfAReorderRefusedWhenTheEntryIsGone)
{
  Session session;
  session.edit(replication::buildReorderSceneScript("Alpha", 2));
  session.applyExternally(replication::buildRemoveSceneScript("Alpha"));

  EXPECT_EQ(session.undo().result, HistoryResult::targetMissing);
}

TEST(SceneScriptUndoTest, UndoOfAFieldEditRefusedWhenTheFieldsChangedAgain)
{
  Session session;
  session.edit(replication::buildSetSceneScriptFields("Alpha", otherFields));
  session.applyExternally(replication::buildSetSceneScriptFields("Alpha", gammaFields));

  const auto outcome = session.undo();

  EXPECT_EQ(outcome.result, HistoryResult::targetChanged);
  EXPECT_EQ(outcome.conflict, sceneUUID);
  EXPECT_FALSE(session.history.canUndo());
  EXPECT_EQ(session.scene->findScript("Alpha")->getFields(), gammaFields);
}

TEST(SceneScriptUndoTest, UndoOfAFieldEditRefusedWhenTheScriptIsGone)
{
  Session session;
  session.edit(replication::buildSetSceneScriptFields("Alpha", otherFields));
  session.applyExternally(replication::buildRemoveSceneScript("Alpha"));

  EXPECT_EQ(session.undo().result, HistoryResult::targetMissing);
}

TEST(SceneScriptUndoTest, RefusalDropsOlderUndoEntriesToo)
{
  Session session;
  session.edit(replication::buildAddSceneScript("Delta"));
  session.edit(replication::buildSetSceneScriptFields("Alpha", otherFields));
  session.applyExternally(replication::buildSetSceneScriptFields("Alpha", gammaFields));

  ASSERT_EQ(session.history.undoDepth(), 2u);
  EXPECT_EQ(session.undo().result, HistoryResult::targetChanged);
  EXPECT_EQ(session.history.undoDepth(), 0u);

  // Positive control: with the target untouched the same entry is undone and the older one stays.
  Session control;
  control.edit(replication::buildAddSceneScript("Delta"));
  control.edit(replication::buildSetSceneScriptFields("Alpha", otherFields));
  EXPECT_TRUE(control.undo().ok());
  EXPECT_EQ(control.history.undoDepth(), 1u);
}

TEST(SceneScriptUndoTest, RedoOfAnAddRefusedWhenTheClassAppearedMeanwhile)
{
  Session session;
  session.edit(replication::buildAddSceneScript("Delta"));
  session.applyPayload(session.undo());
  session.applyExternally(replication::buildAddSceneScript("Delta", 0, &otherFields));

  const auto outcome = session.redo();

  EXPECT_EQ(outcome.result, HistoryResult::targetChanged);
  EXPECT_EQ(outcome.conflict, sceneUUID);
  EXPECT_FALSE(session.history.canRedo());
}

TEST(SceneScriptUndoTest, RedoOfARemoveRefusedWhenTheClassVanishedMeanwhile)
{
  Session session;
  session.edit(replication::buildRemoveSceneScript("Beta"));
  session.applyPayload(session.undo());
  session.applyExternally(replication::buildRemoveSceneScript("Beta"));

  EXPECT_EQ(session.redo().result, HistoryResult::targetMissing);
}

TEST(SceneScriptUndoTest, RedoOfAReorderAndAFieldEditAreRefusedWhenTheirBeforeStateMoved)
{
  Session reorder;
  reorder.edit(replication::buildReorderSceneScript("Alpha", 2));
  reorder.applyPayload(reorder.undo());
  reorder.applyExternally(replication::buildReorderSceneScript("Alpha", 1));
  EXPECT_EQ(reorder.redo().result, HistoryResult::targetChanged);

  Session fields;
  fields.edit(replication::buildSetSceneScriptFields("Alpha", otherFields));
  fields.applyPayload(fields.undo());
  fields.applyExternally(replication::buildSetSceneScriptFields("Alpha", gammaFields));
  EXPECT_EQ(fields.redo().result, HistoryResult::targetChanged);
}

TEST(SceneScriptUndoTest, UndoAndRedoWithoutASceneAreRefusedWithoutCrashing)
{
  Session session;
  session.edit(replication::buildRemoveSceneScript("Beta"));
  const auto listAfterEdit = scriptsOf(session.scene);

  const auto undoOutcome = session.history.undo(session.objects());

  EXPECT_EQ(undoOutcome.result, HistoryResult::targetMissing);
  EXPECT_EQ(undoOutcome.conflict, sceneUUID);
  EXPECT_FALSE(undoOutcome.jsonPayload.has_value());
  EXPECT_FALSE(session.history.canUndo());
  EXPECT_EQ(scriptsOf(session.scene), listAfterEdit);

  Session redoSession;
  redoSession.edit(replication::buildRemoveSceneScript("Beta"));
  redoSession.applyPayload(redoSession.undo());
  const auto redoOutcome = redoSession.history.redo(redoSession.objects());

  EXPECT_EQ(redoOutcome.result, HistoryResult::targetMissing);
  EXPECT_EQ(redoOutcome.conflict, sceneUUID);
  EXPECT_FALSE(redoSession.history.canRedo());

  // Positive control: the same entry with the scene passed is undone.
  Session control;
  control.edit(replication::buildRemoveSceneScript("Beta"));
  EXPECT_TRUE(control.undo().ok());
}

TEST(SceneScriptUndoTest, UndoAgainstADifferentSceneIsRefused)
{
  Session session;
  session.edit(replication::buildRemoveSceneScript("Beta"));
  const auto other = makeScene(otherSceneUUID);
  other->removeScript("Beta");

  const auto outcome = session.history.undo(session.objects(), nullptr, other.get());

  EXPECT_EQ(outcome.result, HistoryResult::targetMissing);
  EXPECT_EQ(outcome.conflict, sceneUUID);
}

TEST(SceneScriptUndoTest, DerivationYieldsNothingWhenTheEditCannotBeRecordedFaithfully)
{
  const auto scene = makeScene();
  const auto& objects = *scene->getObjectManager();
  const auto derive = [&](const nlohmann::json& edit, const SceneAsset* sceneArg) {
    return edits::commandForSceneEdit(edit, objects, nullptr, sceneArg);
  };

  const std::vector<nlohmann::json> unrecordable = {
    replication::buildRemoveSceneScript("Nope"),
    replication::buildReorderSceneScript("Nope", 0),
    replication::buildSetSceneScriptFields("Nope", gammaFields),
    replication::buildAddSceneScript("Beta"),
    replication::buildReorderSceneScript("Alpha", 0),
    replication::buildReorderSceneScript("Alpha", 3),
    replication::buildSetSceneScriptFields("Alpha", nlohmann::json::object()),
    nlohmann::json{ { "op", "removeSceneScript" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "index", "x" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "fields", 4 } },
    nlohmann::json{ { "op", "reorderSceneScript" }, { "className", "Alpha" } },
    nlohmann::json{ { "op", "setSceneScriptFields" }, { "className", "Alpha" } }
  };

  for (const auto& edit : unrecordable)
  {
    EXPECT_FALSE(derive(edit, scene.get()).has_value()) << edit.dump();
  }

  // Positive control: each op kind derives a command when it is well formed.
  EXPECT_TRUE(derive(replication::buildAddSceneScript("Delta"), scene.get()).has_value());
  EXPECT_TRUE(derive(replication::buildRemoveSceneScript("Beta"), scene.get()).has_value());
  EXPECT_TRUE(derive(replication::buildReorderSceneScript("Alpha", 1), scene.get()).has_value());
  EXPECT_TRUE(derive(replication::buildSetSceneScriptFields("Alpha", gammaFields), scene.get()).has_value());

  // Without a scene nothing can be read, so nothing is recorded.
  EXPECT_FALSE(derive(replication::buildRemoveSceneScript("Beta"), nullptr).has_value());
  EXPECT_FALSE(derive(replication::buildAddSceneScript("Delta"), nullptr).has_value());
}

TEST(SceneScriptUndoTest, DerivedCommandsAreSceneScriptKindsInSceneEditForm)
{
  const auto scene = makeScene();
  const auto& objects = *scene->getObjectManager();

  const std::vector<std::pair<nlohmann::json, edits::CommandKind>> cases = {
    { replication::buildAddSceneScript("Delta"), edits::CommandKind::addSceneScript },
    { replication::buildRemoveSceneScript("Beta"), edits::CommandKind::removeSceneScript },
    { replication::buildReorderSceneScript("Alpha", 1), edits::CommandKind::reorderSceneScript },
    { replication::buildSetSceneScriptFields("Alpha", gammaFields), edits::CommandKind::setSceneScriptFields }
  };

  for (const auto& [edit, kind] : cases)
  {
    const auto command = edits::commandForSceneEdit(edit, objects, nullptr, scene.get());
    ASSERT_TRUE(command.has_value()) << edit.dump();
    EXPECT_EQ(command->kind(), kind);
    EXPECT_EQ(command->payloadForm(), edits::PayloadForm::sceneEdit);
    EXPECT_TRUE(command->isReversible());
    EXPECT_TRUE(command->targetsSceneScripts());
    EXPECT_EQ(command->primaryUUID(), sceneUUID);
  }
}

TEST(SceneScriptUndoTest, DescribeForMenuNamesTheClass)
{
  const auto scene = makeScene();
  const auto& objects = *scene->getObjectManager();

  const auto label = [&](const nlohmann::json& edit) {
    return edits::commandForSceneEdit(edit, objects, nullptr, scene.get())->describeForMenu(objects, nullptr);
  };

  EXPECT_EQ(label(replication::buildAddSceneScript("Delta")), "Add Scene Script Delta");
  EXPECT_EQ(label(replication::buildRemoveSceneScript("Beta")), "Remove Scene Script Beta");
  EXPECT_EQ(label(replication::buildReorderSceneScript("Alpha", 2)), "Reorder Scene Script Alpha");
  EXPECT_EQ(label(replication::buildSetSceneScriptFields("Alpha", gammaFields)), "Edit Scene Script Alpha");
}

TEST(SceneScriptUndoTest, ASceneScriptOpInsideABatchHasNoCommandAndAGroupRefusesOne)
{
  const auto scene = makeScene();
  const auto& objects = *scene->getObjectManager();
  const auto objectUUID = objects.getObjects().front()->getUUID();

  const auto batch = replication::buildBatch({
    replication::buildRenameObject(objectUUID, "Renamed"),
    replication::buildRemoveSceneScript("Beta")
  });
  EXPECT_FALSE(edits::commandsForSceneEdit(batch, objects, nullptr, scene.get()).has_value());

  // Positive control: the same batch without the scene script op derives its commands.
  const auto objectOnly = replication::buildBatch({
    replication::buildRenameObject(objectUUID, "Renamed"),
    replication::buildRenameObject(objectUUID, "Again")
  });
  const auto commands = edits::commandsForSceneEdit(objectOnly, objects, nullptr, scene.get());
  ASSERT_TRUE(commands.has_value());
  EXPECT_EQ(commands->size(), 2u);

  // A non-batch scene script edit passes straight through to a single command.
  const auto single = edits::commandsForSceneEdit(replication::buildRemoveSceneScript("Beta"), objects, nullptr,
                                                  scene.get());
  ASSERT_TRUE(single.has_value());
  EXPECT_EQ(single->size(), 1u);

  edits::EditHistory history;
  const auto removeBeta = *edits::commandForSceneEdit(replication::buildRemoveSceneScript("Beta"), objects,
                                                      nullptr, scene.get());
  const auto removeAlpha = *edits::commandForSceneEdit(replication::buildRemoveSceneScript("Alpha"), objects,
                                                       nullptr, scene.get());
  EXPECT_THROW(history.recordBatch({ removeBeta, removeAlpha }), std::invalid_argument);
  EXPECT_FALSE(history.canUndo());
}

TEST(SceneScriptUndoTest, ANonArrayFieldsBlobIsRecordedAsAnEmptyArray)
{
  Session session;
  session.scene->findScript("Beta")->setFields(nlohmann::json::object());

  const auto setCommand = edits::commandForSceneEdit(replication::buildSetSceneScriptFields("Beta", gammaFields),
                                                     session.objects(), nullptr, session.scene.get());
  ASSERT_TRUE(setCommand.has_value());
  EXPECT_EQ(*setCommand, edits::EditCommand::setSceneScriptFields(sceneUUID, "Beta", nlohmann::json::array(),
                                                                  gammaFields));

  // The undo of a removal must be an op the server accepts, so applying it restores the entry.
  session.edit(replication::buildRemoveSceneScript("Beta"));
  const auto undone = session.undo();
  session.applyPayload(undone);
  ASSERT_TRUE(session.scene->findScript("Beta"));
  EXPECT_EQ(session.scene->findScript("Beta")->getFields(), nlohmann::json::array());
}

TEST(SceneScriptUndoTest, AFieldEditThatChangesNothingRecordsNothing)
{
  Session session;
  const auto& objects = session.objects();

  EXPECT_FALSE(edits::commandForSceneEdit(replication::buildSetSceneScriptFields("Alpha", alphaFields), objects,
                                          nullptr, session.scene.get()).has_value());

  // Positive control: a different blob does record.
  EXPECT_TRUE(edits::commandForSceneEdit(replication::buildSetSceneScriptFields("Alpha", otherFields), objects,
                                         nullptr, session.scene.get()).has_value());
}
