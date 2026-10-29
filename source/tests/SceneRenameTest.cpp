#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <Protocol.h>
#include <memory>
#include <string>
#include <uuid.h>

namespace {
  const auto mainUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto otherUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  const auto prefabUUID = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();
  const auto modelUUID = uuids::uuid::from_string("44444444-4444-4444-4444-444444444444").value();
  const auto ghostUUID = uuids::uuid::from_string("55555555-5555-5555-5555-555555555555").value();
  const auto realUUID = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    std::unique_ptr<ProjectPacker> packer;

    Project()
    {
      registerDataComponents(*componentRegistry);
      packer = std::make_unique<ProjectPacker>(&assetRegistry, &sceneManager, componentRegistry);
    }

    void addScene(const uuids::uuid& uuid, const std::string& name)
    {
      sceneManager.addScene(std::make_shared<SceneAsset>(uuid, name, componentRegistry));
      assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Scene, .path = name });
    }
  };

  void populate(Project& project)
  {
    project.addScene(mainUUID, "Main");
    project.addScene(otherUUID, "Other");
    project.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                          .body = R"({"name":"Block"})" });
    project.assetRegistry.registerAsset({ .uuid = modelUUID, .type = AssetType::Model,
                                          .path = "models/thing.obj" });
  }

  bool rename(Project& project, const uuids::uuid& uuid, const std::string& name)
  {
    return replication::applyRenameAsset(project.assetRegistry, project.sceneManager,
                                         replication::buildRenameAsset(uuid, name));
  }

  std::string sceneName(const Project& project, const uuids::uuid& uuid)
  {
    return project.sceneManager.getScene(uuid)->getName();
  }

  void expectUntouched(const Project& project)
  {
    EXPECT_EQ(sceneName(project, mainUUID), "Main");
    EXPECT_EQ(sceneName(project, otherUUID), "Other");
    ASSERT_NE(project.assetRegistry.getByUUID(mainUUID), nullptr);
    EXPECT_EQ(project.assetRegistry.getByUUID(mainUUID)->path, "Main");
    ASSERT_NE(project.assetRegistry.getByPath("Main"), nullptr);
    EXPECT_EQ(project.assetRegistry.getByPath("Main")->uuid, mainUUID);
    ASSERT_NE(project.assetRegistry.getByPath("Block"), nullptr);
    EXPECT_EQ(project.assetRegistry.getByPath("Block")->uuid, prefabUUID);
  }
}

TEST(SceneRename, RenamesTheSceneAndRekeysItsRegistryRecord)
{
  Project project;
  populate(project);
  const auto versionBefore = project.assetRegistry.getVersion();

  EXPECT_TRUE(rename(project, mainUUID, "Level One"));

  EXPECT_EQ(sceneName(project, mainUUID), "Level One");
  const auto* record = project.assetRegistry.getByUUID(mainUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->type, AssetType::Scene);
  EXPECT_EQ(record->path, "Level One");
  EXPECT_TRUE(record->displayName.empty());

  const auto* found = project.assetRegistry.getByPath("Level One");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->uuid, mainUUID);
  EXPECT_EQ(project.assetRegistry.getByPath("Main"), nullptr);
  EXPECT_GT(project.assetRegistry.getVersion(), versionBefore);
}

TEST(SceneRename, RenamingToTheCurrentNameSucceedsWithoutChange)
{
  Project project;
  populate(project);

  EXPECT_TRUE(rename(project, mainUUID, "Main"));

  expectUntouched(project);
}

TEST(SceneRename, ANameAnotherSceneOwnsIsRefused)
{
  Project project;
  populate(project);

  EXPECT_FALSE(rename(project, mainUUID, "Other"));
  expectUntouched(project);

  EXPECT_TRUE(rename(project, mainUUID, "Free"));
}

TEST(SceneRename, ANameAPrefabOwnsIsRefused)
{
  Project project;
  populate(project);

  EXPECT_FALSE(rename(project, mainUUID, "Block"));
  expectUntouched(project);

  EXPECT_TRUE(rename(project, mainUUID, "Blocks"));
}

TEST(SceneRename, AnEmptyOrBlankNameIsRefused)
{
  Project project;
  populate(project);

  EXPECT_FALSE(rename(project, mainUUID, ""));
  EXPECT_FALSE(rename(project, mainUUID, "   "));
  EXPECT_FALSE(rename(project, mainUUID, " \t"));
  expectUntouched(project);

  EXPECT_TRUE(rename(project, mainUUID, "Named"));
}

