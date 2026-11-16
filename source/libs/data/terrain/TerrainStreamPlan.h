#ifndef TERRAINSTREAMPLAN_H
#define TERRAINSTREAMPLAN_H

#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include "TerrainVolume.h"
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace terrain
{
  inline constexpr size_t defaultStreamBudgetBytes = 256 * 1024;

  // These must equal the terrainChunks wire framing: the header of one chunk entry and the header of the message.
  inline constexpr size_t streamEntryOverheadBytes = 28;
  inline constexpr size_t streamMessageOverheadBytes = 20;

  struct StreamedChunk
  {
    ChunkCoord coord;
    // The chunk as the volume held it when the batch was taken; the air chunk for a coordinate the volume no longer
    // holds, so a receiver that already has it erases it.
    std::shared_ptr<const TerrainChunk> chunk;
    // encodeChunk(*chunk).
    std::vector<uint8_t> encoded;
  };

  // Not thread safe; the server keeps one per streaming connection on its tick thread.
  class TerrainStreamPlan
  {
  public:
    // Queues every chunk the volume holds. focus is in the terrain's sample space (local position / voxel size); chunks
    // are ordered by squared distance from focus to the chunk's center, ties broken by coordinate (z, then y, then x)
    // so the order is deterministic.
    TerrainStreamPlan(const TerrainVolume& volume, glm::vec3 focus);

    // Takes chunks in order while streamMessageOverheadBytes plus the sum of (streamEntryOverheadBytes + encoded size)
    // stays within budgetBytes. The first chunk is always taken, even alone over the budget, so the stream cannot
    // stall. Also stops at maxEntries chunks (0 counts as 1), since uniform chunks encode to a few bytes and a byte
    // budget alone could exceed one message's entry cap. Reads each chunk from the volume at this call, so an edit
    // made since it was queued is sent as edited.
    [[nodiscard]] std::vector<StreamedChunk> next(const TerrainVolume& volume,
                                                  size_t budgetBytes = defaultStreamBudgetBytes,
                                                  size_t maxEntries = std::numeric_limits<size_t>::max());

    // Queues the coordinate again (an edit after it was sent, or while pending - no duplicate). Out-of-range
    // coordinates are ignored.
    void requeue(ChunkCoord coord);

    [[nodiscard]] bool done() const;

    [[nodiscard]] size_t pending() const;

  private:
    struct Key
    {
      double distanceSquared;
      ChunkCoord coord;

      bool operator<(const Key& other) const;
    };

    [[nodiscard]] Key keyFor(ChunkCoord coord) const;

    glm::vec3 m_focus;
    std::set<Key> m_queue;
    // Dedup keys on the coordinate, so it never depends on keyFor giving identical doubles at each call.
    std::unordered_map<ChunkCoord, Key> m_queued;
  };
}

#endif //TERRAINSTREAMPLAN_H
