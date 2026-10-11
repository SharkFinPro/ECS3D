#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "SceneScriptsPanelLogic.h"
#include "assets/AssetRegistry.h"
#include "objects/components/Script.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneScriptEdits.h"
#include "Replication.h"

#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
  using Names = std::vector<std::string>;

  const auto sceneUUID = uuids::uuid::from_string("66666666-6666-6666-6666-666666666666").value();

  void addScriptAsset(AssetRegistry& registry, const std::string& path, const std::string& className,
                      const AssetType type = AssetType::Script)
  {
    AssetRecord record;
    static int counter = 0;
    char text[40];
    std::snprintf(text, sizeof(text), "00000000-0000-0000-0000-%012d", ++counter);
    record.uuid = uuids::uuid::from_string(text).value();
    record.type = type;
    record.path = path;
    record.className = className;
    registry.registerAsset(record);
  }

  std::shared_ptr<SceneAsset> makeScene(const Names& classes)
  {
    auto registry = std::make_shared<ComponentRegistry>();
    registerDataComponents(*registry);
    auto scene = std::make_shared<SceneAsset>(sceneUUID, "Main", registry);

    for (const auto& name : classes)
    {
      EXPECT_TRUE(scene->addScript(std::make_shared<Script>(name)));
    }

    return scene;
  }

  Names namesOf(const SceneAsset& scene)
  {
    Names names;
    for (const auto& script : scene.getScripts())
    {
      names.push_back(script->getClassName());
    }

    return names;
  }
}

TEST(SceneScriptsPanelTest, AvailableClassesExcludeAttachedOnesAndAreSortedAndUnique)
{
  AssetRegistry assets;
  addScriptAsset(assets, "scripts/Zeta.cs", "Zeta");
  addScriptAsset(assets, "scripts/Alpha.cs", "Alpha");
  addScriptAsset(assets, "scripts/Beta.cs", "Beta");
  addScriptAsset(assets, "scripts/AlphaCopy.cs", "Alpha");

  const auto scene = makeScene({ "Beta" });

  EXPECT_EQ(sceneScriptPanel::availableClasses(assets, scene->getScripts()), (Names{ "Alpha", "Zeta" }));

  const auto empty = makeScene({});
  EXPECT_EQ(sceneScriptPanel::availableClasses(assets, empty->getScripts()), (Names{ "Alpha", "Beta", "Zeta" }));
}

TEST(SceneScriptsPanelTest, AvailableClassesSkipNonScriptAssetsAndBlankClassNames)
{
  AssetRegistry assets;
  addScriptAsset(assets, "scripts/Real.cs", "Real");
  addScriptAsset(assets, "models/Thing.obj", "NotAScript", AssetType::Model);
  addScriptAsset(assets, "scripts/Blank.cs", "");

  const auto scene = makeScene({});

  EXPECT_EQ(sceneScriptPanel::availableClasses(assets, scene->getScripts()), (Names{ "Real" }));
}

TEST(SceneScriptsPanelTest, ReorderTargetIsTheNeighborAndNothingAtTheEdges)
{
  EXPECT_EQ(sceneScriptPanel::reorderTarget(1, 3, true), std::optional<std::size_t>(0));
  EXPECT_EQ(sceneScriptPanel::reorderTarget(1, 3, false), std::optional<std::size_t>(2));
  EXPECT_EQ(sceneScriptPanel::reorderTarget(2, 3, true), std::optional<std::size_t>(1));
  EXPECT_EQ(sceneScriptPanel::reorderTarget(0, 3, false), std::optional<std::size_t>(1));

  EXPECT_FALSE(sceneScriptPanel::reorderTarget(0, 3, true).has_value());
  EXPECT_FALSE(sceneScriptPanel::reorderTarget(2, 3, false).has_value());
  EXPECT_FALSE(sceneScriptPanel::reorderTarget(0, 1, true).has_value());
  EXPECT_FALSE(sceneScriptPanel::reorderTarget(0, 1, false).has_value());
  EXPECT_FALSE(sceneScriptPanel::reorderTarget(3, 3, true).has_value());
}

TEST(SceneScriptsPanelTest, FieldEditIsSentOnlyOnceNoWidgetIsActiveAndTheCopyDiffers)
{
  const auto replicated = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":3}])");
  const auto edited = nlohmann::json::parse(R"([{"name":"speed","type":"float","value":4}])");

  EXPECT_TRUE(sceneScriptPanel::shouldSendFieldEdit(&edited, replicated, false));

  EXPECT_FALSE(sceneScriptPanel::shouldSendFieldEdit(&edited, replicated, true));
  EXPECT_FALSE(sceneScriptPanel::shouldSendFieldEdit(nullptr, replicated, false));
  EXPECT_FALSE(sceneScriptPanel::shouldSendFieldEdit(&replicated, replicated, false));
}

TEST(SceneScriptsPanelTest, ChosenReorderIndexMovesTheScriptExactlyOnePlace)
{
  const Names order = { "Alpha", "Beta", "Gamma" };

  for (const bool up : { true, false })
  {
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      const auto scene = makeScene(order);
      const auto target = sceneScriptPanel::reorderTarget(i, order.size(), up);

      if (!target)
      {
        // The edge rows: the panel disables the button, and the server would refuse the move anyway.
        EXPECT_TRUE((up && i == 0) || (!up && i == order.size() - 1));
        EXPECT_EQ(replication::applySceneScriptEdit(*scene,
                    replication::buildReorderSceneScript(order[i], up ? 0 : order.size())),
                  replication::SceneEditResult::rejected);
        EXPECT_EQ(namesOf(*scene), order);
        continue;
      }

      EXPECT_EQ(replication::applySceneScriptEdit(*scene, replication::buildReorderSceneScript(order[i], *target)),
                replication::SceneEditResult::applied);

      Names expected = order;
      const auto neighbor = up ? i - 1 : i + 1;
      std::swap(expected[i], expected[neighbor]);
      EXPECT_EQ(namesOf(*scene), expected) << "index " << i << (up ? " up" : " down");
    }
  }
}
