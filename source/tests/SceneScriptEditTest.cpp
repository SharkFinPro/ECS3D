#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "objects/Object.h"
#include "objects/ObjectManager.h"
#include "objects/components/Script.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneScriptEdits.h"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
  using replication::SceneEditResult;

  const auto sceneUUID = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();

  const auto alphaFields = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":3}])");
  const auto gammaFields = nlohmann::json::parse(R"([{"name":"on","type":"bool","value":true}])");

  // A scene holding one object plus the scene scripts Alpha (fields), Beta and Gamma (fields), in that order.
  std::shared_ptr<SceneAsset> makeScene()
  {
    auto registry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*registry);

    auto scene = std::make_shared<SceneAsset>(sceneUUID, "Main", registry);
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

  // The whole list as serialized: class names, order and field blobs. The field arrays are ordered as
  // authored, so comparing them exactly is sound.
  nlohmann::json scriptsOf(const std::shared_ptr<SceneAsset>& scene)
  {
    return scene->serialize().at("scripts");
  }

  std::vector<std::string> namesOf(const std::shared_ptr<SceneAsset>& scene)
  {
    std::vector<std::string> names;
    for (const auto& script : scene->getScripts())
    {
      names.push_back(script->getClassName());
    }

    return names;
  }

  using Names = std::vector<std::string>;
}

TEST(SceneScriptEditTest, AddAppendsByDefaultAndLoadsFields)
{
  const auto scene = makeScene();

  const auto edit = replication::buildAddSceneScript("Delta", std::nullopt, &alphaFields);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, edit), SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Beta", "Gamma", "Delta" }));
  EXPECT_EQ(scene->findScript("Delta")->getFields(), alphaFields);
}

TEST(SceneScriptEditTest, AddWithoutFieldsLeavesAnEmptyBlob)
{
  const auto scene = makeScene();

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildAddSceneScript("Delta")),
            SceneEditResult::applied);
  EXPECT_EQ(scene->findScript("Delta")->getFields(), nlohmann::json::array());
}

TEST(SceneScriptEditTest, AddHonorsAnIndexAndAppendsPastTheEnd)
{
  const auto scene = makeScene();

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildAddSceneScript("Delta", 1)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Delta", "Beta", "Gamma" }));

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildAddSceneScript("Epsilon", 99)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Delta", "Beta", "Gamma", "Epsilon" }));
}

TEST(SceneScriptEditTest, AddOfAnExistingClassIsRejectedAndLeavesTheListAlone)
{
  const auto scene = makeScene();
  const auto before = scriptsOf(scene);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildAddSceneScript("Beta", 0, &gammaFields)),
            SceneEditResult::rejected);
  EXPECT_EQ(scriptsOf(scene), before);

  // Positive control: the same builder with a new class changes the list.
  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildAddSceneScript("Delta", 0)),
            SceneEditResult::applied);
  EXPECT_NE(scriptsOf(scene), before);
}

