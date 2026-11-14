#include "TerrainBrush.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain
{
  namespace
  {
    bool finite(const glm::vec3& value)
    {
      return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool validFields(const BrushOp& op)
    {
      return static_cast<uint8_t>(op.kind) <= static_cast<uint8_t>(BrushOp::Kind::paint) && finite(op.center) &&
             finite(op.planePoint) && finite(op.planeNormal) && std::isfinite(op.radius) &&
             std::isfinite(op.strength) && std::isfinite(op.falloff) && op.radius > 0.0f && op.strength >= 0.0f &&
             op.strength <= 1.0f && op.falloff >= 0.0f && op.falloff <= 1.0f && op.material != reservedMaterial;
    }

    bool validFor(const TerrainVolume& volume, const BrushOp& op)
    {
      if (!validFields(op))
      {
        return false;
      }

      const float voxelSize = volume.voxelSize();
      const float reach = static_cast<float>(maxChunkCoord - 2) * static_cast<float>(chunkSize);

      if (op.radius > maxBrushRadiusVoxels * voxelSize ||
          glm::any(glm::greaterThan(glm::abs(op.center) / voxelSize, glm::vec3(reach))))
      {
        return false;
      }

      return op.kind != BrushOp::Kind::flatten || glm::length(op.planeNormal) > 1.0e-6f;
    }

    float weightAt(const BrushOp& op, const float voxelSize, const float distance)
    {
      const float inner = op.radius * (1.0f - op.falloff);

      if (distance <= inner)
      {
        return op.strength;
      }

      const float t = std::clamp((distance - inner) / (op.radius + voxelSize - inner), 0.0f, 1.0f);
      return op.strength * (1.0f - t * t * (3.0f - 2.0f * t));
    }

    // Dense float copy of the field over a box.
    struct Field
    {
      glm::ivec3 min{ 0 };
      glm::ivec3 size{ 0 };
      std::vector<float> values;

      Field(const glm::ivec3 low, const glm::ivec3 high)
        : min(low), size(high - low),
          values(static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * static_cast<size_t>(size.z), 0.0f)
      {}

      [[nodiscard]] size_t at(const int x, const int y, const int z) const
      {
        return static_cast<size_t>(x - min.x) +
               static_cast<size_t>(size.x) *
                 (static_cast<size_t>(y - min.y) + static_cast<size_t>(size.y) * static_cast<size_t>(z - min.z));
      }
    };

    // Three-tap box blur along one axis, written for every sample in rows.
    void blurAxis(const Field& source, Field& target, const SampleBox& rows, const int axis)
    {
      const glm::ivec3 step(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0);

      for (int z = rows.min.z; z < rows.max.z; ++z)
      {
        for (int y = rows.min.y; y < rows.max.y; ++y)
        {
          for (int x = rows.min.x; x < rows.max.x; ++x)
          {
            target.values[target.at(x, y, z)] =
              (source.values[source.at(x - step.x, y - step.y, z - step.z)] + source.values[source.at(x, y, z)] +
               source.values[source.at(x + step.x, y + step.y, z + step.z)]) / 3.0f;
          }
        }
      }
    }

    // The margin of one sample on every side feeds the blur passes at the box edge.
    Field blurredField(const TerrainVolume& volume, const SampleBox& box)
    {
      const SampleBox grown{ box.min - glm::ivec3(1), box.max + glm::ivec3(1) };
      Field raw(grown.min, grown.max);

      for (int z = grown.min.z; z < grown.max.z; ++z)
      {
        for (int y = grown.min.y; y < grown.max.y; ++y)
        {
          for (int x = grown.min.x; x < grown.max.x; ++x)
          {
            raw.values[raw.at(x, y, z)] = decodeDistance(volume.distanceAtSample({ x, y, z }));
          }
        }
      }

      Field scratch(grown.min, grown.max);

      blurAxis(raw, scratch, { { box.min.x, grown.min.y, grown.min.z }, { box.max.x, grown.max.y, grown.max.z } }, 0);
      blurAxis(scratch, raw, { { box.min.x, box.min.y, grown.min.z }, { box.max.x, box.max.y, grown.max.z } }, 1);
      blurAxis(raw, scratch, box, 2);

      return scratch;
    }
  }

  SampleBox dirtyBox(const TerrainVolume& volume, const BrushOp& op)
  {
    if (!validFor(volume, op))
    {
      return {};
    }

    const float voxelSize = volume.voxelSize();
    const float influence = op.radius + voxelSize;
    const glm::vec3 low = glm::floor((op.center - glm::vec3(influence)) / voxelSize);
    const glm::vec3 high = glm::ceil((op.center + glm::vec3(influence)) / voxelSize);

    return { glm::ivec3(low), glm::ivec3(high) + glm::ivec3(1) };
  }

  ChunkEdits applyBrush(const TerrainVolume& volume, const BrushOp& op)
  {
    const SampleBox box = dirtyBox(volume, op);

    if (box.empty())
    {
      return {};
    }

    const float voxelSize = volume.voxelSize();
    const float influence = op.radius + voxelSize;
    const glm::vec3 normal = op.kind == BrushOp::Kind::flatten ? glm::normalize(op.planeNormal) : glm::vec3(0.0f);
    const Field blurred = op.kind == BrushOp::Kind::smooth ? blurredField(volume, box)
                                                           : Field(glm::ivec3(0), glm::ivec3(0));

    return editSamples(volume, box, [&](const glm::ivec3 sample, int16_t& distance, uint8_t& material)
    {
      const glm::vec3 position = glm::vec3(sample) * voxelSize;
      const float reach = glm::length(position - op.center);

      if (reach > influence)
      {
        return;
      }

      const float weight = weightAt(op, voxelSize, reach);
      const float before = decodeDistance(distance);
      const float sphere = (reach - op.radius) / voxelSize;
      bool paintsRaisedGround = false;
      float target = before;

      switch (op.kind)
      {
        case BrushOp::Kind::add:
          target = std::min(before, sphere);
          paintsRaisedGround = true;
          break;
        case BrushOp::Kind::subtract:
          target = std::max(before, -sphere);
          break;
        case BrushOp::Kind::smooth:
          target = blurred.values[blurred.at(sample.x, sample.y, sample.z)];
          break;
        case BrushOp::Kind::flatten:
          target = glm::dot(position - op.planePoint, normal) / voxelSize;
          paintsRaisedGround = true;
          break;
        case BrushOp::Kind::paint:
          if (reach <= op.radius && weight > 0.0f && std::abs(before) <= 1.0f)
          {
            material = op.material;
          }
          return;
      }

      const int16_t after = encodeDistance(before + weight * (target - before));

      if (paintsRaisedGround && distance >= 0 && after < 0)
      {
        material = op.material;
      }

      distance = after;
    });
  }

  void pack(net::Message& message, const BrushOp& op)
  {
    message.write(static_cast<uint8_t>(op.kind));
    message.write(op.center.x).write(op.center.y).write(op.center.z);
    message.write(op.radius).write(op.strength).write(op.falloff);
    message.write(op.material);
    message.write(op.planePoint.x).write(op.planePoint.y).write(op.planePoint.z);
    message.write(op.planeNormal.x).write(op.planeNormal.y).write(op.planeNormal.z);
  }

  std::optional<BrushOp> unpackBrushOp(net::MessageReader& reader)
  {
    BrushOp op;
    const auto kind = reader.read<uint8_t>();
    op.center.x = reader.read<float>();
    op.center.y = reader.read<float>();
    op.center.z = reader.read<float>();
    op.radius = reader.read<float>();
    op.strength = reader.read<float>();
    op.falloff = reader.read<float>();
    op.material = reader.read<uint8_t>();
    op.planePoint.x = reader.read<float>();
    op.planePoint.y = reader.read<float>();
    op.planePoint.z = reader.read<float>();
    op.planeNormal.x = reader.read<float>();
    op.planeNormal.y = reader.read<float>();
    op.planeNormal.z = reader.read<float>();

    if (kind > static_cast<uint8_t>(BrushOp::Kind::paint))
    {
      return std::nullopt;
    }

    op.kind = static_cast<BrushOp::Kind>(kind);
    return validFields(op) ? std::optional<BrushOp>(op) : std::nullopt;
  }
}
