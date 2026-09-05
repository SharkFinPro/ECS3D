#include <gtest/gtest.h>

#include "assets/AssetRegistry.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <string>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto modelUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto otherUUID = uuidFrom("22222222-2222-2222-2222-222222222222");
  const auto prefabUUID = uuidFrom("33333333-3333-3333-3333-333333333333");
  const auto missingUUID = uuidFrom("99999999-9999-9999-9999-999999999999");

  AssetRecord model(const uuids::uuid& uuid, const std::string& path)
  {
    return { .uuid = uuid, .type = AssetType::Model, .path = path };
  }

  std::string prefabBody(const std::string& name)
  {
    const nlohmann::json body = {
      { "name", name },
      { "uuid", "44444444-4444-4444-4444-444444444444" },
      { "children", nlohmann::json::array() },
      { "components", nlohmann::json::array() },
      { "scripts", nlohmann::json::array() }
    };

    return body.dump();
  }
}

TEST(AssetRegistry, LooksARecordUpByUuidAndByPath)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  const auto* byUUID = registry.getByUUID(modelUUID);
  ASSERT_NE(byUUID, nullptr);
  EXPECT_EQ(byUUID->path, "assets/models/cube.glb");

  EXPECT_EQ(registry.getByPath("assets/models/cube.glb"), byUUID);

  EXPECT_EQ(registry.getByUUID(missingUUID), nullptr);
  EXPECT_EQ(registry.getByPath("assets/models/nothing.glb"), nullptr);
}

TEST(AssetRegistry, RegisteringAPathTwiceKeepsTheFirstRecord)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));
  registry.registerAsset(model(otherUUID, "assets/models/cube.glb"));

  // First-wins: importing the same file again must not mint a second uuid, or every reference to the
  // original would be pointing at a record nothing else knows about.
  EXPECT_NE(registry.getByUUID(modelUUID), nullptr);
  EXPECT_EQ(registry.getByUUID(otherUUID), nullptr);
  EXPECT_EQ(registry.getAssets().size(), 1u);
}

TEST(AssetRegistry, ReRegisteringAPrefabUpdatesItsBodyInPlace)
{
  AssetRegistry registry;
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = prefabBody("Block") });
  registry.registerAsset({ .uuid = otherUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = prefabBody("Block v2") });

  // "Save as Prefab" over an existing name means update it, and the uuid has to survive or every
  // instance in the scene loses its asset.
  ASSERT_EQ(registry.getAssets().size(), 1u);
  ASSERT_NE(registry.getByUUID(prefabUUID), nullptr);
  EXPECT_EQ(registry.getPrefabBody(prefabUUID).at("name"), "Block v2");
}

TEST(AssetRegistry, GivesNoPrefabBodyForAnythingElse)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Broken",
                           .body = "{ not json" });

  // Callers skip on a null body rather than throwing in the middle of a tick.
  EXPECT_TRUE(registry.getPrefabBody(missingUUID).is_null());
  EXPECT_TRUE(registry.getPrefabBody(modelUUID).is_null());
  EXPECT_TRUE(registry.getPrefabBody(prefabUUID).is_null());
}

TEST(AssetRegistry, RenamingChangesOnlyTheDisplayName)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  registry.renameAsset(modelUUID, "Nicer Cube");

  const auto* record = registry.getByUUID(modelUUID);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->displayName, "Nicer Cube");

  // The path is the registry key and the file on disk, so a rename must leave it alone.
  EXPECT_EQ(record->path, "assets/models/cube.glb");
  EXPECT_EQ(registry.getByPath("assets/models/cube.glb"), record);
}

TEST(AssetRegistry, RenamingAnUnknownUuidDoesNothing)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  registry.renameAsset(missingUUID, "Nothing");

  EXPECT_EQ(registry.getAssets().size(), 1u);
  EXPECT_TRUE(registry.getByUUID(modelUUID)->displayName.empty());
}

