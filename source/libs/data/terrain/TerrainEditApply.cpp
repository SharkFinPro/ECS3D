#include "TerrainEditApply.h"
#include "TerrainBrush.h"
#include "TerrainChunk.h"
#include "TerrainOps.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

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
  }

  TerrainEditResult applyTerrainEdit(TerrainStore& store, const TerrainEditPayload& edit)
  {
    const auto volume = store.find(edit.terrain);

    if (!volume)
    {
      return refused(TerrainEditRefusal::unknownTerrain);
    }

    ChunkEdits edits;

    switch (edit.kind)
    {
      case TerrainEditKind::brush:
        if (dirtyBox(*volume, edit.brush).empty())
        {
          return refused(TerrainEditRefusal::invalidOp);
        }

        edits = applyBrush(*volume, edit.brush);
        break;
      case TerrainEditKind::redistance:
      {
        const TerrainEditRefusal refusal = checkRedistanceBox(edit.box);

        if (refusal != TerrainEditRefusal::none)
        {
          return refused(refusal);
        }

        edits = redistance(*volume, edit.box);
        break;
      }
      case TerrainEditKind::setChunks:
      {
        for (const SetChunkEntry& entry : edit.chunks)
        {
          if (!isValidChunkCoord(entry.coord))
          {
            return refused(TerrainEditRefusal::invalidOp);
          }
        }

        for (const SetChunkEntry& entry : edit.chunks)
        {
          const auto held = volume->chunk(entry.coord);
          const bool heldAir = !held || held->isAir();
          const bool incomingAir = !entry.chunk || entry.chunk->isAir();

          if (heldAir ? incomingAir : (!incomingAir && held->hash() == entry.chunk->hash()))
          {
            continue;
          }

          edits.emplace_back(entry.coord, incomingAir ? nullptr : entry.chunk);
        }

        break;
      }
      default:
        return refused(TerrainEditRefusal::invalidOp);
    }

    TerrainEditResult result;

    if (edits.empty())
    {
      return result;
    }

    const uint32_t revision = store.nextRevision(edit.terrain);

    if (revision == 0)
    {
      return refused(TerrainEditRefusal::revisionsExhausted);
    }

    result.revision = revision;
    result.changed.reserve(edits.size());

    for (auto& [coord, chunk] : edits)
    {
      if (!store.setChunk(edit.terrain, coord, std::move(chunk), revision))
      {
        throw std::logic_error("terrain edit could not be stored after it was validated");
      }

      result.changed.push_back(coord);
    }

    return result;
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
    }

    return "unknown refusal";
  }
}
