#include "TerrainSetChunks.h"
#include "Replication.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainCodec.h"
#include "terrain/TerrainPalette.h"
#include "terrain/TerrainReplication.h"
#include "terrain/TerrainVolume.h"
#include <nlohmann/json.hpp>
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

namespace {

std::optional<terrain::TerrainDescription> describedWith(const terrain::TerrainStore& view, const uuids::uuid& terrain,
                                                         const std::vector<terrain::TerrainMaterial>& palette)
{
  const auto* held = view.description(terrain);
  if (!held)
  {
    return std::nullopt;
  }

  auto described = *held;
  described.palette = palette;
  return terrain::isValidDescription(described) ? std::optional(described) : std::nullopt;
}

}

std::vector<net::Message> packTerrainStep(const TerrainSetChunks& payload, const terrain::TerrainStore& view,
                                          const uint64_t strokeId)
{
  if (!payload.palette)
  {
    return packSetChunksEdits(payload, strokeId);
  }

  const auto described = describedWith(view, payload.terrain, payload.palette->palette);
  if (!described)
  {
    return {};
  }

  if (payload.palette->remap)
  {
    return { terrain::packRemapEdit(payload.terrain, strokeId, *payload.palette->remap,
                                    payload.palette->fromPaletteHash, payload.palette->palette) };
  }

  auto messages = packSetChunksEdits(payload, strokeId);
  messages.push_back(replication::packAddAsset(terrain::addTerrainAssetJson(*described)));
  return messages;
}

bool predictTerrainStep(terrain::TerrainStore& view, const TerrainSetChunks& payload)
{
  const auto volume = view.find(payload.terrain);
  const auto described = payload.palette ? describedWith(view, payload.terrain, payload.palette->palette)
                                         : std::nullopt;

  if (!volume || (payload.palette && !described))
  {
    return false;
  }

  if (payload.palette && payload.palette->remap)
  {
    static_cast<void>(terrain::commitRemap(*volume, *payload.palette->remap));
  }
  else
  {
    predictSetChunks(*volume, payload);
  }

  return !described || view.applyDescription(*described);
}

bool terrainChunksDiffer(const std::shared_ptr<const terrain::TerrainChunk>& first,
                         const std::shared_ptr<const terrain::TerrainChunk>& second)
{
  const bool firstAir = !first || first->isAir();
  const bool secondAir = !second || second->isAir();

  if (firstAir || secondAir)
  {
    return firstAir != secondAir;
  }

  return first->hash() != second->hash();
}

}
