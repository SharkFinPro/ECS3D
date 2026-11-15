#include "Terrain.h"
#include "../Transform.h"
#include "../../../terrain/TerrainVolume.h"
#include "WireTypes.h"
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <limits>
#include <string>
#include <utility>
#include <Protocol.h>

namespace {
  void requireNotTrigger(const bool isTrigger)
  {
    if (isTrigger)
    {
      throw RefusedPayload("Terrain: a terrain cannot be a trigger");
    }
  }

  [[nodiscard]] bool readBool(const nlohmann::json& data, const char* key, const bool fallback)
  {
    const auto it = data.find(key);
    if (it == data.end())
    {
      return fallback;
    }

    if (!it->is_boolean())
    {
      throw RefusedPayload(std::string("Terrain: ") + key + " is not a boolean");
    }

    return it->get<bool>();
  }

  [[nodiscard]] uint32_t readUnsigned(const nlohmann::json& data, const char* key, const uint32_t fallback)
  {
    const auto it = data.find(key);
    if (it == data.end())
    {
      return fallback;
    }

    if (!it->is_number_integer() || it->get<int64_t>() < 0 || it->get<int64_t>() > std::numeric_limits<uint32_t>::max())
    {
      throw RefusedPayload(std::string("Terrain: ") + key + " is not an unsigned 32-bit integer");
    }

    return it->get<uint32_t>();
  }

  [[nodiscard]] uuids::uuid readTerrainUUID(const nlohmann::json& data)
  {
    const auto it = data.find("terrainUUID");
    if (it == data.end())
    {
      return {};
    }

    if (!it->is_string())
    {
      throw RefusedPayload("Terrain: terrainUUID is not a string");
    }

    const std::string text = it->get<std::string>();
    if (text.empty())
    {
      return {};
    }

    const auto parsed = uuids::uuid::from_string(text);
    if (!parsed.has_value())
    {
      throw RefusedPayload("Terrain: terrainUUID is not a uuid");
    }

    return *parsed;
  }
}

Terrain::Terrain()
  : Collider(ColliderType::terrainCollider, ComponentType::SubComponentType_terrain)
{}

const uuids::uuid& Terrain::getTerrainUUID() const
{
  return m_terrainUUID;
}

void Terrain::setTerrainUUID(const uuids::uuid& terrainUUID)
{
  if (terrainUUID != m_terrainUUID)
  {
    m_volume.reset();
  }

  m_terrainUUID = terrainUUID;
  invalidateBoundingBox();
}

bool Terrain::getCollisionEnabled() const
{
  return m_collisionEnabled;
}

void Terrain::setCollisionEnabled(const bool collisionEnabled)
{
  m_collisionEnabled = collisionEnabled;
}

bool Terrain::getCastShadows() const
{
  return m_castShadows;
}

void Terrain::setCastShadows(const bool castShadows)
{
  m_castShadows = castShadows;
}

void Terrain::setVolume(std::weak_ptr<const terrain::TerrainVolume> volume)
{
  m_volume = std::move(volume);
  invalidateBoundingBox();
}

std::shared_ptr<const terrain::TerrainVolume> Terrain::volume() const
{
  return m_volume.lock();
}

void Terrain::invalidateBounds()
{
  invalidateBoundingBox();
}

nlohmann::json Terrain::serialize()
{
  return {
    { "type", "Collider" },
    { "subType", "Terrain" },
    { "terrainUUID", m_terrainUUID.is_nil() ? "" : uuids::to_string(m_terrainUUID) },
    { "collisionEnabled", m_collisionEnabled },
    { "castShadows", m_castShadows },
    { "renderCollider", m_renderCollider },
    { "isTrigger", m_isTrigger },
    { "layer", m_layer },
    { "mask", m_mask }
  };
}