TEST(SceneScriptEditTest, MalformedEditsAreRefusedWithoutChangingTheList)
{
  const auto scene = makeScene();
  const auto before = scriptsOf(scene);

  const std::vector<nlohmann::json> malformed = {
    nlohmann::json{ { "op", "addSceneScript" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", 5 } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "fields", "nope" } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "fields", nlohmann::json::array({ 1 }) } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" },
                    { "fields", nlohmann::json::array({ nlohmann::json{ { "name", "x" } } }) } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "index", -1 } },
    nlohmann::json{ { "op", "addSceneScript" }, { "className", "Delta" }, { "index", "first" } },
    nlohmann::json{ { "op", "removeSceneScript" } },
    nlohmann::json{ { "op", "removeSceneScript" }, { "className", 7 } },
    nlohmann::json{ { "op", "reorderSceneScript" }, { "className", "Alpha" } },
    nlohmann::json{ { "op", "reorderSceneScript" }, { "className", "Alpha" }, { "index", 1.5 } },
    nlohmann::json{ { "op", "reorderSceneScript" }, { "index", 1 } },
    nlohmann::json{ { "op", "setSceneScriptFields" }, { "className", "Alpha" } },
    nlohmann::json{ { "op", "setSceneScriptFields" }, { "className", "Alpha" }, { "fields", nlohmann::json::object() } },
    nlohmann::json{ { "op", "setSceneScriptFields" }, { "className", "Alpha" },
                    { "fields", nlohmann::json::array({ nlohmann::json{ { "name", 1 }, { "type", "int" } } }) } }
  };

  for (const auto& edit : malformed)
  {
    EXPECT_EQ(replication::applySceneScriptEdit(*scene, edit), SceneEditResult::malformedEdit) << edit.dump();
    EXPECT_EQ(scriptsOf(scene), before) << edit.dump();
  }

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, nlohmann::json::array()), SceneEditResult::malformedEdit);
  EXPECT_EQ(replication::applySceneScriptEdit(*scene, nlohmann::json{ { "op", "addObject" } }),
            SceneEditResult::malformedEdit);

  // Positive control: a well formed edit of the same shape does change the list.
  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildSetSceneScriptFields("Alpha", gammaFields)),
            SceneEditResult::applied);
  EXPECT_NE(scriptsOf(scene), before);
}

TEST(SceneScriptEditTest, RemoveDropsTheEntry)
{
  const auto scene = makeScene();

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildRemoveSceneScript("Beta")),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Gamma" }));
}

TEST(SceneScriptEditTest, RemoveOfAnAbsentClassIsUnknownComponentAndLeavesTheListAlone)
{
  const auto scene = makeScene();
  const auto before = scriptsOf(scene);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildRemoveSceneScript("Nope")),
            SceneEditResult::unknownComponent);
  EXPECT_EQ(scriptsOf(scene), before);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildRemoveSceneScript("Gamma")),
            SceneEditResult::applied);
  EXPECT_NE(scriptsOf(scene), before);
}

TEST(SceneScriptEditTest, ReorderReadsTheIndexAgainstTheListWithTheScriptRemoved)
{
  const auto scene = makeScene();

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Alpha", 2)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Beta", "Gamma", "Alpha" }));

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Alpha", 0)),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Beta", "Gamma" }));
}

TEST(SceneScriptEditTest, ReorderRefusalsLeaveTheListAlone)
{
  const auto scene = makeScene();
  const auto before = scriptsOf(scene);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Nope", 0)),
            SceneEditResult::unknownComponent);
  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Alpha", 3)),
            SceneEditResult::rejected);
  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Alpha", 0)),
            SceneEditResult::rejected);
  EXPECT_EQ(scriptsOf(scene), before);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript("Alpha", 2)),
            SceneEditResult::applied);
  EXPECT_NE(scriptsOf(scene), before);
}

TEST(SceneScriptEditTest, SetFieldsReplacesOnlyThatEntrysBlob)
{
  const auto scene = makeScene();

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildSetSceneScriptFields("Alpha", gammaFields)),
            SceneEditResult::applied);
  EXPECT_EQ(scene->findScript("Alpha")->getFields(), gammaFields);
  EXPECT_EQ(scene->findScript("Gamma")->getFields(), gammaFields);
  EXPECT_EQ(scene->findScript("Beta")->getFields(), nlohmann::json::array());
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Beta", "Gamma" }));

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildSetSceneScriptFields("Alpha", nlohmann::json::array())),
            SceneEditResult::applied);
  EXPECT_EQ(scene->findScript("Alpha")->getFields(), nlohmann::json::array());
}

TEST(SceneScriptEditTest, SetFieldsOnAnUnknownClassIsRefusedAndLeavesTheListAlone)
{
  const auto scene = makeScene();
  const auto before = scriptsOf(scene);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildSetSceneScriptFields("Nope", gammaFields)),
            SceneEditResult::unknownComponent);
  EXPECT_EQ(scriptsOf(scene), before);
}

