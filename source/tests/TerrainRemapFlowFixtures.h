#ifndef TERRAINREMAPFLOWFIXTURES_H
#define TERRAINREMAPFLOWFIXTURES_H

#include <gtest/gtest.h>

#include "TerrainPaletteEditor.h"
#include "TestScene.h"
#include "Replication.h"
#include "edits/EditHistory.h"
#include "edits/RecordTerrainEdits.h"
#include "edits/TerrainSetChunks.h"
#include "scenes/SceneManager.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainBrush.h"
#include "terrain/TerrainChunk.h"
#include "terrain/TerrainEdit.h"
#include "terrain/TerrainEditApply.h"
#include "terrain/TerrainEditEcho.h"
#include "terrain/TerrainGenerators.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainViewSync.h"
#include "terrain/TerrainVolume.h"

#include <Protocol.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <uuid.h>

// Two editors and the server, wired the way ServerApp and EditorApp wire a material removal or move: the editor
// predicts and records, the server applies, echoes the chunks and then re-snapshots the palette.
namespace remapFlowFixtures
{
  inline constexpr int32_t connA = 1;
  inline constexpr int32_t connB = 2;

  inline const auto terrainId = uuids::uuid::from_string("66666666-6666-4666-8666-666666666666").value();

  inline std::vector<terrain::TerrainMaterial> namedPalette(const size_t count)
  {
    std::vector<terrain::TerrainMaterial> palette;

    for (size_t i = 0; i < count; ++i)
    {
      palette.push_back({ .name = "m" + std::to_string(i), .color = glm::vec3(0.1f * static_cast<float>(i)) });
    }

    return palette;
  }

  using Hashes = std::map<std::tuple<int32_t, int32_t, int32_t>, uint64_t>;

  inline Hashes hashesOf(const terrain::TerrainVolume& volume)
  {
    Hashes hashes;

    for (const auto& [coord, chunk] : volume.chunks())
    {
      hashes[{ coord.x, coord.y, coord.z }] = chunk->hash();
    }

    return hashes;
  }

  struct Node
  {
    terrain::TerrainStore store;
    std::shared_ptr<terrain::TerrainVolume> volume;
    terrain::TerrainViewSync sync;
    edits::EditHistory history;

    [[nodiscard]] const std::vector<terrain::TerrainMaterial>& palette() const
    {
      return store.description(terrainId)->palette;
    }
  };

  struct RemapFlow
  {
    fixtures::Scene scene;
    SceneManager scenes;
    Node server;
    Node a;
    Node b;
    terrain::TerrainEditEcho echo;
    std::vector<std::pair<int32_t, net::Message>> requests;
    std::map<int32_t, size_t> fullEntries;

    RemapFlow()
    {
      server.volume = server.store.create({ .uuid = terrainId, .voxelSize = 2.5f, .palette = namedPalette(4) });

      // Flat ground of material 1 with buried chunks of materials 2 and 3, so a removal shifts painted indices.
      terrain::ChunkEdits edits = terrain::fillFlat(*server.volume, { 64, 64 }, 10.0f, 20.0f, 1);
      edits.emplace_back(terrain::ChunkCoord{ 0, -3, 0 }, terrain::TerrainChunk::uniform(-100, 2));
      edits.emplace_back(terrain::ChunkCoord{ 1, -3, 0 }, terrain::TerrainChunk::uniform(-100, 3));

      const uint32_t revision = server.store.nextRevision(terrainId);
      for (const auto& [coord, chunk] : edits)
      {
        server.store.setChunk(terrainId, coord, chunk, revision);
      }

      join(a);
      join(b);
    }

    // What a joining connection receives: the snapshot's description, then the whole terrain streamed.
    void join(Node& view)
    {
      auto description = *server.store.description(terrainId);
      description.chunkCount = static_cast<uint32_t>(server.volume->chunks().size());
      view.volume = view.store.create(description);

      std::vector<terrain::ChunkEntry> entries;
      for (const auto& [coord, chunk] : server.volume->chunks())
      {
        entries.push_back(terrain::makeChunkEntry(coord, server.store.revision(terrainId, coord), chunk));
      }

      const auto payload = terrain::unpackTerrainChunks(terrain::packTerrainChunks(terrainId, entries));
      ASSERT_TRUE(payload.has_value());
      ASSERT_EQ(terrain::applyTerrainChunks(view.store, *payload).applied, entries.size());
    }

    // ServerApp::broadcastSnapshot as far as the terrain goes: every view takes the server's description.
    void snapshot()
    {
      for (Node* view : { &a, &b })
      {
        auto description = *server.store.description(terrainId);
        description.chunkCount = static_cast<uint32_t>(server.volume->chunks().size());
        ASSERT_TRUE(view->store.applyDescription(description));
      }
    }

