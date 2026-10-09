#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "ProjectSerializer.h"
#include "assets/AssetRegistry.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {
  const auto sceneUUID = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();

  using Names = std::vector<std::string>;

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    std::unique_ptr<AssetRegistry> assetRegistry = std::make_unique<AssetRegistry>();
    std::unique_ptr<SceneManager> sceneManager = std::make_unique<SceneManager>();
    std::unique_ptr<ProjectSerializer> serializer;
    std::unique_ptr<ProjectPacker> packer;
  };

  Project makeProject()
  {
    Project project;
    registerDataComponents(*project.componentRegistry);

    project.serializer = std::make_unique<ProjectSerializer>(project.assetRegistry.get(),
                                                             project.sceneManager.get(),
                                                             project.componentRegistry);
    project.packer = std::make_unique<ProjectPacker>(project.assetRegistry.get(),
                                                     project.sceneManager.get(),
                                                     project.componentRegistry);

    return project;
  }

  std::shared_ptr<SceneAsset> makeBareScene()
  {
    return std::make_shared<SceneAsset>(sceneUUID, "Main", std::make_shared<ComponentRegistry>());
  }

  std::shared_ptr<Script> makeEntry(const std::string& className,
                                    const nlohmann::json& fields = nlohmann::json::array())
  {
    auto script = std::make_shared<Script>(className);
    script->setFields(fields);

    return script;
  }

  Names classNames(const std::shared_ptr<SceneAsset>& scene)
  {
    Names names;

    for (const auto& script : scene->getScripts())
    {
      names.push_back(script->getClassName());
    }

    return names;
  }

  // A project with one scene holding an object (so a load that keeps the objects is observable) and the
  // scene scripts Alpha (with fields), Beta and Gamma (with fields), in that order.
  std::shared_ptr<SceneAsset> buildProject(const Project& project)
  {
    const auto scene = std::make_shared<SceneAsset>(sceneUUID, "Main", project.componentRegistry);
    scene->getObjectManager()->addObject(std::make_shared<Object>("Thing"));

    EXPECT_TRUE(scene->addScript(makeEntry("Alpha", nlohmann::json::parse(R"([{"name":"speed","value":3}])"))));
    EXPECT_TRUE(scene->addScript(makeEntry("Beta")));
    EXPECT_TRUE(scene->addScript(makeEntry("Gamma", nlohmann::json::parse(R"([{"name":"on","value":true}])"))));

    project.sceneManager->addScene(scene);
    project.sceneManager->loadScene(scene);

    return scene;
  }

  std::shared_ptr<SceneAsset> onlyScene(const Project& project)
  {
    const auto& scenes = project.sceneManager->getScenes();
    EXPECT_EQ(scenes.size(), 1u);

    return scenes.begin()->second;
  }

  void expectSameScripts(const std::shared_ptr<SceneAsset>& actual, const std::shared_ptr<SceneAsset>& expected)
  {
    ASSERT_EQ(actual->getScripts().size(), expected->getScripts().size());

    for (std::size_t i = 0; i < expected->getScripts().size(); ++i)
    {
      EXPECT_EQ(actual->getScripts()[i]->getClassName(), expected->getScripts()[i]->getClassName());
      EXPECT_EQ(actual->getScripts()[i]->getFields(), expected->getScripts()[i]->getFields());
    }
  }
}

TEST(SceneScriptList, StartsEmpty)
{
  const auto scene = makeBareScene();

  EXPECT_TRUE(scene->getScripts().empty());
  EXPECT_EQ(scene->findScript("Alpha"), nullptr);
}

TEST(SceneScriptList, AddAppendsAndFindsByClassName)
{
  const auto scene = makeBareScene();
  const auto alpha = makeEntry("Alpha");

  EXPECT_TRUE(scene->addScript(alpha));
  EXPECT_TRUE(scene->addScript(makeEntry("Beta")));

  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta" }));
  EXPECT_EQ(scene->findScript("Alpha"), alpha);
  EXPECT_EQ(scene->findScript("Missing"), nullptr);
}

TEST(SceneScriptList, AddRefusesNullEmptyAndDuplicateClassesAndLeavesTheListAlone)
{
  const auto scene = makeBareScene();
  const auto alpha = makeEntry("Alpha");
  ASSERT_TRUE(scene->addScript(alpha));

  EXPECT_FALSE(scene->addScript(nullptr));
  EXPECT_FALSE(scene->addScript(makeEntry("")));
  EXPECT_FALSE(scene->addScript(makeEntry("Alpha")));

  EXPECT_EQ(classNames(scene), (Names{ "Alpha" }));
  EXPECT_EQ(scene->findScript("Alpha"), alpha);

  EXPECT_TRUE(scene->addScript(makeEntry("Beta")));
  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta" }));
}