TEST(SceneScriptEditTest, BuildersProduceEditsThatApplyThroughTheRoundTrip)
{
  const auto scene = makeScene();
  const auto reparsed = [](const nlohmann::json& edit) { return nlohmann::json::parse(edit.dump()); };

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, reparsed(replication::buildAddSceneScript("Delta", 0, &alphaFields))),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Delta", "Alpha", "Beta", "Gamma" }));
  EXPECT_EQ(scene->findScript("Delta")->getFields(), alphaFields);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, reparsed(replication::buildReorderSceneScript("Delta", 3))),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Beta", "Gamma", "Delta" }));

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, reparsed(replication::buildSetSceneScriptFields("Delta", gammaFields))),
            SceneEditResult::applied);
  EXPECT_EQ(scene->findScript("Delta")->getFields(), gammaFields);

  EXPECT_EQ(replication::applySceneScriptEdit(*scene, reparsed(replication::buildRemoveSceneScript("Delta"))),
            SceneEditResult::applied);
  EXPECT_EQ(namesOf(scene), (Names{ "Alpha", "Beta", "Gamma" }));
}

TEST(SceneScriptEditTest, IsSceneScriptEditRecognizesOnlyTheFourOps)
{
  EXPECT_TRUE(replication::isSceneScriptEdit(replication::buildAddSceneScript("A")));
  EXPECT_TRUE(replication::isSceneScriptEdit(replication::buildRemoveSceneScript("A")));
  EXPECT_TRUE(replication::isSceneScriptEdit(replication::buildReorderSceneScript("A", 0)));
  EXPECT_TRUE(replication::isSceneScriptEdit(replication::buildSetSceneScriptFields("A", nlohmann::json::array())));

  const auto scene = makeScene();
  const auto objectUUID = scene->getObjectManager()->getObjects().front()->getUUID();
  EXPECT_FALSE(replication::isSceneScriptEdit(replication::buildAddObject("X")));
  EXPECT_FALSE(replication::isSceneScriptEdit(replication::buildRenameObject(objectUUID, "Y")));
  EXPECT_FALSE(replication::isSceneScriptEdit(replication::buildBatch({ replication::buildAddObject("X") })));
  EXPECT_FALSE(replication::isSceneScriptEdit(nlohmann::json{ { "op", 5 } }));
  EXPECT_FALSE(replication::isSceneScriptEdit(nlohmann::json::array()));
  EXPECT_FALSE(replication::isSceneScriptEdit(nlohmann::json::object()));
}

TEST(SceneScriptEditTest, ASceneScriptOpInsideABatchIsMalformedAndNothingInTheBatchApplies)
{
  const auto scene = makeScene();
  auto& objects = *scene->getObjectManager();
  const auto object = objects.getObjects().front();
  const auto scriptsBefore = scriptsOf(scene);

  // Positive control: the same rename on its own, in a batch, is applied.
  const auto control = replication::buildBatch({ replication::buildRenameObject(object->getUUID(), "Renamed") });
  ASSERT_EQ(replication::applySceneEdit(objects, control), SceneEditResult::applied);
  ASSERT_EQ(object->getName(), "Renamed");

  const auto batch = replication::buildBatch({
    replication::buildRenameObject(object->getUUID(), "RenamedAgain"),
    replication::buildRemoveSceneScript("Alpha")
  });

  EXPECT_EQ(replication::applySceneEdit(objects, batch), SceneEditResult::malformedEdit);
  EXPECT_EQ(object->getName(), "Renamed");
  EXPECT_EQ(objects.getObjects().size(), 1u);
  EXPECT_EQ(scriptsOf(scene), scriptsBefore);

  // The same op sent alone is refused by applySceneEdit too: it is not an object op.
  EXPECT_EQ(replication::applySceneEdit(objects, replication::buildRemoveSceneScript("Alpha")),
            SceneEditResult::malformedEdit);
  EXPECT_EQ(scriptsOf(scene), scriptsBefore);
}
