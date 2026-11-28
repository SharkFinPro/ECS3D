#include "TerrainEditApply.h"
#include "TerrainBrush.h"
#include "TerrainChunk.h"
#include "TerrainMaterialRemap.h"
#include "TerrainOps.h"
#include "TerrainPalette.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    TerrainEditResult refused(const TerrainEditRefusal refusal)
    {
      TerrainEditResult result;
      result.refusal = refusal;
      return result;
    }

    // Mirrors the limits redistance() and editSamples() enforce, which return empty edits for a refusal and for an
    // edit that changed nothing alike. Sizes are int64 and capped stepwise because unpacked boxes span all of int32.
    TerrainEditRefusal checkRedistanceBox(const SampleBox& box)
    {
      if (box.empty())
      {
        return TerrainEditRefusal::invalidOp;
      }

      const int64_t grownX = static_cast<int64_t>(box.max.x) - box.min.x + 2;
      const int64_t grownY = static_cast<int64_t>(box.max.y) - box.min.y + 2;
      const int64_t grownZ = static_cast<int64_t>(box.max.z) - box.min.z + 2;

      if (grownX > maxRedistanceSamples || grownY > maxRedistanceSamples || grownZ > maxRedistanceSamples ||
          grownX * grownY > maxRedistanceSamples || grownX * grownY * grownZ > maxRedistanceSamples)
      {
        return TerrainEditRefusal::tooLarge;
      }

      const ChunkCoord first = chunkOfSample(box.min);
      const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

      if (!isValidChunkCoord(first) || !isValidChunkCoord(last))
      {
        return TerrainEditRefusal::invalidOp;
      }

      const int64_t extentX = static_cast<int64_t>(last.x) - first.x + 1;
      const int64_t extentY = static_cast<int64_t>(last.y) - first.y + 1;
      const int64_t extentZ = static_cast<int64_t>(last.z) - first.z + 1;

      return extentX * extentY * extentZ > maxEditChunks ? TerrainEditRefusal::tooLarge
                                                         : TerrainEditRefusal::none;
    }

    struct Plan
    {
      TerrainEditRefusal refusal = TerrainEditRefusal::none;
      ChunkEdits edits;
      // Set for a remap whose palette differs from the held one.
      std::optional<TerrainDescription> description;
    };

    Plan refusedPlan(const TerrainEditRefusal refusal)
    {
      return { refusal, {}, std::nullopt };
    }

    Plan planBrush(const TerrainVolume& volume, const BrushOp& brush)
    {
      if (dirtyBox(volume, brush).empty())
      {
        return refusedPlan(TerrainEditRefusal::invalidOp);
      }

      return { TerrainEditRefusal::none, applyBrush(volume, brush), std::nullopt };
    }

    Plan planRedistance(const TerrainVolume& volume, const SampleBox& box)
    {
      const TerrainEditRefusal refusal = checkRedistanceBox(box);

      if (refusal != TerrainEditRefusal::none)
      {
        return refusedPlan(refusal);
      }

      return { TerrainEditRefusal::none, redistance(volume, box), std::nullopt };
    }

    bool sameAsHeld(const std::shared_ptr<const TerrainChunk>& held,
                    const std::shared_ptr<const TerrainChunk>& incoming)
    {
      const bool heldAir = !held || held->isAir();
      const bool incomingAir = !incoming || incoming->isAir();

      return heldAir ? incomingAir : (!incomingAir && held->hash() == incoming->hash());
    }

    Plan planSetChunks(const TerrainVolume& volume, const std::vector<SetChunkEntry>& chunks)
    {
      if (!std::ranges::all_of(chunks, [](const SetChunkEntry& entry) { return isValidChunkCoord(entry.coord); }))
      {
        return refusedPlan(TerrainEditRefusal::invalidOp);
      }

      Plan plan;

      for (const SetChunkEntry& entry : chunks)
      {
        if (!sameAsHeld(volume.chunk(entry.coord), entry.chunk))
        {
          const bool air = !entry.chunk || entry.chunk->isAir();
          plan.edits.emplace_back(entry.coord, air ? nullptr : entry.chunk);
        }
      }

      return plan;
    }

    Plan planEdit(const TerrainVolume& volume, const TerrainEditPayload& edit)
    {
      switch (edit.kind)
      {
        case TerrainEditKind::brush:
          return planBrush(volume, edit.brush);
        case TerrainEditKind::redistance:
          return planRedistance(volume, edit.box);
        case TerrainEditKind::setChunks:
          return planSetChunks(volume, edit.chunks);
        case TerrainEditKind::remapMaterials:
          break;
      }

      return refusedPlan(TerrainEditRefusal::invalidOp);
    }

    // The palette is checked before the chunks are read, so a refused remap costs no pass over the volume.
    Plan planRemap(const TerrainStore& store, const TerrainVolume& volume, const TerrainEditPayload& edit)
    {
      const TerrainDescription* held = store.description(edit.terrain);

      if (!held || !isValidRemap(edit.remap) || isIdentity(edit.remap))
      {
        return refusedPlan(TerrainEditRefusal::invalidOp);
      }

      if (paletteHash(held->palette) != edit.paletteHash)
      {
        return refusedPlan(TerrainEditRefusal::paletteChanged);
      }

      TerrainDescription replaced = *held;
      replaced.palette = edit.palette;

      if (!isValidDescription(replaced))
      {
        return refusedPlan(TerrainEditRefusal::invalidOp);
      }

      Plan plan{ TerrainEditRefusal::none, remapMaterials(volume, edit.remap), std::nullopt };
      if (replaced.palette != held->palette)
      {
        plan.description = std::move(replaced);
      }

      return plan;
    }
  }

  TerrainEditResult applyTerrainEdit(TerrainStore& store, const TerrainEditPayload& edit)
  {
    const auto volume = store.find(edit.terrain);

    if (!volume)
    {
      return refused(TerrainEditRefusal::unknownTerrain);
    }

    Plan plan = edit.kind == TerrainEditKind::remapMaterials ? planRemap(store, *volume, edit)
                                                             : planEdit(*volume, edit);

    if (plan.refusal != TerrainEditRefusal::none)
    {
      return refused(plan.refusal);
    }

    TerrainEditResult result;
    const uint32_t revision = plan.edits.empty() ? 0 : store.nextRevision(edit.terrain);

    if (!plan.edits.empty() && revision == 0)
    {
      return refused(TerrainEditRefusal::revisionsExhausted);
    }

    if (plan.description)
    {
      result.paletteReplaced = store.applyDescription(*plan.description);
    }

    result.revision = revision;
    result.changed.reserve(plan.edits.size());

    for (auto& [coord, chunk] : plan.edits)
    {
      if (!store.setChunk(edit.terrain, coord, std::move(chunk), revision))
      {
        throw std::logic_error("terrain edit could not be stored after it was validated");
      }

      result.changed.push_back(coord);
    }

    return result;
  }

  std::vector<ChunkCoord> restampRefusedRemap(TerrainStore& store, const TerrainEditPayload& edit)
  {
    const auto volume = store.find(edit.terrain);
    if (!volume || edit.kind != TerrainEditKind::remapMaterials)
    {
      return {};
    }

    const ChunkEdits wouldChange = remapMaterials(*volume, edit.remap);
    const uint32_t revision = wouldChange.empty() ? 0 : store.nextRevision(edit.terrain);
    std::vector<ChunkCoord> coords;

    if (revision == 0)
    {
      return coords;
    }

    coords.reserve(wouldChange.size());
    for (const auto& entry : wouldChange)
    {
      if (store.setChunk(edit.terrain, entry.first, volume->chunk(entry.first), revision))
      {
        coords.push_back(entry.first);
      }
    }

    return coords;
  }

  std::string_view refusalText(const TerrainEditRefusal refusal)
  {
    switch (refusal)
    {
      case TerrainEditRefusal::none:
        return "no refusal";
      case TerrainEditRefusal::unknownTerrain:
        return "the terrain is unknown";
      case TerrainEditRefusal::invalidOp:
        return "the operation is invalid";
      case TerrainEditRefusal::tooLarge:
        return "the operation is too large";
      case TerrainEditRefusal::revisionsExhausted:
        return "the terrain has no revisions left";
      case TerrainEditRefusal::paletteChanged:
        return "the terrain's palette changed since the edit was made";
    }

    return "unknown refusal";
  }
}
