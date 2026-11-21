#ifndef TERRAINSIDEVOTES_H
#define TERRAINSIDEVOTES_H

#include <uuid.h>

#include <cstdint>
#include <unordered_map>

// Open sides change a surface's geometry, and several objects can draw one terrain, so a side of its surface is open
// only when every object drawing it this frame asks for that side.
class TerrainSideVotes {
public:
  void clear()
  {
    m_open.clear();
  }

  // openSides is a mask of terrain::TerrainSide bits.
  void vote(const uuids::uuid& terrainUUID, const uint8_t openSides)
  {
    const auto [it, inserted] = m_open.try_emplace(terrainUUID, openSides);

    if (!inserted)
    {
      it->second &= openSides;
    }
  }

  // The sides every vote for the terrain this frame asked to open; 0 with no vote (the closed default).
  [[nodiscard]] uint8_t openSides(const uuids::uuid& terrainUUID) const
  {
    const auto it = m_open.find(terrainUUID);

    return it == m_open.end() ? uint8_t{ 0 } : it->second;
  }

private:
  std::unordered_map<uuids::uuid, uint8_t> m_open;
};

#endif //TERRAINSIDEVOTES_H
