#include "RecordTerrainEdits.h"
#include "TerrainSetChunks.h"
#include "terrain/TerrainVolume.h"
#include <vector>

namespace edits {

std::optional<EditCommand> commandForTerrainStroke(
  const uuids::uuid& terrainUUID,
  const std::span<const std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before,
  const terrain::TerrainVolume& volume)
{
  std::vector<TerrainStrokeChunk> chunks;
  chunks.reserve(before.size());

  for (const auto& [coord, beforeChunk] : before)
  {
    auto after = volume.chunk(coord);

    if (terrainChunksDiffer(beforeChunk, after))
    {
      chunks.push_back({ coord, beforeChunk, std::move(after) });
    }
  }

  if (chunks.empty())
  {
    return std::nullopt;
  }

  return EditCommand::terrainStroke(terrainUUID, std::move(chunks));
}

}
