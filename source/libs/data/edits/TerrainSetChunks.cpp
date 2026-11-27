#include "TerrainSetChunks.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainVolume.h"
#include <span>

namespace edits {

std::vector<net::Message> packSetChunksEdits(const TerrainSetChunks& payload, const uint64_t strokeId)
{
  constexpr std::size_t worstEntryBytes = terrain::setChunkEntryHeaderBytes + terrain::maxEncodedChunkBytes;

  std::vector<net::Message> messages;
  const std::span<const terrain::SetChunkEntry> entries(payload.chunks);

  std::size_t begin = 0;
  while (begin < entries.size())
  {
    std::size_t end = begin;
    while (end < entries.size() && end - begin < terrain::maxTerrainChunksEntries &&
           (end - begin + 1) * worstEntryBytes <= maxSetChunksMessageBytes)
    {
      ++end;
    }

    messages.push_back(terrain::packSetChunksEdit(payload.terrain, strokeId, entries.subspan(begin, end - begin)));
    begin = end;
  }

  return messages;
}

void predictSetChunks(terrain::TerrainVolume& volume, const TerrainSetChunks& payload)
{
  for (const auto& entry : payload.chunks)
  {
    volume.setChunk(entry.coord, entry.chunk);
  }
}

bool terrainChunksDiffer(const std::shared_ptr<const terrain::TerrainChunk>& first,
                         const std::shared_ptr<const terrain::TerrainChunk>& second)
{
  if (!first || !second)
  {
    return static_cast<bool>(first) != static_cast<bool>(second);
  }

  return first->hash() != second->hash();
}

}