TEST(SceneRename, ASceneRecordWithNoSceneAssetIsRefused)
{
  Project project;
  populate(project);
  project.assetRegistry.registerAsset({ .uuid = ghostUUID, .type = AssetType::Scene, .path = "Ghost" });

  EXPECT_FALSE(rename(project, ghostUUID, "Haunted"));

  EXPECT_EQ(project.assetRegistry.getByUUID(ghostUUID)->path, "Ghost");
  EXPECT_NE(project.assetRegistry.getByPath("Ghost"), nullptr);
  EXPECT_EQ(project.assetRegistry.getByPath("Haunted"), nullptr);

  project.addScene(realUUID, "Real");
  EXPECT_TRUE(rename(project, realUUID, "Moved"));
}

TEST(SceneRename, ANonSceneRenameStillSetsOnlyTheDisplayName)
{
  Project project;
  populate(project);

  EXPECT_TRUE(rename(project, modelUUID, "Nicer Thing"));

  const auto* record = project.assetRegistry.getByUUID(modelUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->displayName, "Nicer Thing");
  EXPECT_EQ(record->path, "models/thing.obj");
  EXPECT_EQ(project.assetRegistry.getByPath("models/thing.obj")->uuid, modelUUID);
  expectUntouched(project);

  EXPECT_TRUE(rename(project, prefabUUID, "Brick"));
  EXPECT_EQ(project.assetRegistry.getByUUID(prefabUUID)->displayName, "Brick");
  EXPECT_EQ(project.assetRegistry.getByUUID(prefabUUID)->path, "Block");
}

TEST(SceneRename, TheNewNameSurvivesASnapshotRoundTrip)
{
  Project server;
  populate(server);
  ASSERT_TRUE(rename(server, mainUUID, "Level One"));

  net::Message message(net::MessageType::snapshot);
  server.packer->pack(message);

  Project client;
  client.packer->unpack(message);

  ASSERT_NE(client.sceneManager.getScene(mainUUID), nullptr);
  EXPECT_EQ(sceneName(client, mainUUID), "Level One");

  const auto* record = client.assetRegistry.getByUUID(mainUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->path, "Level One");
  EXPECT_TRUE(record->displayName.empty());
  ASSERT_NE(client.assetRegistry.getByPath("Level One"), nullptr);
  EXPECT_EQ(client.assetRegistry.getByPath("Level One")->uuid, mainUUID);
  EXPECT_EQ(client.assetRegistry.getByPath("Main"), nullptr);
  EXPECT_EQ(sceneName(client, otherUUID), "Other");
}

TEST(SceneRename, ASceneSharingAPrefabNameComesBackRenamedFromASnapshot)
{
  Project server;
  server.assetRegistry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                                       .body = R"({"name":"Block"})" });
  server.addScene(mainUUID, "Main");
  server.sceneManager.addScene(std::make_shared<SceneAsset>(otherUUID, "Block", server.componentRegistry));

  net::Message message(net::MessageType::snapshot);
  server.packer->pack(message);

  Project client;
  client.packer->unpack(message);

  ASSERT_NE(client.sceneManager.getScene(otherUUID), nullptr);
  EXPECT_EQ(sceneName(client, otherUUID), "Block (2)");

  const auto* record = client.assetRegistry.getByUUID(otherUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->type, AssetType::Scene);
  EXPECT_EQ(record->path, "Block (2)");

  const auto* prefab = client.assetRegistry.getByPath("Block");
  ASSERT_NE(prefab, nullptr);
  EXPECT_EQ(prefab->uuid, prefabUUID);
  EXPECT_EQ(prefab->type, AssetType::Prefab);
  EXPECT_EQ(sceneName(client, mainUUID), "Main");
}