TEST(SceneScriptList, AddAtAnIndexInsertsThereAndAnIndexPastTheEndAppends)
{
  const auto scene = makeBareScene();
  ASSERT_TRUE(scene->addScript(makeEntry("Alpha")));
  ASSERT_TRUE(scene->addScript(makeEntry("Beta")));

  EXPECT_TRUE(scene->addScript(makeEntry("Front"), 0));
  EXPECT_EQ(classNames(scene), (Names{ "Front", "Alpha", "Beta" }));

  EXPECT_TRUE(scene->addScript(makeEntry("Middle"), 2));
  EXPECT_EQ(classNames(scene), (Names{ "Front", "Alpha", "Middle", "Beta" }));

  EXPECT_TRUE(scene->addScript(makeEntry("Last"), 99));
  EXPECT_EQ(classNames(scene), (Names{ "Front", "Alpha", "Middle", "Beta", "Last" }));
}

TEST(SceneScriptList, RemoveDropsTheNamedClassOnly)
{
  const auto scene = makeBareScene();
  ASSERT_TRUE(scene->addScript(makeEntry("Alpha")));
  ASSERT_TRUE(scene->addScript(makeEntry("Beta")));

  EXPECT_FALSE(scene->removeScript("Missing"));
  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta" }));

  EXPECT_TRUE(scene->removeScript("Alpha"));
  EXPECT_EQ(classNames(scene), (Names{ "Beta" }));
  EXPECT_FALSE(scene->removeScript("Alpha"));
}

TEST(SceneScriptList, MoveReadsTheIndexAgainstTheListWithTheScriptRemoved)
{
  const auto scene = makeBareScene();
  for (const auto* name : { "A", "B", "C" })
  {
    ASSERT_TRUE(scene->addScript(makeEntry(name)));
  }

  EXPECT_TRUE(scene->moveScript("A", 2));
  EXPECT_EQ(classNames(scene), (Names{ "B", "C", "A" }));

  EXPECT_TRUE(scene->moveScript("A", 0));
  EXPECT_EQ(classNames(scene), (Names{ "A", "B", "C" }));

  EXPECT_TRUE(scene->moveScript("A", 1));
  EXPECT_EQ(classNames(scene), (Names{ "B", "A", "C" }));
}

TEST(SceneScriptList, MoveRefusesAnUnknownClassAnIndexPastTheEndAndANoOp)
{
  const auto scene = makeBareScene();
  for (const auto* name : { "A", "B", "C" })
  {
    ASSERT_TRUE(scene->addScript(makeEntry(name)));
  }

  EXPECT_FALSE(scene->moveScript("Missing", 0));
  EXPECT_FALSE(scene->moveScript("A", 3));
  EXPECT_FALSE(scene->moveScript("A", 0));
  EXPECT_FALSE(scene->moveScript("C", 2));
  EXPECT_EQ(classNames(scene), (Names{ "A", "B", "C" }));

  EXPECT_TRUE(scene->moveScript("C", 0));
  EXPECT_EQ(classNames(scene), (Names{ "C", "A", "B" }));
}

TEST(SceneScriptList, JsonRoundTripKeepsOrderClassNamesAndFields)
{
  const auto original = makeProject();
  const auto originalScene = buildProject(original);

  const auto blob = original.serializer->serialize();

  const auto rebuilt = makeProject();
  rebuilt.serializer->deserialize(blob);

  const auto scene = onlyScene(rebuilt);
  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta", "Gamma" }));
  expectSameScripts(scene, originalScene);
  EXPECT_EQ(scene->getObjectManager()->getAllObjects().size(), 1u);
}

TEST(SceneScriptList, AnEmptyListSerializesAsAnEmptyArray)
{
  const auto project = makeProject();
  project.sceneManager->addScene(std::make_shared<SceneAsset>(sceneUUID, "Main", project.componentRegistry));

  const auto blob = project.serializer->serialize();
  const auto& sceneData = blob.at("assets").at("scenes").at(0);

  ASSERT_TRUE(sceneData.contains("scripts"));
  EXPECT_TRUE(sceneData.at("scripts").is_array());
  EXPECT_TRUE(sceneData.at("scripts").empty());
}

