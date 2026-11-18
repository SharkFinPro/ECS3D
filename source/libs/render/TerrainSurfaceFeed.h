#ifndef TERRAINSURFACEFEED_H
#define TERRAINSURFACEFEED_H

#include <terrain/TerrainVolume.h>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>

// The chunk snapshot a terrain's surfaces read, shared by all of them. Fills copy the pointer under a shared lock and
// read the immutable chunks without it; publish swaps the pointer and runs the invalidations under the exclusive
// lock, so no fill can see a new snapshot before the surfaces are told about it.
class TerrainSurfaceFeed
{
public:
  explicit TerrainSurfaceFeed(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot)
    : m_snapshot(std::move(snapshot))
  {}

  [[nodiscard]] std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot() const
  {
    const std::shared_lock lock(m_mutex);

    return m_snapshot;
  }

  // report may be empty. It runs once, after the swap, while the exclusive lock is held, so it must not call
  // snapshot().
  void publish(std::shared_ptr<const terrain::TerrainVolume::ChunkMap> snapshot, const std::function<void()>& report)
  {
    const std::unique_lock lock(m_mutex);

    m_snapshot = std::move(snapshot);

    if (report)
    {
      report();
    }
  }

private:
  mutable std::shared_mutex m_mutex;
  std::shared_ptr<const terrain::TerrainVolume::ChunkMap> m_snapshot;
};

#endif //TERRAINSURFACEFEED_H
