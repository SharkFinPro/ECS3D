#include <gtest/gtest.h>

#include "ComponentRegistration.h"
#include "ComponentRegistry.h"
#include "ProjectPacker.h"
#include "assets/AssetRegistry.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainTypes.h"
#include "WireTypes.h"

#include <Protocol.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <uuid.h>

namespace {
  uuids::uuid uuidFrom(const std::string& text)
  {
    return uuids::uuid::from_string(text).value();
  }

  const auto firstUUID = uuidFrom("11111111-1111-1111-1111-111111111111");
  const auto secondUUID = uuidFrom("22222222-2222-2222-2222-222222222222");
  const auto thirdUUID = uuidFrom("33333333-3333-3333-3333-333333333333");
  const auto sceneUUID = uuidFrom("44444444-4444-4444-4444-444444444444");

  AssetRecord terrainRecord(const uuids::uuid& uuid)
  {
    return { .uuid = uuid, .type = AssetType::Terrain, .path = terrainAssetPath(uuid) };
  }

  struct Project {
    std::shared_ptr<ComponentRegistry> componentRegistry = std::make_shared<ComponentRegistry>();
    AssetRegistry assetRegistry;
    SceneManager sceneManager;
    terrain::TerrainStore store;
    ProjectPacker packer;

    Project()
      : packer(&assetRegistry, &sceneManager, componentRegistry, &store)
    {
      registerDataComponents(*componentRegistry);
    }

    void addScene()
    {
      const auto scene = std::make_shared<SceneAsset>(sceneUUID, "Main", componentRegistry);
      sceneManager.addScene(scene);
      sceneManager.loadScene(scene);
      assetRegistry.registerAsset({ .uuid = sceneUUID, .type = AssetType::Scene, .path = "Main" });
    }
  };

  terrain::TerrainDescription grassDescription(const uuids::uuid& uuid, const float voxelSize)
  {
    terrain::TerrainDescription description;
    description.uuid = uuid;
    description.voxelSize = voxelSize;
    description.palette.push_back({ "grass", secondUUID, thirdUUID });

    return description;
  }

  // The sender holds terrain A (voxel size 4, one chunk, one material) and terrain B (defaults).
  void fillSender(Project& sender)
  {
    sender.assetRegistry.registerAsset(terrainRecord(firstUUID));
    sender.assetRegistry.registerAsset(terrainRecord(secondUUID));

    const auto volume = sender.store.create(grassDescription(firstUUID, 4.0f));
    volume->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));
    sender.store.create({ .uuid = secondUUID });
  }

  net::Message packed(const ProjectPacker& packer)
  {
    net::Message message(net::MessageType::snapshot);
    packer.pack(message);

    return message;
  }

  // A valid snapshot whose registry holds terrain A's record and whose trailing terrain section is cut off, so a
  // test can append its own.
  net::Message snapshotWithoutTerrainSection()
  {
    Project sender;
    sender.assetRegistry.registerAsset(terrainRecord(firstUUID));

    const auto whole = packed(sender.packer);
    const auto bytes = whole.bytes();
    const std::span<const uint8_t> prefix(bytes.data(), bytes.size() - sizeof(uint32_t));

    return net::Message(net::MessageType::snapshot, prefix);
  }

  net::Message withOneTerrain(const float voxelSize, const uint32_t paletteSize)
  {
    auto message = snapshotWithoutTerrainSection();
    message.write(static_cast<uint32_t>(1));
    message.write(firstUUID);
    message.write(voxelSize);
    message.write(static_cast<uint32_t>(0));
    message.write(paletteSize);
    for (uint32_t i = 0; i < paletteSize; ++i)
    {
      message.writeString("m");
      message.write(uuids::uuid{});
      message.write(uuids::uuid{});
    }

    return message;
  }

  // A receiver with a scene and a terrain, to show a refused snapshot changes none of it.
  struct GuardedReceiver {
    Project project;

    GuardedReceiver()
    {
      project.addScene();
      project.assetRegistry.registerAsset(terrainRecord(firstUUID));
      project.store.create({ .uuid = firstUUID, .voxelSize = 3.0f });
    }

    void expectUntouched() const
    {
      EXPECT_EQ(project.sceneManager.getScenes().size(), 1u);
      EXPECT_NE(project.sceneManager.getCurrentScene(), nullptr);
      EXPECT_NE(project.assetRegistry.getByUUIDOfType(firstUUID, AssetType::Terrain), nullptr);

      const auto* description = project.store.description(firstUUID);
      ASSERT_NE(description, nullptr);
      EXPECT_NEAR(description->voxelSize, 3.0f, 1.0e-6f);
    }
  };
}

TEST(TerrainSnapshot, ADescriptionAppliesInPlaceAndKeepsTheReceiversVolume)
{
  Project sender;
  fillSender(sender);

  Project receiver;
  receiver.assetRegistry.registerAsset(terrainRecord(firstUUID));
  const auto held = receiver.store.create({ .uuid = firstUUID, .voxelSize = 4.0f });
  ASSERT_NE(held, nullptr);

  receiver.packer.unpack(packed(sender.packer));

  EXPECT_EQ(receiver.store.find(firstUUID), held);

  const auto* description = receiver.store.description(firstUUID);
  ASSERT_NE(description, nullptr);
  EXPECT_NEAR(description->voxelSize, 4.0f, 1.0e-6f);
  EXPECT_EQ(description->chunkCount, 1u);
  ASSERT_EQ(description->palette.size(), 1u);
  EXPECT_EQ(description->palette[0].name, "grass");
  EXPECT_EQ(description->palette[0].albedoTexture, secondUUID);
  EXPECT_EQ(description->palette[0].specularTexture, thirdUUID);

  EXPECT_TRUE(held->chunks().empty());
}

