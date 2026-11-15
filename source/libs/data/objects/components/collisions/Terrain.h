#ifndef TERRAIN_H
#define TERRAIN_H

#include "Collider.h"
#include <glm/vec3.hpp>
#include <uuid.h>
#include <cstdint>
#include <memory>

namespace terrain
{
  class TerrainVolume;
}

// An object's terrain: a reference to terrain data plus the collider settings that govern it. It is a Collider
// so the broad phase, layers, masks and events apply to it, but it never has a trigger and its geometry is the
// box around the volume's occupied chunks. It does not own the terrain data: the app resolves terrainUUID to a
// volume and hands it over with setVolume.
class Terrain final : public Collider {
public:
  Terrain();

  [[nodiscard]] const uuids::uuid& getTerrainUUID() const;
  void setTerrainUUID(const uuids::uuid& terrainUUID);

  [[nodiscard]] bool getCollisionEnabled() const;
  void setCollisionEnabled(bool collisionEnabled);

  [[nodiscard]] bool getCastShadows() const;
  void setCastShadows(bool castShadows);

  // Never serialized or packed.
  void setVolume(std::weak_ptr<const terrain::TerrainVolume> volume);
  [[nodiscard]] std::shared_ptr<const terrain::TerrainVolume> volume() const;

  // A volume edit moves the occupied bounds without moving a transform, so whoever edits the volume calls this.
  void invalidateBounds();

  [[nodiscard]] nlohmann::json serialize() override;

  void loadFromJSON(const nlohmann::json& componentData) override;

  // The support of the world-space box around the volume's occupied bounds; the owner's position when there is
  // no volume or it is empty.
  glm::vec3 findFurthestPoint(const glm::vec3& direction) override;

  void pack(net::Message& message) const override;

  void unpack(net::MessageReader& messageReader) override;

private:
  uuids::uuid m_terrainUUID;
  bool m_collisionEnabled = true;
  bool m_castShadows = true;

  std::weak_ptr<const terrain::TerrainVolume> m_volume;

  // Every persisted field, read and checked as a whole before any of it is assigned.
  struct Data {
    uuids::uuid terrainUUID;
    bool collisionEnabled = true;
    bool castShadows = true;
    bool renderCollider = false;
    uint32_t layer = 0;
    uint32_t mask = 0xFFFFFFFFu;
  };

  void assign(const Data& data);
};

#endif //TERRAIN_H