TEST(SceneRename, UndoRecordsTheOldNameAndRestoresIt)
{
  Project project;
  populate(project);
  const auto op = replication::buildRenameAsset(mainUUID, "Level One");

  const auto command = edits::commandForRenameAsset(op, project.assetRegistry);
  ASSERT_TRUE(command.has_value());
  EXPECT_EQ(*command, edits::EditCommand::renameAsset(mainUUID, "Main", "Level One"));

  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager, op));

  edits::EditHistory history;
  history.record(*command);
  auto& objects = *project.sceneManager.getScene(mainUUID)->getObjectManager();

  const auto undoOutcome = history.undo(objects, &project.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(undoOutcome.messagePayload.has_value());

  const auto reverse = replication::unpackRenameAsset(*undoOutcome.messagePayload);
  EXPECT_EQ(reverse.at("displayName"), "Main");
  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager, reverse));
  EXPECT_EQ(sceneName(project, mainUUID), "Main");
  EXPECT_EQ(project.assetRegistry.getByUUID(mainUUID)->path, "Main");

  const auto redoOutcome = history.redo(objects, &project.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(redoOutcome.messagePayload.has_value());
  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager,
                                            replication::unpackRenameAsset(*redoOutcome.messagePayload)));
  EXPECT_EQ(sceneName(project, mainUUID), "Level One");
}

TEST(SceneRename, UndoIsRefusedWhenSomeoneRenamedTheSceneAgain)
{
  Project project;
  populate(project);
  const auto op = replication::buildRenameAsset(mainUUID, "Level One");

  const auto command = edits::commandForRenameAsset(op, project.assetRegistry);
  ASSERT_TRUE(command.has_value());
  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager, op));

  edits::EditHistory history;
  history.record(*command);
  auto& objects = *project.sceneManager.getScene(mainUUID)->getObjectManager();

  // Positive control: with the scene on the recorded name, undo and redo are allowed.
  const auto undoOutcome = history.undo(objects, &project.assetRegistry);
  ASSERT_TRUE(undoOutcome.ok());
  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager,
                                            replication::unpackRenameAsset(*undoOutcome.messagePayload)));
  const auto redoOutcome = history.redo(objects, &project.assetRegistry);
  ASSERT_TRUE(redoOutcome.ok());
  ASSERT_TRUE(replication::applyRenameAsset(project.assetRegistry, project.sceneManager,
                                            replication::unpackRenameAsset(*redoOutcome.messagePayload)));

  ASSERT_TRUE(rename(project, mainUUID, "Third"));

  const auto outcome = history.undo(objects, &project.assetRegistry);
  EXPECT_EQ(outcome.result, edits::HistoryResult::targetChanged);
  ASSERT_TRUE(outcome.conflict.has_value());
  EXPECT_EQ(*outcome.conflict, mainUUID);
  EXPECT_FALSE(outcome.messagePayload.has_value());
}

TEST(SceneRename, ANameMatchingAnyRegistryPathIsUnavailableToTheUIAndRefusedByTheServer)
{
  Project project;
  populate(project);

  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "models/thing.obj"));
  EXPECT_FALSE(rename(project, mainUUID, "models/thing.obj"));
  expectUntouched(project);

  // Positive control: a free name passes both, and so does the scene's own name.
  EXPECT_TRUE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "models/other.obj"));
  EXPECT_TRUE(rename(project, mainUUID, "models/other.obj"));
  EXPECT_TRUE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "models/other.obj"));
}

TEST(SceneRename, TheAvailabilityRuleMatchesWhatTheServerAccepts)
{
  Project project;
  populate(project);

  EXPECT_TRUE(replication::isSceneNameAvailable(project.assetRegistry, &project.sceneManager, mainUUID, "Main"));
  EXPECT_TRUE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "Free"));
  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "Other"));
  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "Block"));
  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, ""));
  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "  "));

  // A scene that has no registry record is still caught through the SceneManager.
  project.sceneManager.addScene(std::make_shared<SceneAsset>(realUUID, "Unlisted", project.componentRegistry));
  EXPECT_TRUE(replication::isSceneNameAvailable(project.assetRegistry, nullptr, mainUUID, "Unlisted"));
  EXPECT_FALSE(replication::isSceneNameAvailable(project.assetRegistry, &project.sceneManager, mainUUID,
                                                 "Unlisted"));
  EXPECT_FALSE(rename(project, mainUUID, "Unlisted"));
}

TEST(SceneRename, AnUnknownUUIDIsRefused)
{
  Project project;
  populate(project);

  EXPECT_FALSE(rename(project, ghostUUID, "Anything"));
  EXPECT_EQ(project.assetRegistry.getByUUID(ghostUUID), nullptr);

  // Positive control: a known asset's rename still applies.
  EXPECT_TRUE(rename(project, modelUUID, "Anything"));
}
