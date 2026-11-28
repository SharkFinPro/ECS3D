#include "RecordTerrainEdits.h"
#include "TerrainSetChunks.h"
#include "terrain/TerrainAddition.h"
#include "terrain/TerrainStore.h"
#include "terrain/TerrainVolume.h"
#include <nlohmann/json.hpp>
#include <string>
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

std::optional<EditCommand> commandForTerrainAdd(const nlohmann::json& asset, const terrain::TerrainStore& store)
{
  const auto description = terrain::parseAddedTerrain(asset);
  if (!description)
  {
    return std::nullopt;
  }

  const auto volume = store.find(description->uuid);
  if (!volume)
  {
    return std::nullopt;
  }

  auto restore = makeTerrainRestore(*description, volume->snapshotChunks(), asset.value("displayName", std::string{}));

  return EditCommand::addAsset(description->uuid, AssetType::Terrain, terrainAssetPath(description->uuid), {}, {},
                               std::move(restore));
}

std::optional<EditCommand> commandForTerrainPalette(const uuids::uuid& terrainUUID, const terrain::TerrainStore& view,
                                                    std::vector<terrain::TerrainMaterial> palette)
{
  const auto* current = view.description(terrainUUID);
  if (!current || current->palette == palette)
  {
    return std::nullopt;
  }

  auto replaced = *current;
  replaced.palette = std::move(palette);
  if (!terrain::isValidDescription(replaced))
  {
    return std::nullopt;
  }

  const auto before = terrain::addTerrainAssetJson(*current);
  const auto after = terrain::addTerrainAssetJson(replaced);
  const std::string path = before.value("path", std::string{});

  return EditCommand::replaceAsset(terrainUUID, AssetType::Terrain, path, {}, before.value("body", std::string{}),
                                   path, {}, after.value("body", std::string{}));
}

}
