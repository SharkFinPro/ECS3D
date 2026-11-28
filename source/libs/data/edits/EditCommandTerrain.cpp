#include "EditCommand.h"
#include "TerrainSetChunks.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"
#include <nlohmann/json.hpp>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace edits {

EditCommand EditCommand::terrainStroke(const uuids::uuid& terrainUUID, std::vector<TerrainStrokeChunk> chunks)
{
  if (chunks.empty())
  {
    throw std::invalid_argument("A terrain stroke needs at least one chunk");
  }

  std::unordered_set<terrain::ChunkCoord> seen;
  for (const auto& chunk : chunks)
  {
    if (!terrain::isValidChunkCoord(chunk.coord))
    {
      throw std::invalid_argument("A terrain stroke chunk coordinate is beyond what the wire can carry");
    }

    if (!seen.insert(chunk.coord).second)
    {
      throw std::invalid_argument("A terrain stroke names the same chunk coordinate twice");
    }
  }

  EditCommand command;
  command.m_kind = CommandKind::terrainStroke;
  command.m_data = TerrainStrokeData{ terrainUUID, std::move(chunks) };
  return command;
}

const std::vector<TerrainStrokeChunk>* EditCommand::terrainStrokeChunks() const
{
  if (const auto* data = std::get_if<TerrainStrokeData>(&m_data))
  {
    return &data->chunks;
  }

  return nullptr;
}

namespace {
  std::size_t restoreBytes(const std::optional<TerrainRestore>& restore)
  {
    std::size_t bytes = 0;

    if (restore)
    {
      for (const auto& entry : restore->chunks)
      {
        bytes += entry.second ? entry.second->byteSize() : 0;
      }
    }

    return bytes;
  }
}

std::size_t EditCommand::pinnedBytes() const
{
  if (const auto* add = std::get_if<AddAssetData>(&m_data))
  {
    return restoreBytes(add->terrain);
  }

  if (const auto* remove = std::get_if<RemoveAssetData>(&m_data))
  {
    return restoreBytes(remove->terrain);
  }

  const auto* data = std::get_if<TerrainStrokeData>(&m_data);
  if (!data)
  {
    return 0;
  }

  std::size_t bytes = 0;

  for (const auto& chunk : data->chunks)
  {
    bytes += chunk.before ? chunk.before->byteSize() : 0;
    bytes += chunk.after ? chunk.after->byteSize() : 0;
  }

  return bytes;
}

Validation EditCommand::validateTerrainStroke(const terrain::TerrainStore* terrains, const bool forUndo) const
{
  const auto& data = std::get<TerrainStrokeData>(m_data);

  const auto volume = terrains ? terrains->find(data.terrainUUID) : nullptr;
  if (!volume)
  {
    return { ValidationFailure::targetMissing, data.terrainUUID };
  }

  for (const auto& chunk : data.chunks)
  {
    if (terrainChunksDiffer(volume->chunk(chunk.coord), forUndo ? chunk.after : chunk.before))
    {
      return { ValidationFailure::targetChanged, data.terrainUUID };
    }
  }

  return {};
}

Validation EditCommand::validateTerrainReAdd(const uuids::uuid& assetUUID, const terrain::TerrainStore* terrains) const
{
  if (!terrains)
  {
    return { ValidationFailure::targetMissing, assetUUID };
  }

  if (terrains->find(assetUUID))
  {
    return { ValidationFailure::targetChanged, assetUUID };
  }

  return {};
}

Validation EditCommand::validateTerrainPalette(const uuids::uuid& assetUUID, const std::string& body,
                                               const terrain::TerrainStore* terrains)
{
  const auto* live = terrains ? terrains->description(assetUUID) : nullptr;
  if (!live)
  {
    return { ValidationFailure::targetMissing, assetUUID };
  }

  const auto expected = terrain::parseAddedTerrain({ { "assetType", "terrain" },
                                                     { "uuid", uuids::to_string(assetUUID) },
                                                     { "body", body } });
  if (!expected || expected->palette != live->palette)
  {
    return { ValidationFailure::targetChanged, assetUUID };
  }

  return {};
}

std::optional<TerrainRestore> EditCommand::terrainToRestore(const bool forUndo) const
{
  if (forUndo)
  {
    if (const auto* data = std::get_if<RemoveAssetData>(&m_data))
    {
      return data->terrain;
    }
  }
  else if (const auto* data = std::get_if<AddAssetData>(&m_data))
  {
    return data->terrain;
  }

  return std::nullopt;
}

std::optional<uuids::uuid> EditCommand::terrainToRemove(const bool forUndo) const
{
  if (forUndo)
  {
    if (const auto* data = std::get_if<AddAssetData>(&m_data); data && data->terrain)
    {
      return data->assetUUID;
    }
  }
  else if (const auto* data = std::get_if<RemoveAssetData>(&m_data); data && data->terrain)
  {
    return data->assetUUID;
  }

  return std::nullopt;
}

TerrainSetChunks EditCommand::buildUndoTerrainChunks() const
{
  const auto& data = std::get<TerrainStrokeData>(m_data);

  TerrainSetChunks payload;
  payload.terrain = data.terrainUUID;
  payload.chunks.reserve(data.chunks.size());

  for (const auto& chunk : data.chunks)
  {
    payload.chunks.push_back({ chunk.coord, chunk.before });
  }

  return payload;
}

TerrainSetChunks EditCommand::buildRedoTerrainChunks() const
{
  const auto& data = std::get<TerrainStrokeData>(m_data);

  TerrainSetChunks payload;
  payload.terrain = data.terrainUUID;
  payload.chunks.reserve(data.chunks.size());

  for (const auto& chunk : data.chunks)
  {
    payload.chunks.push_back({ chunk.coord, chunk.after });
  }

  return payload;
}

}
