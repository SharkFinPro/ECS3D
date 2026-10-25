#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "objects/components/Transform.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <uuid.h>

namespace {
  const auto mainUUID = uuids::uuid::from_string("11111111-1111-1111-1111-111111111111").value();
  const auto otherUUID = uuids::uuid::from_string("22222222-2222-2222-2222-222222222222").value();
  const auto objectUUID = uuids::uuid::from_string("33333333-3333-3333-3333-333333333333").value();
  const auto modelUUID = uuids::uuid::from_string("44444444-4444-4444-4444-444444444444").value();

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;

    Project()
    {
      registerDataComponents(*componentRegistry);
    }

    // Registers the scene the way applyAddAsset does, so both halves of the pair agree.
    std::shared_ptr<SceneAsset> addScene(const uuids::uuid& uuid, const std::string& name)
    {
      const auto scene = std::make_shared<SceneAsset>(uuid, name, componentRegistry);
      sceneManager.addScene(scene);
      assetRegistry.registerAsset({ .uuid = uuid, .type = AssetType::Scene, .path = name });

      return scene;
    }

    // "Main" is loaded; "Other" is the scene that can be deleted. Other holds one object and one script.
    std::shared_ptr<SceneAsset> addTwoScenes()
    {
      const auto main = addScene(mainUUID, "Main");
      sceneManager.loadScene(main);

      const auto other = addScene(otherUUID, "Other");
      const auto object = std::make_shared<Object>("Crate", objectUUID);
      other->getObjectManager()->addObject(object);
      object->getComponent<Transform>(ComponentType::transform)->setPosition(glm::vec3(4.0f, 5.0f, 6.0f));
      other->addScript(std::make_shared<Script>("Spinner"));

      return other;
    }
  };

  nlohmann::json removeOp(const uuids::uuid& uuid)
  {
    return replication::buildRemoveAsset(uuid);
  }

  bool hasScene(const Project& project, const uuids::uuid& uuid)
  {
    return project.sceneManager.getScene(uuid) != nullptr && project.assetRegistry.getByUUID(uuid) != nullptr;
  }

  bool hasAnyTrace(const Project& project, const uuids::uuid& uuid)
  {
    return project.sceneManager.getScene(uuid) != nullptr || project.assetRegistry.getByUUID(uuid) != nullptr;
  }

  void expectOtherRestored(const Project& project)
  {
    const auto scene = project.sceneManager.getScene(otherUUID);
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(scene->getName(), "Other");

    const auto object = scene->getObjectManager()->getObjectByUUID(objectUUID);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->getName(), "Crate");
    const auto position = object->getComponent<Transform>(ComponentType::transform)->getPosition();
    EXPECT_FLOAT_EQ(position.x, 4.0f);
    EXPECT_FLOAT_EQ(position.y, 5.0f);
    EXPECT_FLOAT_EQ(position.z, 6.0f);

    ASSERT_EQ(scene->getScripts().size(), 1u);
    EXPECT_EQ(scene->getScripts().front()->getClassName(), "Spinner");

    ASSERT_NE(project.assetRegistry.getByUUID(otherUUID), nullptr);
    EXPECT_EQ(project.assetRegistry.getByUUID(otherUUID)->path, "Other");
  }
}

TEST(SceneDelete, RemovingAnInactiveSceneDropsItFromTheManagerAndTheRegistry)
{
  Project project;
  project.addTwoScenes();
  ASSERT_TRUE(hasScene(project, otherUUID));

  EXPECT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));

  EXPECT_FALSE(hasAnyTrace(project, otherUUID));
  EXPECT_TRUE(hasScene(project, mainUUID));
}

TEST(SceneDelete, RemovingTheActiveSceneIsRefusedAndChangesNothing)
{
  Project project;
  project.addTwoScenes();

  EXPECT_FALSE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(mainUUID)));

  EXPECT_TRUE(hasScene(project, mainUUID));
  EXPECT_TRUE(hasScene(project, otherUUID));
}

TEST(SceneDelete, RemovingTheOnlySceneIsRefusedAndChangesNothing)
{
  Project project;
  project.addScene(otherUUID, "Other");
  ASSERT_EQ(project.sceneManager.getCurrentScene(), nullptr);

  EXPECT_FALSE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));
  EXPECT_TRUE(hasScene(project, otherUUID));
}

TEST(SceneDelete, RemovingANonSceneAssetStillDropsJustTheRecord)
{
  Project project;
  project.addTwoScenes();
  project.assetRegistry.registerAsset({ .uuid = modelUUID, .type = AssetType::Model, .path = "models/a.obj" });

  EXPECT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(modelUUID)));
  EXPECT_EQ(project.assetRegistry.getByUUID(modelUUID), nullptr);
  EXPECT_EQ(project.sceneManager.getScenes().size(), 2u);

  EXPECT_FALSE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(modelUUID)));
}

