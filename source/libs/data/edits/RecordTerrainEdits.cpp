#include "RecordTerrainEdits.h"
#include "TerrainSetChunks.h"
#include "terrain/TerrainVolume.h"
#include <unordered_set>
#include <vector>

namespace edits {

std::optional<EditCommand> commandForTerrainStroke(
  const uuids::uuid& terrainUUID,
  const std::span<const std::pair<terrain::ChunkCoord, std::shared_ptr<const terrain::TerrainChunk>>> before,
  const terrain::TerrainVolume& volume)
{
  std::vector<TerrainStrokeChunk> chunks;
  chunks.reserve(before.size());
  std::unordered_set<terrain::ChunkCoord> seen;

  for (const auto& [coord, beforeChunk] : before)
  {
    if (!seen.insert(coord).second)
    {
      continue;
    }

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
