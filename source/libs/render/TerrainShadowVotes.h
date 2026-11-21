#ifndef TERRAINSHADOWVOTES_H
#define TERRAINSHADOWVOTES_H

#include <uuid.h>

#include <unordered_map>

// The engine's shadow flag belongs to a terrain's surface, and several objects can draw one terrain, so the surface
// casts when any object drawing it this frame asks to.
class TerrainShadowVotes {
public:
  void clear()
  {
    m_casts.clear();
  }

  void vote(const uuids::uuid& terrainUUID, const bool castShadows)
  {
    m_casts[terrainUUID] |= castShadows;
  }

  // True when any vote for the terrain this frame was true, and with no vote (the engine's default).
  [[nodiscard]] bool castsShadows(const uuids::uuid& terrainUUID) const
  {
    const auto it = m_casts.find(terrainUUID);

    return it == m_casts.end() || it->second;
  }

private:
  std::unordered_map<uuids::uuid, bool> m_casts;
};

#endif //TERRAINSHADOWVOTES_H
