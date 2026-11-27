#ifndef TERRAINASSETUNDOFIXTURES_H
#define TERRAINASSETUNDOFIXTURES_H

#include <gtest/gtest.h>

#include "TerrainCreateFlow.h"
#include "TerrainStreamerFixtures.h"
#include "TestScene.h"
#include "AssetReferences.h"
#include "ComponentRegistry.h"
#include "Replication.h"
#include "assets/AssetRegistry.h"
#include "edits/EditCommand.h"
#include "edits/EditHistory.h"
#include "edits/RecordEdits.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainRestore.h"
#include "objects/ObjectManager.h"
#include "scenes/SceneAsset.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainUpload.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <uuid.h>

// Shared by the terrain asset undo suites: an editor, the server it talks to and the history the editor records.
namespace terrainAssetUndoFixtures
{
  using namespace terrainStreamerFixtures;

  inline const auto terrainId = uuids::uuid::from_string("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb").value();
  inline const auto placedObjectId = uuids::uuid::from_string("cccccccc-cccc-4ccc-8ccc-cccccccccccc").value();
  inline constexpr int createdChunks = 4;
  inline constexpr size_t unlimitedBudget = 100u * 1024 * 1024;

  using Hashes = std::map<std::tuple<int32_t, int32_t, int32_t>, uint64_t>;

  inline Hashes hashesOf(const terrain::TerrainStore& store, const uuids::uuid& uuid)
  {
    Hashes hashes;

    if (const auto volume = store.find(uuid))
    {
      for (const auto& [coord, chunk] : volume->chunks())
      {
        hashes[{ coord.x, coord.y, coord.z }] = chunk->hash();
      }
    }

    return hashes;
  }

  // One side of the wire: the registries and store an editor or the server holds.
  struct Side
  {
    std::shared_ptr<ComponentRegistry> components;
    AssetRegistry assets;
    SceneManager scenes;
    terrain::TerrainStore store;
  };

  // The editor, the server it talks to and the history the editor records. The editor applies every op locally and
  // sends it; the server applies the wire copy, with the upload expectation, and takes the uploaded chunks.
  struct World
  {
    fixtures::Scene scene;
    Side editor;
    Side server;
    edits::EditHistory history;
    terrain::TerrainUpload upload;
    nlohmann::json createAsset;

    World()
    {
      editor.components = scene.componentRegistry;
      server.components = scene.componentRegistry;
    }

    // The create flow: the asset record and an empty volume first, then the generated chunks into the store and onto
    // the upload queue, then the command recorded from the store.
    void create(const bool record = true, const bool pump = true, const int chunkCount = createdChunks)
    {
      terrain::TerrainDescription description;
      description.uuid = terrainId;
      description.voxelSize = 1.0f;
      description.chunkCount = static_cast<uint32_t>(chunkCount);

      auto generated = std::make_shared<terrain::TerrainVolume>(1.0f);
      for (int x = 0; x < chunkCount; ++x)
      {
        generated->setChunk({ x, 0, 0 }, noise(static_cast<uint32_t>(x + 1)));
      }

      createAsset = terrain::addTerrainAssetJson(description);
      createAsset["displayName"] = "Hills";

      ASSERT_TRUE(replication::applyAddAssetWithTerrain(editor.assets, editor.scenes, editor.components, editor.store,
                                                        createAsset, false));
      editor.store.find(terrainId)->restoreChunks(generated->snapshotChunks());
      upload.add(terrainId, generated);

      if (record)
      {
        const auto command = edits::commandForTerrainAdd(createAsset, editor.store);
        ASSERT_TRUE(command.has_value());
        history.record(*command);
      }

      sendAddAsset(replication::packAddAsset(createAsset));

      if (pump)
      {
        pumpUpload();
      }
    }

    void sendAddAsset(const net::Message& message)
    {
      const auto asset = replication::unpackAddAsset(message);
      EXPECT_TRUE(replication::applyAddAssetWithTerrain(server.assets, server.scenes, server.components, server.store,
                                                        asset, true));
    }

    void sendRemoveAsset(const net::Message& message)
    {
      EXPECT_TRUE(replication::applyRemoveAsset(server.assets, server.scenes, replication::unpackRemoveAsset(message)));
      server.store.reconcile(server.assets);
    }

    void pumpUpload()
    {
      upload.pump([this](const net::Message& message) {
        const auto payload = terrain::unpackTerrainChunks(message);
        ASSERT_TRUE(payload.has_value());
        static_cast<void>(terrain::applyTerrainUpload(server.store, *payload));
      }, unlimitedBudget);
    }

    // What EditorApp::onRemoveAsset does: record from the store while it still holds the terrain, apply, send.
    void remove()
    {
      const auto op = replication::buildRemoveAsset(terrainId);
      const auto command = edits::commandForRemoveAsset(op, editor.assets, editor.scenes, &editor.store);
      ASSERT_TRUE(command.has_value());

      ASSERT_TRUE(replication::applyRemoveAsset(editor.assets, editor.scenes, op));
      editor.store.reconcile(editor.assets);
      history.record(*command);

      sendRemoveAsset(replication::packRemoveAsset(op));
    }

    [[nodiscard]] edits::HistoryOutcome undo()
    {
      return history.undo(*scene.objectManager, &editor.assets, nullptr, &editor.store);
    }

    [[nodiscard]] edits::HistoryOutcome redo()
    {
      return history.redo(*scene.objectManager, &editor.assets, nullptr, &editor.store);
    }

    // What EditorApp::reportHistoryOutcome does for an asset payload.
    void apply(const edits::HistoryOutcome& outcome)
    {
      ASSERT_TRUE(outcome.ok());
      ASSERT_TRUE(outcome.messagePayload.has_value());

      if (outcome.terrainRestore)
      {
        ASSERT_EQ(edits::restoreTerrainAndSend(editor.assets, editor.scenes, editor.components, editor.store, upload,
                                               *outcome.terrainRestore,
                                               [&] { sendAddAsset(*outcome.messagePayload); }),
                  edits::TerrainRestoreResult::applied);
        return;
      }

      if (outcome.terrainRemoved)
      {
        upload.forget(*outcome.terrainRemoved);
      }

      if (outcome.messagePayload->getType() == net::MessageType::addAsset)
      {
        sendAddAsset(*outcome.messagePayload);
      }
      else
      {
        // The editor does not apply a removal itself: the snapshot that follows the server's does.
        ASSERT_TRUE(replication::applyRemoveAsset(editor.assets, editor.scenes,
                                                  replication::unpackRemoveAsset(*outcome.messagePayload)));
        editor.store.reconcile(editor.assets);
        sendRemoveAsset(*outcome.messagePayload);
      }
    }

    [[nodiscard]] bool serverHolds() const
    {
      return server.store.find(terrainId) != nullptr && server.assets.getByUUID(terrainId) != nullptr;
    }
  };

  // A chunk set after the create, so the state at delete time differs from what the create generated.
  inline Hashes sculptAfterCreate(World& world)
  {
    const auto edited = noise(99);
    world.editor.store.find(terrainId)->setChunk({ createdChunks, 0, 0 }, edited);
    world.server.store.setChunk(terrainId, { createdChunks, 0, 0 }, edited, world.server.store.nextRevision(terrainId));

    return hashesOf(world.editor.store, terrainId);
  }
}

#endif //TERRAINASSETUNDOFIXTURES_H
