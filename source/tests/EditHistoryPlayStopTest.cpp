#include <gtest/gtest.h>

#include "TestScene.h"
#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"

#include <memory>
#include <utility>
#include <uuid.h>

// The editor stashes the authored history when play starts and restores it when play stops (see
// EditorApp::handleSceneStatus). That lives in EditorApp, which this suite cannot link, so these pin the
// contract the restore relies on: SceneAsset::stop() rebuilds the authored tree under the same uuids, so a
// command recorded before start still validates, undoes and redoes afterwards.

namespace {
  struct PlayScene {
    std::shared_ptr<SceneAsset> scene;
    uuids::uuid objectUUID;

    [[nodiscard]] ObjectManager& objects() const
    {
      return *scene->getObjectManager();
    }

    [[nodiscard]] std::shared_ptr<Object> object() const
    {
      return objects().getObjectByUUID(objectUUID);
    }
  };

  PlayScene makePlayScene()
  {
    const auto componentRegistry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*componentRegistry);

    PlayScene play;
    play.scene = std::make_shared<SceneAsset>(
      uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value(), "Main", componentRegistry);

    const auto object = std::make_shared<Object>("Object");
    play.scene->getObjectManager()->addObject(object);
    play.objectUUID = object->getUUID();

    return play;
  }

  // Runtime changes that stop() must discard: a spawn, a destroy and a rename of the authored object.
  void mutateAtRuntime(const PlayScene& play)
  {
    const auto spawned = std::make_shared<Object>("Spawned");
    play.objects().addObject(spawned);

    play.object()->setName("Hacked");
    play.objects().removeObject(play.object());
    play.objects().deleteObjectsMarkedForDeletion();
  }
}

TEST(EditHistoryPlayStop, ComponentEditRecordedBeforePlayUndoesAndRedoesAfterStop)
{
  const auto play = makePlayScene();
  const auto transform = play.object()->getComponent<Transform>(ComponentType::transform);
  ASSERT_NE(transform, nullptr);

  transform->setPosition({ 1, 2, 3 });
  const auto before = transform->serialize();
  transform->setPosition({ 4, 5, 6 });
  const auto after = transform->serialize();

  edits::EditHistory authored;
  authored.record(edits::EditCommand::componentEdit(play.objectUUID, before, after));

  play.scene->start();
  transform->setPosition({ 9, 9, 9 });
  mutateAtRuntime(play);
  play.scene->stop();

  ASSERT_NE(play.object(), nullptr);

  const auto undoOutcome = authored.undo(play.objects());
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());
  EXPECT_EQ(replication::applyComponentEdit(play.objects(), *undoOutcome.messagePayload),
            replication::ComponentEditResult::applied);
  fixtures::expectNear(fixtures::transformOf(play.object())->getLocalPosition(), { 1, 2, 3 });

  const auto redoOutcome = authored.redo(play.objects());
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  EXPECT_EQ(replication::applyComponentEdit(play.objects(), *redoOutcome.messagePayload),
            replication::ComponentEditResult::applied);
  fixtures::expectNear(fixtures::transformOf(play.object())->getLocalPosition(), { 4, 5, 6 });
}

TEST(EditHistoryPlayStop, RenameRecordedBeforePlayUndoesAndRedoesAfterStop)
{
  const auto play = makePlayScene();
  play.object()->setName("Renamed");

  edits::EditHistory authored;
  authored.record(edits::EditCommand::renameObject(play.objectUUID, "Object", "Renamed"));

  play.scene->start();
  mutateAtRuntime(play);
  play.scene->stop();

  ASSERT_NE(play.object(), nullptr);
  EXPECT_EQ(play.object()->getName(), "Renamed");

  const auto undoOutcome = authored.undo(play.objects());
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(play.objects(), *undoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(play.object()->getName(), "Object");

  const auto redoOutcome = authored.redo(play.objects());
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.jsonPayload.has_value());
  EXPECT_EQ(replication::applySceneEdit(play.objects(), *redoOutcome.jsonPayload),
            replication::SceneEditResult::applied);
  EXPECT_EQ(play.object()->getName(), "Renamed");
}

// The two refusal tests never stop the scene: they show validation is sensitive to the live scene, which
// is why the tests above pass only because stop() restored it.
TEST(EditHistoryPlayStop, UndoIsRefusedWhileTheRunHasChangedWhatTheCommandNames)
{
  const auto play = makePlayScene();
  play.object()->setName("Renamed");

  edits::EditHistory authored;
  authored.record(edits::EditCommand::renameObject(play.objectUUID, "Object", "Renamed"));

  play.scene->start();
  play.object()->setName("Hacked");

  const auto outcome = authored.undo(play.objects());
  EXPECT_FALSE(outcome.ok());
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, play.objectUUID);
}

TEST(EditHistoryPlayStop, UndoIsRefusedWhileTheRunHasDestroyedTheTarget)
{
  const auto play = makePlayScene();
  play.object()->setName("Renamed");

  edits::EditHistory authored;
  authored.record(edits::EditCommand::renameObject(play.objectUUID, "Object", "Renamed"));

  play.scene->start();
  play.objects().removeObject(play.object());
  play.objects().deleteObjectsMarkedForDeletion();

  const auto outcome = authored.undo(play.objects());
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetMissing);
}
