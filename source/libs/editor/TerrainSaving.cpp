#include "TerrainSaving.h"
#include <Log.h>
#include <ProjectPaths.h>
#include <assets/AssetRegistry.h>
#include <terrain/TerrainFiles.h>
#include <terrain/TerrainStore.h>
#include <terrain/TerrainVolume.h>
#include <memory>
#include <optional>
#include <string>
#include <uuid.h>

namespace
{
  bool anyTerrainHoldsChunks(const terrain::TerrainStore& store)
  {
    for (const auto& uuid : store.terrains())
    {
      const auto volume = store.find(uuid);

      if (volume && !volume->chunks().empty())
      {
        return true;
      }
    }

    return false;
  }
}

std::optional<size_t> savedChunksToKeep(const std::filesystem::path& root, const uuids::uuid& uuid,
                                        const terrain::TerrainVolume& volume)
{
  if (!volume.chunks().empty())
  {
    return std::nullopt;
  }

  const std::optional<size_t> saved = terrain::savedChunkCount(root, uuid);

  return saved && *saved > 0 ? saved : std::nullopt;
}

bool writeEditorTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                         const terrain::TerrainStore& store)
{
  if (const std::optional<terrain::IncompleteTerrain> incomplete = terrain::firstIncompleteTerrain(store))
  {
    Log::error(LogCategory::editor, "Not saving while terrain " + uuids::to_string(incomplete->uuid) +
      " is still arriving from the server: " + std::to_string(incomplete->held) + " of " +
      std::to_string(incomplete->expected) + " chunk(s) received");
    return false;
  }

  if (root.empty())
  {
    if (anyTerrainHoldsChunks(store))
    {
      Log::warn(LogCategory::editor, "Terrain is not saved: it needs a project folder. Use Save As to move the "
        "project into one.");
    }

    return true;
  }

  terrain::TerrainStore toSave;

  for (const uuids::uuid& uuid : store.terrains())
  {
    const terrain::TerrainDescription* description = store.description(uuid);
    const std::shared_ptr<terrain::TerrainVolume> volume = store.find(uuid);

    if (description == nullptr || volume == nullptr)
    {
      continue;
    }

    if (const std::optional<size_t> kept = savedChunksToKeep(root, uuid, *volume))
    {
      Log::warn(LogCategory::editor, "Not saving terrain " + uuids::to_string(uuid) +
        ": the editor holds none of its " + std::to_string(*kept) + " saved chunk(s), so its files are kept");
      continue;
    }

    if (!toSave.adopt(*description, volume))
    {
      Log::error(LogCategory::editor, "Could not save terrain: " + uuids::to_string(uuid) + " was refused");
      return false;
    }
  }

  const terrain::TerrainSaveReport report = terrain::saveProjectTerrains(root, registry, toSave);

  for (const std::string& error : report.errors)
  {
    Log::error(LogCategory::editor, "Could not save terrain: " + error);
  }

  if (!report.errors.empty())
  {
    return false;
  }

  if (report.saved > 0)
  {
    Log::info(LogCategory::editor, "Saved " + std::to_string(report.saved) + " terrain(s) to " +
      pathToUtf8(root / "terrain"));
  }

  return true;
}
