#include "TerrainBrush.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
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

    // Keeps the dirty box, in exact arithmetic, inside the valid chunk range.
    constexpr double centerBoundSamples =
      static_cast<double>(maxChunkCoord - 1) * chunkSize - (maxBrushRadiusVoxels + brushBorderVoxels + 2.0);

    bool withinBound(const glm::vec3& position, const float voxelSize)
    {
      const double size = voxelSize;
      return std::abs(static_cast<double>(position.x)) / size <= centerBoundSamples &&
             std::abs(static_cast<double>(position.y)) / size <= centerBoundSamples &&
             std::abs(static_cast<double>(position.z)) / size <= centerBoundSamples;
    }

    bool validFor(const TerrainVolume& volume, const BrushOp& op)
    {
      if (!validFields(op))
      {
        return false;
      }

      const float voxelSize = volume.voxelSize();

      if (op.radius > maxBrushRadiusVoxels * voxelSize || !withinBound(op.center, voxelSize))
      {
        return false;
      }

      return op.kind != BrushOp::Kind::flatten ||
             (withinBound(op.planePoint, voxelSize) && glm::length(glm::dvec3(op.planeNormal)) > 1.0e-6);
    }

    // The ramp ends at the influence radius so that a falloff of zero keeps full weight across the whole border.
    float weightAt(const BrushOp& op, const float influence, const float distance)
    {
      const float inner = influence * (1.0f - op.falloff);

      if (distance <= inner)
      {
        return op.strength;
      }

      const float t = std::clamp((distance - inner) / (influence - inner), 0.0f, 1.0f);
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
      std::vector<int16_t> encoded(raw.values.size());

      volume.readDistances(grown, encoded);

      for (size_t index = 0; index < encoded.size(); ++index)
      {
        raw.values[index] = decodeDistance(encoded[index]);
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

    const double voxelSize = volume.voxelSize();
    const double influence = op.radius + brushBorderVoxels * voxelSize;
    glm::ivec3 low;
    glm::ivec3 high;

    for (int axis = 0; axis < 3; ++axis)
    {
      const double center = op.center[axis];
      low[axis] = static_cast<int>(std::floor((center - influence) / voxelSize));
      high[axis] = static_cast<int>(std::ceil((center + influence) / voxelSize)) + 1;
    }

    return { low, high };
  }

  ChunkEdits applyBrush(const TerrainVolume& volume, const BrushOp& op)
  {
    const SampleBox box = dirtyBox(volume, op);

    if (box.empty())
    {
      return {};
    }

    const float voxelSize = volume.voxelSize();
    const float influence = op.radius + brushBorderVoxels * voxelSize;
    const glm::vec3 normal =
      op.kind == BrushOp::Kind::flatten ? glm::vec3(glm::normalize(glm::dvec3(op.planeNormal))) : glm::vec3(0.0f);
    const Field blurred = op.kind == BrushOp::Kind::smooth ? blurredField(volume, box)
                                                           : Field(glm::ivec3(0), glm::ivec3(0));

    const float influenceSquared = influence * influence;

    const auto editRow = [&](const glm::ivec3 first, const int count, int16_t* distances, uint8_t* materials)
    {
      const float positionY = static_cast<float>(first.y) * voxelSize;
      const float positionZ = static_cast<float>(first.z) * voxelSize;
      const float offsetY = positionY - op.center.y;
      const float offsetZ = positionZ - op.center.z;
      const float rest = influenceSquared * 1.001f - (offsetY * offsetY + offsetZ * offsetZ);

      // The row's samples inside the influence sphere form one run; the margin keeps rounding from trimming a sample
      // the exact test below would keep.
      if (rest < 0.0f)
      {
        return false;
      }

      const float halfRun = std::sqrt(rest) / voxelSize + 1.0f;
      const int runFirst = std::max(first.x, static_cast<int>(std::floor(op.center.x / voxelSize - halfRun)));
      const int runEnd = std::min(first.x + count, static_cast<int>(std::ceil(op.center.x / voxelSize + halfRun)) + 1);
      bool touched = false;

      for (int x = runFirst; x < runEnd; ++x)
      {
        const float positionX = static_cast<float>(x) * voxelSize;
        const float offsetX = positionX - op.center.x;
        const float reachSquared = offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ;

        if (reachSquared > influenceSquared)
        {
          continue;
        }

        const float reach = std::sqrt(reachSquared);

        if (reach > influence)
        {
          continue;
        }

        const float weight = weightAt(op, influence, reach);

        if (weight <= 0.0f)
        {
          continue;
        }

        const int offset = x - first.x;
        const int16_t distance = distances[offset];
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
            target = blurred.values[blurred.at(x, first.y, first.z)];
            break;
          case BrushOp::Kind::flatten:
            target = glm::dot(glm::vec3(positionX, positionY, positionZ) - op.planePoint, normal) / voxelSize;
            paintsRaisedGround = true;
            break;
          case BrushOp::Kind::paint:
            if (reach <= op.radius && std::abs(before) <= 1.0f)
            {
              materials[offset] = op.material;
              touched = true;
            }
            continue;
        }

        if (!std::isfinite(target))
        {
          continue;
        }

        const int16_t after = encodeDistance(before + weight * (target - before));

        if (paintsRaisedGround && distance >= 0 && after < 0)
        {
          materials[offset] = op.material;
        }

        distances[offset] = after;
        touched = true;
      }

      return touched;
    };

    return editRows(volume, box, editRow, {}, [&](const SampleBox& covered)
    {
      const glm::vec3 nearest = glm::clamp(op.center, glm::vec3(covered.min) * voxelSize,
                                           glm::vec3(covered.max - glm::ivec3(1)) * voxelSize);
      const glm::vec3 gap = nearest - op.center;

      // The margin keeps a rounding difference from skipping a sample the edit would reach.
      return glm::dot(gap, gap) > influence * influence * 1.001f;
    }, true);
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
