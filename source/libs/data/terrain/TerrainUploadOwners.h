#ifndef TERRAINUPLOADOWNERS_H
#define TERRAINUPLOADOWNERS_H

#include "TerrainStore.h"
#include <cstdint>
#include <map>
#include <optional>
#include <uuid.h>
#include <vector>

namespace terrain
{
  // The server's record of which connection owes the upload of each terrain with a pending expectation, so a
  // disconnect drops only the expectations that connection owed. Not thread safe.
  class TerrainUploadOwners
  {
  public:
    // Makes the connection the owner of the terrain, replacing an earlier owner.
    void claim(const uuids::uuid& terrain, int32_t connection);

    // Claims every terrain in the store that has an expectation.
    void claimExpected(const TerrainStore& store, int32_t connection);

    void release(const uuids::uuid& terrain);

    void clear();

    [[nodiscard]] std::optional<int32_t> owner(const uuids::uuid& terrain) const;

    // Forgets every terrain the connection owns and clears the expectation of each that still has one. Returns those,
    // in uuid order.
    std::vector<uuids::uuid> drop(int32_t connection, TerrainStore& store);

  private:
    std::map<uuids::uuid, int32_t> m_owners;
  };
}

#endif //TERRAINUPLOADOWNERS_H
