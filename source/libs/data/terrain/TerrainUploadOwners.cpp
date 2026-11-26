#include "TerrainUploadOwners.h"

namespace terrain
{
  void TerrainUploadOwners::claim(const uuids::uuid& terrain, const int32_t connection)
  {
    m_owners[terrain] = connection;
  }

  void TerrainUploadOwners::claimExpected(const TerrainStore& store, const int32_t connection)
  {
    for (const uuids::uuid& uuid : store.terrains())
    {
      if (store.expectedChunks(uuid).has_value())
      {
        claim(uuid, connection);
      }
    }
  }

  void TerrainUploadOwners::release(const uuids::uuid& terrain)
  {
    m_owners.erase(terrain);
  }

  void TerrainUploadOwners::clear()
  {
    m_owners.clear();
  }

  std::optional<int32_t> TerrainUploadOwners::owner(const uuids::uuid& terrain) const
  {
    const auto it = m_owners.find(terrain);

    return it != m_owners.end() ? std::optional<int32_t>(it->second) : std::nullopt;
  }

  std::vector<uuids::uuid> TerrainUploadOwners::drop(const int32_t connection, TerrainStore& store)
  {
    std::vector<uuids::uuid> dropped;

    for (auto it = m_owners.begin(); it != m_owners.end();)
    {
      if (it->second != connection)
      {
        ++it;
        continue;
      }

      if (store.expectedChunks(it->first).has_value())
      {
        store.clearExpectation(it->first);
        dropped.push_back(it->first);
      }

      it = m_owners.erase(it);
    }

    return dropped;
  }
}