TEST(SceneDelete, ADeletedSceneDoesNotComeBackInTheNextSnapshot)
{
  Project source;
  source.addTwoScenes();
  const ProjectPacker packer(&source.assetRegistry, &source.sceneManager, source.componentRegistry);

  net::Message before(net::MessageType::snapshot);
  packer.pack(before);

  Project controlView;
  ProjectPacker(&controlView.assetRegistry, &controlView.sceneManager, controlView.componentRegistry).unpack(before);
  EXPECT_TRUE(hasScene(controlView, otherUUID));

  ASSERT_TRUE(replication::applyRemoveAsset(source.assetRegistry, source.sceneManager, removeOp(otherUUID)));

  net::Message after(net::MessageType::snapshot);
  packer.pack(after);

  Project view;
  ProjectPacker(&view.assetRegistry, &view.sceneManager, view.componentRegistry).unpack(after);

  EXPECT_FALSE(hasAnyTrace(view, otherUUID));
  EXPECT_TRUE(hasScene(view, mainUUID));
}

TEST(SceneDelete, AddingASceneWithABodyRestoresItsObjectsAndScripts)
{
  Project source;
  const auto other = source.addTwoScenes();

  Project target;
  target.addScene(mainUUID, "Main");

  const nlohmann::json asset = {
    { "assetType", "scene" },
    { "uuid", uuids::to_string(otherUUID) },
    { "name", "Other" },
    { "body", other->serialize().dump() }
  };
  replication::applyAddAsset(target.assetRegistry, target.sceneManager, target.componentRegistry, asset);

  expectOtherRestored(target);
}

TEST(SceneDelete, AddingASceneWithoutABodyStillCreatesAnEmptyScene)
{
  Project target;

  const nlohmann::json asset = {
    { "assetType", "scene" },
    { "uuid", uuids::to_string(otherUUID) },
    { "name", "Other" }
  };
  replication::applyAddAsset(target.assetRegistry, target.sceneManager, target.componentRegistry, asset);

  ASSERT_TRUE(hasScene(target, otherUUID));
  EXPECT_TRUE(target.sceneManager.getScene(otherUUID)->getObjectManager()->getObjects().empty());
  EXPECT_TRUE(target.sceneManager.getScene(otherUUID)->getScripts().empty());
}

TEST(SceneDelete, AddingASceneWithAMalformedBodyAddsNothing)
{
  Project source;
  const auto other = source.addTwoScenes();

  const auto addWithBody = [](const std::string& body) {
    Project target;
    const nlohmann::json asset = {
      { "assetType", "scene" },
      { "uuid", uuids::to_string(otherUUID) },
      { "name", "Other" },
      { "body", body }
    };
    replication::applyAddAsset(target.assetRegistry, target.sceneManager, target.componentRegistry, asset);

    return hasAnyTrace(target, otherUUID);
  };

  EXPECT_FALSE(addWithBody("{not json"));
  EXPECT_FALSE(addWithBody("[1, 2]"));
  EXPECT_FALSE(addWithBody(R"({"name":"Other"})"));
  EXPECT_FALSE(addWithBody(R"({"objects":[{"unexpected":true}]})"));

  EXPECT_TRUE(addWithBody(other->serialize().dump()));
}

TEST(SceneDelete, AddingASceneUnderAnExistingSceneUuidIsRefused)
{
  Project project;
  project.addTwoScenes();

  const nlohmann::json asset = {
    { "assetType", "scene" },
    { "uuid", uuids::to_string(otherUUID) },
    { "name", "Replacement" }
  };
  replication::applyAddAsset(project.assetRegistry, project.sceneManager, project.componentRegistry, asset);

  EXPECT_EQ(project.sceneManager.getScene(otherUUID)->getName(), "Other");
  EXPECT_EQ(project.sceneManager.getScene(otherUUID)->getObjectManager()->getObjects().size(), 1u);

  const nlohmann::json fresh = {
    { "assetType", "scene" },
    { "uuid", uuids::to_string(modelUUID) },
    { "name", "Replacement" }
  };
  replication::applyAddAsset(project.assetRegistry, project.sceneManager, project.componentRegistry, fresh);
  EXPECT_NE(project.sceneManager.getScene(modelUUID), nullptr);
}

