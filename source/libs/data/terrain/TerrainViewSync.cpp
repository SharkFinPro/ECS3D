#include "TerrainViewSync.h"
#include "TerrainBinding.h"
#include "TerrainStore.h"
#include <memory>

namespace terrain
{
  bool TerrainViewSync::observeSnapshot(TerrainStore& store, const uuids::uuid& loadId)
  {
    const bool changed = m_loadId.has_value() && *m_loadId != loadId;

    m_loadId = loadId;

    if (changed)
    {
      store.resetVolumes();
      m_completed.clear();
    }

    return changed;
  }

  void TerrainViewSync::reset()
  {
    m_loadId.reset();
    m_completed.clear();
  }

  ViewApplyOutcome TerrainViewSync::apply(TerrainStore& store, const SceneManager& scenes, const net::Message& message)
  {
    ViewApplyOutcome outcome;

    const auto payload = unpackTerrainChunks(message);
    if (!payload)
    {
      outcome.malformed = true;
      outcome.log.push_back({ LogLevel::warn, "Refused a malformed terrainChunks message ("
                                                + std::to_string(message.size()) + " bytes)" });
      return outcome;
    }

    outcome.terrain = payload->terrain;
    forgetCompletionIfIncomplete(store, payload->terrain);
    outcome.result = applyTerrainChunks(store, *payload);
    const std::string name = uuids::to_string(payload->terrain);

    if (outcome.result.unknownTerrain)
    {
      outcome.log.push_back({ LogLevel::warn, "Ignored terrainChunks for unknown terrain " + name });
      return outcome;
    }

    if (!outcome.result.missing.empty())
    {
      outcome.log.push_back({ LogLevel::warn, "Terrain " + name + ": " + std::to_string(outcome.result.missing.size())
                                                + " hash-only chunk(s) did not match what is held" });
    }

    if (outcome.result.applied > 0)
    {
      invalidateTerrainBounds(scenes, payload->terrain);
    }

    const TerrainDescription* description = store.description(payload->terrain);
    const auto volume = store.find(payload->terrain);

    if (description && volume && description->chunkCount > 0 && volume->chunks().size() >= description->chunkCount &&
        m_completed.insert(payload->terrain).second)
    {
      outcome.log.push_back({ LogLevel::info, "Terrain " + name + " complete: "
                                                + std::to_string(volume->chunks().size()) + " chunk(s)" });
    }

    return outcome;
  }

  void TerrainViewSync::forgetCompletionIfIncomplete(const TerrainStore& store, const uuids::uuid& terrain)
  {
    const TerrainDescription* description = store.description(terrain);
    const auto volume = store.find(terrain);

    if (!description || !volume || volume->chunks().size() < description->chunkCount)
    {
      m_completed.erase(terrain);
    }
  }
}