    // ServerApp::handleTerrainEdit (with resyncTerrainView) and handleAddAsset (a palette replacement), each flushing
    // the echo before its snapshot.
    terrain::TerrainEditRefusal send(const int32_t connId, const net::Message& message)
    {
      if (message.getType() == net::MessageType::addAsset)
      {
        const auto parsed = terrain::parseAddedTerrain(replication::unpackAddAsset(message));
        auto description = *server.store.description(terrainId);
        description.palette = parsed ? parsed->palette : description.palette;
        EXPECT_TRUE(parsed.has_value() && server.store.applyDescription(description));
        deliver();
        snapshot();
        return terrain::TerrainEditRefusal::none;
      }

      const auto edit = terrain::unpackTerrainEdit(message);
      EXPECT_TRUE(edit.has_value());
      const auto result = edit ? terrain::applyTerrainEdit(server.store, *edit)
                               : terrain::TerrainEditResult{ terrain::TerrainEditRefusal::invalidOp };

      if (!result.changed.empty())
      {
        echo.recordEdit(terrainId, result.changed, connId);
      }

      if (result.refusal == terrain::TerrainEditRefusal::paletteChanged)
      {
        echo.requestFull(terrainId, terrain::restampRefusedRemap(server.store, *edit), connId);
      }

      if (result.paletteReplaced || result.refusal == terrain::TerrainEditRefusal::paletteChanged)
      {
        deliver();
        snapshot();
      }

      return result.refusal;
    }

    void deliver()
    {
      const std::vector<int32_t> connections{ connA, connB };

      echo.flush(server.store, connections, [this](const int32_t connId, const net::Message& message)
      {
        Node& view = connId == connA ? a : b;
        const auto payload = terrain::unpackTerrainChunks(message);
        for (const auto& entry : payload ? payload->entries : std::vector<terrain::ReceivedChunk>{})
        {
          fullEntries[connId] += entry.chunk ? 1 : 0;
        }

        auto outcome = view.sync.apply(view.store, scenes, message);
        if (outcome.request)
        {
          requests.emplace_back(connId, std::move(*outcome.request));
        }
      });
    }

    // Delivers the echo, then answers every request a hash mismatch raised.
    void settle()
    {
      deliver();

      for (int round = 0; round < 3 && !requests.empty(); ++round)
      {
        for (const auto& [connId, message] : std::exchange(requests, {}))
        {
          const auto payload = terrain::unpackTerrainChunks(message);
          ASSERT_TRUE(payload.has_value());

          std::vector<terrain::ChunkCoord> coords;
          for (const auto& entry : payload->entries)
          {
            coords.push_back(entry.coord);
          }

          echo.requestFull(terrainId, coords, connId);
        }

        deliver();
      }
    }

    // What EditorApp::finishTerrainPaletteEdit does for a removal or move: predict, record one entry, send one edit.
    terrain::TerrainEditRefusal edit(Node& view, const int32_t connId, const TerrainPaletteEditor::Result& result)
    {
      EXPECT_TRUE(result.ok());
      EXPECT_TRUE(result.remap.has_value());
      auto prediction = edits::predictTerrainRemap(terrainId, view.store, *result.remap, result.palette);
      EXPECT_TRUE(prediction.has_value());
      if (!prediction)
      {
        return terrain::TerrainEditRefusal::invalidOp;
      }

      view.history.record(std::move(prediction->command));
      return send(connId, prediction->message);
    }

    terrain::TerrainEditRefusal remove(Node& view, const int32_t connId, const size_t index, const size_t replacement)
    {
      return edit(view, connId, TerrainPaletteEditor::remove(view.palette(), index, replacement));
    }

    terrain::TerrainEditRefusal move(Node& view, const int32_t connId, const size_t from, const size_t to)
    {
      return edit(view, connId, TerrainPaletteEditor::move(view.palette(), from, to));
    }

    // What EditorApp::sendHistoryTerrainStep does with an undo or redo.
    edits::HistoryOutcome step(Node& view, const int32_t connId, const bool undo)
    {
      auto outcome = undo ? view.history.undo(*scene.objectManager, nullptr, nullptr, &view.store)
                          : view.history.redo(*scene.objectManager, nullptr, nullptr, &view.store);

      if (outcome.ok() && outcome.terrainPayload)
      {
        const auto messages = edits::packTerrainStep(*outcome.terrainPayload, view.store);
        EXPECT_TRUE(edits::predictTerrainStep(view.store, *outcome.terrainPayload));

        for (const net::Message& message : messages)
        {
          EXPECT_EQ(send(connId, message), terrain::TerrainEditRefusal::none);
        }
      }

      return outcome;
    }

    void expectConverged() const
    {
      const Hashes expected = hashesOf(*server.volume);

      for (const Node* view : { &a, &b })
      {
        EXPECT_EQ(hashesOf(*view->volume), expected);
        EXPECT_EQ(view->palette(), server.palette());
      }
    }
  };
}

#endif //TERRAINREMAPFLOWFIXTURES_H