TEST(SceneDelete, UndoOfASceneRemovalRestoresTheSceneAndRedoRemovesItAgain)
{
  Project project;
  project.addTwoScenes();
  const auto current = project.sceneManager.getCurrentScene();

  const auto command = edits::commandForRemoveAsset(removeOp(otherUUID), project.assetRegistry,
                                                    project.sceneManager);
  ASSERT_TRUE(command.has_value());

  edits::EditHistory history;
  history.record(*command);
  ASSERT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));
  ASSERT_FALSE(hasAnyTrace(project, otherUUID));

  const auto undo = history.undo(*current->getObjectManager(), &project.assetRegistry, current.get());
  ASSERT_TRUE(undo.ok());
  ASSERT_TRUE(undo.messagePayload.has_value());
  replication::applyAddAsset(project.assetRegistry, project.sceneManager, project.componentRegistry,
                             replication::unpackAddAsset(*undo.messagePayload));
  expectOtherRestored(project);

  const auto redo = history.redo(*current->getObjectManager(), &project.assetRegistry, current.get());
  ASSERT_TRUE(redo.ok());
  ASSERT_TRUE(redo.messagePayload.has_value());
  EXPECT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager,
                                            replication::unpackRemoveAsset(*redo.messagePayload)));
  EXPECT_FALSE(hasAnyTrace(project, otherUUID));
}

TEST(SceneDelete, ARemoveCommandForASceneNeedsTheSceneInTheManager)
{
  Project project;
  project.addTwoScenes();
  project.assetRegistry.registerAsset({ .uuid = modelUUID, .type = AssetType::Scene, .path = "Ghost" });

  EXPECT_FALSE(edits::commandForRemoveAsset(removeOp(modelUUID), project.assetRegistry,
                                            project.sceneManager).has_value());
  EXPECT_TRUE(edits::commandForRemoveAsset(removeOp(otherUUID), project.assetRegistry,
                                           project.sceneManager).has_value());
}

TEST(SceneDelete, UndoOfNewSceneRemovesTheSceneWhileItIsNotActive)
{
  Project project;
  project.addTwoScenes();
  const auto current = project.sceneManager.getCurrentScene();

  edits::EditHistory history;
  history.record(edits::EditCommand::addAsset(otherUUID, AssetType::Scene, "Other", "", ""));

  const auto undo = history.undo(*current->getObjectManager(), &project.assetRegistry, current.get());
  ASSERT_TRUE(undo.ok());
  ASSERT_TRUE(undo.messagePayload.has_value());
  EXPECT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager,
                                            replication::unpackRemoveAsset(*undo.messagePayload)));

  EXPECT_FALSE(hasAnyTrace(project, otherUUID));
}

TEST(SceneDelete, UndoOfNewSceneIsRefusedWhileThatSceneIsActive)
{
  Project project;
  project.addTwoScenes();
  const auto other = project.sceneManager.getScene(otherUUID);
  project.sceneManager.loadScene(other);

  edits::EditHistory history;
  history.record(edits::EditCommand::addAsset(otherUUID, AssetType::Scene, "Other", "", ""));

  const auto undo = history.undo(*other->getObjectManager(), &project.assetRegistry, other.get());
  EXPECT_EQ(undo.result, edits::HistoryResult::targetActive);
  EXPECT_FALSE(undo.messagePayload.has_value());
  ASSERT_TRUE(undo.conflict.has_value());
  EXPECT_EQ(*undo.conflict, otherUUID);
}

TEST(SceneDelete, RedoOfASceneRemovalIsRefusedWhileThatSceneIsActive)
{
  Project project;
  project.addTwoScenes();

  const auto command = edits::commandForRemoveAsset(removeOp(otherUUID), project.assetRegistry,
                                                    project.sceneManager);
  ASSERT_TRUE(command.has_value());

  edits::EditHistory history;
  history.record(*command);
  ASSERT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));

  const auto main = project.sceneManager.getCurrentScene();
  const auto undo = history.undo(*main->getObjectManager(), &project.assetRegistry, main.get());
  ASSERT_TRUE(undo.ok());
  replication::applyAddAsset(project.assetRegistry, project.sceneManager, project.componentRegistry,
                             replication::unpackAddAsset(*undo.messagePayload));

  const auto restored = project.sceneManager.getScene(otherUUID);
  ASSERT_NE(restored, nullptr);
  project.sceneManager.loadScene(restored);

  const auto redo = history.redo(*restored->getObjectManager(), &project.assetRegistry, restored.get());
  EXPECT_EQ(redo.result, edits::HistoryResult::targetActive);
  EXPECT_FALSE(redo.messagePayload.has_value());
}

TEST(SceneDelete, RemovingASceneIsRefusedWhileTheSceneIsRunningAndWorksAfterStop)
{
  Project project;
  project.addTwoScenes();
  project.sceneManager.startScene();

  EXPECT_FALSE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));
  EXPECT_TRUE(hasScene(project, otherUUID));

  project.sceneManager.resetScene();
  EXPECT_TRUE(replication::applyRemoveAsset(project.assetRegistry, project.sceneManager, removeOp(otherUUID)));
  EXPECT_FALSE(hasAnyTrace(project, otherUUID));
}
