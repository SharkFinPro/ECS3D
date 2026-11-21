#ifndef TERRAINSIDEVOTES_H
#define TERRAINSIDEVOTES_H

#include <uuid.h>

#include <unordered_map>

// Open sides change a surface's geometry, and several objects can draw one terrain, so the surface is open only when
// every object drawing it this frame asks for it.
class TerrainSideVotes {
public:
  void clear()
  {
    m_open.clear();
  }

  void vote(const uuids::uuid& terrainUUID, const bool openSides)
  {
    const auto [it, inserted] = m_open.try_emplace(terrainUUID, openSides);

    if (!inserted)
    {
      it->second = it->second && openSides;
    }
  }

  // True when every vote for the terrain this frame was true; false with no vote (the closed default).
  [[nodiscard]] bool opensSides(const uuids::uuid& terrainUUID) const
  {
    const auto it = m_open.find(terrainUUID);

    return it != m_open.end() && it->second;
  }

private:
  std::unordered_map<uuids::uuid, bool> m_open;
};

#endif //TERRAINSIDEVOTES_H
