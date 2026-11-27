#include "EditCommand.h"
#include "TerrainSetChunks.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"
#include <utility>
#include <variant>

namespace edits {

EditCommand EditCommand::terrainStroke(const uuids::uuid& terrainUUID, std::vector<TerrainStrokeChunk> chunks)
{
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
