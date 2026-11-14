#include "TerrainChunk.h"
#include "TerrainTypes.h"
#include <algorithm>
#include <cassert>
#include <utility>

namespace terrain
{
  namespace
  {
    bool validSample(const int16_t distance, const uint8_t material)
    {
      return distance >= -airDistance && distance <= airDistance && material != reservedMaterial;
    }

    // The same 64-bit FNV-1a as fingerprintBytes (including its mapping of 0 to 1), fed value by value over the
    // canonical little-endian byte form, so equal content hashes equally whichever factory built the chunk and no
    // temporary buffer is needed.
    uint64_t hashContent(const std::vector<int16_t>& distances, const std::vector<uint8_t>& materials,
                         const bool uniformContent)
    {
      uint64_t hash = 14695981039346656037ULL;

      const auto feed = [&hash](const uint8_t byte)
      {
        hash ^= byte;
        hash *= 1099511628211ULL;
      };

      feed(uniformContent ? 0 : 1);

      for (const int16_t distance : distances)
      {
        const auto raw = static_cast<uint16_t>(distance);
        feed(static_cast<uint8_t>(raw & 0xFF));
        feed(static_cast<uint8_t>(raw >> 8));
      }

      for (const uint8_t material : materials)
      {
        feed(material);
      }

      return hash == 0 ? 1 : hash;
    }
  }

  TerrainChunk::TerrainChunk(std::vector<int16_t> distances, std::vector<uint8_t> materials, const bool uniformContent)
    : m_distances(std::move(distances)),
      m_materials(std::move(materials)),
      m_isUniform(uniformContent),
      m_hash(hashContent(m_distances, m_materials, uniformContent))
  {}

  std::shared_ptr<const TerrainChunk> TerrainChunk::uniform(const int16_t distance, const uint8_t material)
  {
    if (!validSample(distance, material))
    {
      return nullptr;
    }

    return std::shared_ptr<const TerrainChunk>(
      new TerrainChunk(std::vector<int16_t>{ distance }, std::vector<uint8_t>{ material }, true));
  }

  std::shared_ptr<const TerrainChunk> TerrainChunk::dense(std::vector<int16_t> distances,
                                                          std::vector<uint8_t> materials)
  {
    if (distances.size() != static_cast<size_t>(samplesPerChunk) ||
        materials.size() != static_cast<size_t>(samplesPerChunk))
    {
      return nullptr;
    }

    for (size_t i = 0; i < distances.size(); ++i)
    {
      if (!validSample(distances[i], materials[i]))
      {
        return nullptr;
      }
    }

    const int16_t firstDistance = distances.front();
    const uint8_t firstMaterial = materials.front();

    const bool equalDistances = std::ranges::all_of(distances, [firstDistance](const int16_t value)
    {
      return value == firstDistance;
    });
    const bool equalMaterials = std::ranges::all_of(materials, [firstMaterial](const uint8_t value)
    {
      return value == firstMaterial;
    });

    if (equalDistances && equalMaterials)
    {
      return uniform(firstDistance, firstMaterial);
    }

    return std::shared_ptr<const TerrainChunk>(new TerrainChunk(std::move(distances), std::move(materials), false));
  }

  bool TerrainChunk::isUniform() const
  {
    return m_isUniform;
  }

  bool TerrainChunk::isAir() const
  {
    return m_isUniform && m_distances.front() == airDistance;
  }

  int16_t TerrainChunk::distanceAt(const int x, const int y, const int z) const
  {
    return distanceAtIndex(sampleIndex(x, y, z));
  }

  uint8_t TerrainChunk::materialAt(const int x, const int y, const int z) const
  {
    return materialAtIndex(sampleIndex(x, y, z));
  }

  int16_t TerrainChunk::distanceAtIndex(const int index) const
  {
    assert(index >= 0 && index < samplesPerChunk);
    return m_isUniform ? m_distances.front() : m_distances[static_cast<size_t>(index)];
  }

  uint8_t TerrainChunk::materialAtIndex(const int index) const
  {
    assert(index >= 0 && index < samplesPerChunk);
    return m_isUniform ? m_materials.front() : m_materials[static_cast<size_t>(index)];
  }

  std::span<const int16_t> TerrainChunk::distances() const
  {
    return m_distances;
  }

  std::span<const uint8_t> TerrainChunk::materials() const
  {
    return m_materials;
  }

  uint64_t TerrainChunk::hash() const
  {
    return m_hash;
  }

  size_t TerrainChunk::byteSize() const
  {
    return sizeof(TerrainChunk) + m_distances.capacity() * sizeof(int16_t) + m_materials.capacity() * sizeof(uint8_t);
  }
}
