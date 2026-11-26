#ifndef TERRAINEDITECHO_H
#define TERRAINEDITECHO_H

#include "TerrainStore.h"
#include "TerrainStreamPlan.h"
#include "TerrainTypes.h"
#include <Protocol.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <uuid.h>

namespace terrain
{
  inline constexpr std::chrono::milliseconds terrainEchoInterval{ 50 };

  // Not thread safe; the server drives it from its tick thread.
  class TerrainEditEcho
  {
  public:
    // Must not call back into the echo.
    using Send = std::function<void(int32_t connId, const net::Message& message)>;

    // A terrain edit from sender changed these chunks on the server store. The caller has already given every changed
    // chunk a fresh revision from TerrainStore::nextRevision (through setChunk): a view drops an entry whose revision
    // is not above the one it holds. Coordinates beyond maxChunkCoord are ignored.
    void recordEdit(const uuids::uuid& terrain, std::span<const ChunkCoord> coords, int32_t sender);

    // connId asked for the full chunks (a hash-only entry it was sent did not match what it holds). The caller
    // validates that the terrain exists; one that does not is dropped at the next flush.
    void requestFull(const uuids::uuid& terrain, std::span<const ChunkCoord> coords, int32_t connId);

    // Drops the connection's pending requests and its standing as a sender: chunks it changed still go to everyone
    // else in full at the next flush, since no remaining connection predicted the edit.
    void forget(int32_t connId);

    // Drops every recorded edit and request, for a project load that replaces the volumes they describe.
    void clear();

    // Drops the terrain's recorded edits and requests, for a terrain the store removed or whose volume it replaced: a
    // record kept across that would flush the new volume's contents as if an edit had made them.
    void forgetTerrain(const uuids::uuid& terrain);

    [[nodiscard]] bool pending() const;

    // Sends every recorded chunk's current store state once to each listed connection - hash-only to a connection
    // that was the only sender of that chunk since the last flush, full to every other - and each requested chunk in
    // full to its requester (if listed). connections must not contain duplicates: a duplicate is sent every entry
    // twice. Then clears the record. Terrains are served in uuid order and chunks in z, y, x order. A terrain the store no longer holds is dropped, and a chunk whose store revision is 0 is given
    // one before it is sent.
    //
    // Each connection is served on its own: when a send throws, the other connections are still served and the first
    // exception is rethrown at the end. The record is cleared before anything is sent, so a throw never replays it.
    void flush(TerrainStore& store, std::span<const int32_t> connections, const Send& send,
               size_t budgetBytes = defaultStreamBudgetBytes);

  private:
    struct CoordLess
    {
      bool operator()(const ChunkCoord& a, const ChunkCoord& b) const;
    };

    using ConnectionsByChunk = std::map<ChunkCoord, std::set<int32_t>, CoordLess>;

    // shared: the edits did not all come from one connection (or a sender was forgotten), so no connection predicted
    // the chunk's final state and every connection gets it in full.
    struct EditRecord
    {
      std::set<int32_t> senders;
      bool shared = false;
    };

    std::map<uuids::uuid, std::map<ChunkCoord, EditRecord, CoordLess>> m_edits;
    std::map<uuids::uuid, ConnectionsByChunk> m_requests;
  };
}

#endif //TERRAINEDITECHO_H