TEST(AssetRegistry, RemovingFreesThePathForReuse)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  registry.removeAsset(modelUUID);

  EXPECT_EQ(registry.getByUUID(modelUUID), nullptr);
  EXPECT_EQ(registry.getByPath("assets/models/cube.glb"), nullptr);

  // The path key has to go with the record, or a name-keyed prefab could never be recreated.
  registry.registerAsset(model(otherUUID, "assets/models/cube.glb"));

  EXPECT_NE(registry.getByUUID(otherUUID), nullptr);
}

TEST(AssetRegistry, RemovingAnUnknownUuidDoesNothing)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  registry.removeAsset(missingUUID);

  EXPECT_EQ(registry.getAssets().size(), 1u);
}

TEST(AssetRegistry, BumpsItsVersionWhenTheContentsChange)
{
  AssetRegistry registry;
  const auto initial = registry.getVersion();

  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));
  const auto afterRegister = registry.getVersion();
  EXPECT_GT(afterRegister, initial);

  registry.renameAsset(modelUUID, "Nicer Cube");
  const auto afterRename = registry.getVersion();
  EXPECT_GT(afterRename, afterRegister);

  registry.removeAsset(modelUUID);
  const auto afterRemove = registry.getVersion();
  EXPECT_GT(afterRemove, afterRename);

  registry.clear();
  EXPECT_GT(registry.getVersion(), afterRemove);
}

TEST(AssetRegistry, ClearDropsEverything)
{
  AssetRegistry registry;
  registry.registerAsset(model(modelUUID, "assets/models/cube.glb"));
  registry.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = prefabBody("Block") });

  registry.clear();

  EXPECT_TRUE(registry.getAssets().empty());
  EXPECT_EQ(registry.getByPath("assets/models/cube.glb"), nullptr);
}

TEST(AssetRegistry, RoundTripsEveryRecordKindThroughBothPaths)
{
  AssetRegistry original;
  original.registerAsset(model(modelUUID, "assets/models/cube.glb"));
  original.registerAsset({ .uuid = otherUUID, .type = AssetType::Script,
                           .path = "scripts/UserScripts/Player.cs", .className = "PlayerScript" });
  original.registerAsset({ .uuid = prefabUUID, .type = AssetType::Prefab, .path = "Block",
                           .body = prefabBody("Block") });
  original.renameAsset(modelUUID, "Nicer Cube");

  AssetRegistry fromJson;
  fromJson.loadFromJSON(original.serialize());

  net::Message message(net::MessageType::snapshot);
  original.pack(message);

  net::MessageReader reader(message);
  AssetRegistry fromWire;
  fromWire.unpack(reader);

  for (const auto* registry : { &fromJson, &fromWire })
  {
    ASSERT_EQ(registry->getAssets().size(), 3u);

    const auto* rebuiltModel = registry->getByUUID(modelUUID);
    ASSERT_NE(rebuiltModel, nullptr);
    EXPECT_EQ(rebuiltModel->path, "assets/models/cube.glb");
    EXPECT_EQ(rebuiltModel->displayName, "Nicer Cube");

    const auto* rebuiltScript = registry->getByUUID(otherUUID);
    ASSERT_NE(rebuiltScript, nullptr);
    EXPECT_EQ(rebuiltScript->className, "PlayerScript");

    EXPECT_EQ(registry->getPrefabBody(prefabUUID).at("name"), "Block");
  }
}

TEST(AssetRegistry, LoadingMergesIntoWhateverIsAlreadyThere)
{
  AssetRegistry source;
  source.registerAsset(model(modelUUID, "assets/models/cube.glb"));

  AssetRegistry target;
  target.registerAsset(model(otherUUID, "assets/models/sphere.glb"));
  target.loadFromJSON(source.serialize());

  // Neither loadFromJSON nor unpack clears first. Loading a project replaces rather than merges only
  // because its callers parse into a fresh registry and swap that in - a precondition this type neither
  // states nor enforces, so pin the behavior it actually has.
  EXPECT_EQ(target.getAssets().size(), 2u);
  EXPECT_NE(target.getByUUID(otherUUID), nullptr);
}