void Terrain::loadFromJSON(const nlohmann::json& componentData)
{
  if (!componentData.is_object())
  {
    throw RefusedPayload("Terrain: payload is not an object");
  }

  Data data;
  data.terrainUUID = readTerrainUUID(componentData);
  data.collisionEnabled = readBool(componentData, "collisionEnabled", true);
  data.castShadows = readBool(componentData, "castShadows", true);
  data.renderCollider = readBool(componentData, "renderCollider", false);
  data.layer = readUnsigned(componentData, "layer", 0u);
  data.mask = readUnsigned(componentData, "mask", 0xFFFFFFFFu);
  requireNotTrigger(readBool(componentData, "isTrigger", false));

  assign(data);
}

glm::vec3 Terrain::findFurthestPoint(const glm::vec3& direction)
{
  Transform& transform = cachedTransform();

  if (!refreshCorners(transform))
  {
    return transform.getPosition();
  }

  float largestDot = std::numeric_limits<float>::lowest();
  glm::vec3 furthest = m_corners[0];

  for (const glm::vec3& corner : m_corners)
  {
    if (const float currentDot = glm::dot(corner, direction); currentDot > largestDot)
    {
      largestDot = currentDot;
      furthest = corner;
    }
  }

  return furthest;
}

bool Terrain::refreshCorners(Transform& transform)
{
  const auto volume = m_volume.lock();
  if (!volume)
  {
    m_cornersValid = false;
    return false;
  }

  const uint64_t updateID = transform.getWorldUpdateID();
  const uint64_t revision = getGeometryRevision();

  if (m_cornersValid && m_cornersUpdateID == updateID && m_cornersRevision == revision
      && m_cornersVolume == volume.get())
  {
    return true;
  }

  const terrain::SampleBox bounds = volume->occupiedBounds();
  if (bounds.empty())
  {
    m_cornersValid = false;
    return false;
  }

  // Terrain space is metric (a sample sits at voxelSize * index), so the owner's scale does not apply.
  const glm::vec3 low = glm::vec3(bounds.min) * volume->voxelSize();
  const glm::vec3 high = glm::vec3(bounds.max) * volume->voxelSize();
  const glm::vec3 origin = transform.getPosition();
  const glm::quat orientation = transform.getOrientation();

  for (int corner = 0; corner < 8; ++corner)
  {
    const glm::vec3 local((corner & 1) != 0 ? high.x : low.x, (corner & 2) != 0 ? high.y : low.y,
                          (corner & 4) != 0 ? high.z : low.z);
    m_corners[corner] = origin + orientation * local;
  }

  m_cornersUpdateID = updateID;
  m_cornersRevision = revision;
  m_cornersVolume = volume.get();
  m_cornersValid = true;

  return true;
}

void Terrain::pack(net::Message& message) const
{
  message.write(ComponentType::SubComponentType_terrain);

  message.write(m_renderCollider);
  message.write(m_terrainUUID);
  message.write(m_collisionEnabled);
  message.write(m_castShadows);
  message.write(m_isTrigger);
  message.write(m_layer);
  message.write(m_mask);
}

void Terrain::unpack(net::MessageReader& messageReader)
{
  Data data;
  data.renderCollider = messageReader.read<bool>();
  data.terrainUUID = messageReader.read<uuids::uuid>();
  data.collisionEnabled = messageReader.read<bool>();
  data.castShadows = messageReader.read<bool>();
  const bool isTrigger = messageReader.read<bool>();
  data.layer = messageReader.read<uint32_t>();
  data.mask = messageReader.read<uint32_t>();
  requireNotTrigger(isTrigger);

  assign(data);
}

void Terrain::assign(const Data& data)
{
  if (data.terrainUUID != m_terrainUUID)
  {
    m_volume.reset();
  }

  m_terrainUUID = data.terrainUUID;
  m_collisionEnabled = data.collisionEnabled;
  m_castShadows = data.castShadows;
  m_renderCollider = data.renderCollider;
  setLayer(data.layer);
  m_mask = data.mask;

  invalidateBoundingBox();
}