TEST(SceneScriptList, ASceneFileWithoutScriptsLoadsEmpty)
{
  const auto original = makeProject();
  buildProject(original);

  auto blob = original.serializer->serialize();
  ASSERT_EQ(blob.at("assets").at("scenes").at(0).at("scripts").size(), 3u);
  blob["assets"]["scenes"][0].erase("scripts");

  const auto rebuilt = makeProject();
  rebuilt.serializer->deserialize(blob);

  const auto scene = onlyScene(rebuilt);
  EXPECT_TRUE(scene->getScripts().empty());
  EXPECT_EQ(scene->getObjectManager()->getAllObjects().size(), 1u);
}

TEST(SceneScriptList, MalformedAndRepeatedEntriesAreSkippedWhileValidOnesAndObjectsLoad)
{
  const auto original = makeProject();
  buildProject(original);

  auto blob = original.serializer->serialize();
  blob["assets"]["scenes"][0]["scripts"] = nlohmann::json::array({
    nlohmann::json::parse(R"({"type":"Script","className":"Alpha","fields":[{"name":"first","value":1}]})"),
    "not an object",
    nlohmann::json::parse(R"({"type":"Script","fields":[]})"),
    nlohmann::json::parse(R"({"type":"Script","className":"","fields":[]})"),
    nlohmann::json::parse(R"({"type":"Script","className":7,"fields":[]})"),
    nlohmann::json::parse(R"({"type":"Script","className":"Alpha","fields":[{"name":"second","value":2}]})"),
    nlohmann::json::parse(R"({"type":"Script","className":"Beta","fields":[]})")
  });

  const auto rebuilt = makeProject();
  ASSERT_NO_THROW(rebuilt.serializer->deserialize(blob));

  const auto scene = onlyScene(rebuilt);
  ASSERT_EQ(classNames(scene), (Names{ "Alpha", "Beta" }));
  EXPECT_EQ(scene->findScript("Alpha")->getFields(), nlohmann::json::parse(R"([{"name":"first","value":1}])"));
  EXPECT_EQ(scene->getObjectManager()->getAllObjects().size(), 1u);
}

TEST(SceneScriptList, WireRoundTripKeepsOrderClassNamesAndFields)
{
  const auto original = makeProject();
  const auto originalScene = buildProject(original);

  net::Message message(net::MessageType::snapshot);
  original.packer->pack(message);

  const auto rebuilt = makeProject();
  rebuilt.packer->unpack(message);

  const auto scene = onlyScene(rebuilt);
  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta", "Gamma" }));
  expectSameScripts(scene, originalScene);
  EXPECT_EQ(scene->getObjectManager()->getAllObjects().size(), 1u);
}

TEST(SceneScriptList, AnEmptyListSurvivesTheWire)
{
  const auto original = makeProject();
  original.sceneManager->addScene(std::make_shared<SceneAsset>(sceneUUID, "Main", original.componentRegistry));

  net::Message message(net::MessageType::snapshot);
  original.packer->pack(message);

  const auto rebuilt = makeProject();
  rebuilt.packer->unpack(message);

  EXPECT_TRUE(onlyScene(rebuilt)->getScripts().empty());
}

TEST(SceneScriptList, StoppingRestoresTheAuthoredListAndItsFields)
{
  const auto project = makeProject();
  const auto scene = buildProject(project);
  const auto authored = scene->serialize().at("scripts");
  ASSERT_EQ(authored.size(), 3u);

  scene->start();

  scene->findScript("Alpha")->setFields(nlohmann::json::parse(R"([{"name":"speed","value":99}])"));
  ASSERT_TRUE(scene->removeScript("Beta"));
  ASSERT_TRUE(scene->addScript(makeEntry("Runtime")));
  ASSERT_TRUE(scene->moveScript("Gamma", 0));
  ASSERT_EQ(classNames(scene), (Names{ "Gamma", "Alpha", "Runtime" }));

  scene->stop();

  EXPECT_EQ(classNames(scene), (Names{ "Alpha", "Beta", "Gamma" }));
  EXPECT_EQ(scene->serialize().at("scripts"), authored);
}

TEST(SceneScriptList, StoppingWithoutAStartLeavesTheListAlone)
{
  const auto scene = makeBareScene();
  ASSERT_TRUE(scene->addScript(makeEntry("Alpha")));

  scene->stop();

  EXPECT_EQ(classNames(scene), (Names{ "Alpha" }));
}
