#include "TerrainSaving.h"
#include <Log.h>
#include <ProjectPaths.h>
#include <assets/AssetRegistry.h>
#include <terrain/TerrainFiles.h>
#include <terrain/TerrainStore.h>
#include <optional>
#include <string>

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

bool writeEditorTerrains(const std::filesystem::path& root, const AssetRegistry& registry,
                         const terrain::TerrainStore& store)
{
  if (const std::optional<std::string> reason = terrain::incompleteTerrainReason(store))
  {
    Log::error(LogCategory::editor, "Not saving while terrain is still arriving from the server (" + *reason +
      "). Save again once it has arrived.");
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

  const terrain::TerrainSaveReport report = terrain::saveProjectTerrains(root, registry, store);

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