TEST(TerrainSnapshot, ATerrainOnlyTheSnapshotNamesIsCreatedAndOneWithoutARecordIsDropped)
{
  Project sender;
  fillSender(sender);

  Project receiver;
  receiver.assetRegistry.registerAsset(terrainRecord(thirdUUID));
  ASSERT_NE(receiver.store.create({ .uuid = thirdUUID }), nullptr);

  receiver.packer.unpack(packed(sender.packer));

  EXPECT_NE(receiver.store.find(firstUUID), nullptr);
  EXPECT_NE(receiver.store.find(secondUUID), nullptr);
  EXPECT_EQ(receiver.store.find(thirdUUID), nullptr);
  EXPECT_EQ(receiver.store.terrains().size(), 2u);
}

TEST(TerrainSnapshot, AVoxelSizeChangeReplacesTheReceiversVolumeWithAnEmptyOne)
{
  Project sender;
  fillSender(sender);

  Project receiver;
  receiver.assetRegistry.registerAsset(terrainRecord(firstUUID));
  const auto held = receiver.store.create({ .uuid = firstUUID, .voxelSize = 2.5f });
  ASSERT_NE(held, nullptr);
  held->setChunk({ 0, 0, 0 }, terrain::TerrainChunk::uniform(-terrain::airDistance, 1));

  receiver.packer.unpack(packed(sender.packer));

  const auto replaced = receiver.store.find(firstUUID);
  ASSERT_NE(replaced, nullptr);
  EXPECT_NE(replaced, held);
  EXPECT_NEAR(replaced->voxelSize(), 4.0f, 1.0e-6f);
  EXPECT_TRUE(replaced->chunks().empty());
}

TEST(TerrainSnapshot, OnlyTheDescriptionsRideTheSnapshotNeverChunkData)
{
  Project sender;
  fillSender(sender);

  const ProjectPacker withoutStore(&sender.assetRegistry, &sender.sceneManager, sender.componentRegistry);
  const size_t plain = packed(withoutStore).bytes().size();
  const size_t withTerrain = packed(sender.packer).bytes().size();

  const size_t uuidSize = sizeof(uuids::uuid);
  const size_t header = uuidSize + sizeof(float) + 2 * sizeof(uint32_t);
  const size_t grass = sizeof(uint32_t) + std::string("grass").size() + 2 * uuidSize;
  const size_t descriptions = header + grass + header;

  EXPECT_EQ(withTerrain - plain, descriptions);
  EXPECT_EQ(sender.store.find(firstUUID)->chunks().size(), 1u);
}

TEST(TerrainSnapshot, ASnapshotWithoutAStoreCarriesNoDescriptionsSoTheReceiverKeepsItsOwn)
{
  Project sender;
  fillSender(sender);

  const ProjectPacker withoutStore(&sender.assetRegistry, &sender.sceneManager, sender.componentRegistry);
  Project receiver;
  receiver.assetRegistry.registerAsset(terrainRecord(firstUUID));
  ASSERT_NE(receiver.store.create({ .uuid = firstUUID, .voxelSize = 3.0f }), nullptr);

  receiver.packer.unpack(packed(withoutStore));

  const auto* description = receiver.store.description(firstUUID);
  ASSERT_NE(description, nullptr);
  EXPECT_NEAR(description->voxelSize, 3.0f, 1.0e-6f);
}

TEST(TerrainSnapshot, AValidDescriptionOfTheSameShapeApplies)
{
  GuardedReceiver receiver;

  receiver.project.packer.unpack(withOneTerrain(4.0f, 2));

  EXPECT_TRUE(receiver.project.sceneManager.getScenes().empty());

  const auto* description = receiver.project.store.description(firstUUID);
  ASSERT_NE(description, nullptr);
  EXPECT_NEAR(description->voxelSize, 4.0f, 1.0e-6f);
  EXPECT_EQ(description->palette.size(), 2u);
}

TEST(TerrainSnapshot, APaletteOverTheBoundMakesUnpackThrowAndLeavesTheLiveProjectUntouched)
{
  GuardedReceiver receiver;

  EXPECT_THROW(receiver.project.packer.unpack(withOneTerrain(4.0f, static_cast<uint32_t>(terrain::maxPaletteSize) + 1)),
               std::exception);

  receiver.expectUntouched();
}

TEST(TerrainSnapshot, ABadVoxelSizeMakesUnpackThrowAndLeavesTheLiveProjectUntouched)
{
  GuardedReceiver receiver;

  EXPECT_THROW(receiver.project.packer.unpack(withOneTerrain(0.0f, 0)), std::exception);

  receiver.expectUntouched();
}

TEST(TerrainSnapshot, ATerrainCountOverTheBoundMakesUnpackThrowAndLeavesTheLiveProjectUntouched)
{
  GuardedReceiver receiver;
  auto message = snapshotWithoutTerrainSection();
  message.write(static_cast<uint32_t>(4097));

  EXPECT_THROW(receiver.project.packer.unpack(message), std::exception);

  receiver.expectUntouched();
}
