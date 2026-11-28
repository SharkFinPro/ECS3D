#include "TerrainOps.h"
#include "TerrainParallel.h"
#include <glm/common.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    bool sameContent(const std::shared_ptr<const TerrainChunk>& before, const std::shared_ptr<const TerrainChunk>& after)
    {
      const bool beforeAir = !before || before->isAir();
      const bool afterAir = !after || after->isAir();

      if (beforeAir || afterAir)
      {
        return beforeAir == afterAir;
      }

      return before->isUniform() == after->isUniform() && std::ranges::equal(before->distances(), after->distances()) &&
             std::ranges::equal(before->materials(), after->materials());
    }

    constexpr int minParallelChunks = 4;
    constexpr int64_t minParallelSamples = 65536;

    // Runs body once per z slice, in parallel when the grid is big enough to repay it.
    template <typename Body>
    void forEachSlice(const int slices, const int64_t samples, const Body& body)
    {
      if (samples >= minParallelSamples)
      {
        parallelFor(slices, body);
        return;
      }

      for (int slice = 0; slice < slices; ++slice)
      {
        body(slice);
      }
    }

    bool withinEditLimits(const SampleBox& box)
    {
      const ChunkCoord first = chunkOfSample(box.min);
      const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

      if (!isValidChunkCoord(first) || !isValidChunkCoord(last))
      {
        return false;
      }

      const int64_t extentX = static_cast<int64_t>(last.x) - first.x + 1;
      const int64_t extentY = static_cast<int64_t>(last.y) - first.y + 1;
      const int64_t extentZ = static_cast<int64_t>(last.z) - first.z + 1;

      return extentX <= maxEditChunks && extentY <= maxEditChunks && extentZ <= maxEditChunks &&
             extentX * extentY * extentZ <= maxEditChunks;
    }

    // Godunov upwind solution of |grad u| = 1 with unit spacing from the three axis-neighbor minima.
    float solveEikonal(float a, float b, float c)
    {
      if (a > b)
      {
        std::swap(a, b);
      }

      if (b > c)
      {
        std::swap(b, c);
      }

      if (a > b)
      {
        std::swap(a, b);
      }

      float u = a + 1.0f;

      if (u > b)
      {
        u = 0.5f * (a + b + std::sqrt(std::max(0.0f, 2.0f - (a - b) * (a - b))));

        if (u > c)
        {
          const float sum = a + b + c;
          const float squares = a * a + b * b + c * c;
          u = (sum + std::sqrt(std::max(0.0f, sum * sum - 3.0f * (squares - 1.0f)))) / 3.0f;
        }
      }

      return u;
    }
  }

  void commit(TerrainVolume& volume, const ChunkEdits& edits)
  {
    for (const auto& edit : edits)
    {
      if (!isValidChunkCoord(edit.first))
      {
        throw std::out_of_range("terrain chunk coordinate out of range");
      }
    }

    for (const auto& edit : edits)
    {
      volume.setChunk(edit.first, edit.second);
    }
  }

  ChunkEdits editRows(const TerrainVolume& volume, const SampleBox& box, const RowEdit& editRow,
                      const UniformChunkHint& uniformHint, const ChunkSkip& skipChunk, const bool parallel)
  {
    if (box.empty())
    {
      return {};
    }

    if (!withinEditLimits(box))
    {
      return {};
    }

    const ChunkCoord first = chunkOfSample(box.min);
    const ChunkCoord last = chunkOfSample(box.max - glm::ivec3(1));

    std::vector<ChunkCoord> coords;

    for (int32_t cz = first.z; cz <= last.z; ++cz)
    {
      for (int32_t cy = first.y; cy <= last.y; ++cy)
      {
        for (int32_t cx = first.x; cx <= last.x; ++cx)
        {
          coords.push_back({ cx, cy, cz });
        }
      }
    }

    const auto processChunk = [&](const ChunkCoord coord) -> std::optional<ChunkEdits::value_type>
    {
      const auto current = volume.chunk(coord);
      const glm::ivec3 origin = chunkOrigin(coord);
      const glm::ivec3 low = glm::max(box.min, origin);
      const glm::ivec3 high = glm::min(box.max, origin + glm::ivec3(chunkSize));

      if (skipChunk && skipChunk({ low, high }))
      {
        return std::nullopt;
      }

      if (uniformHint && low == origin && high == origin + glm::ivec3(chunkSize))
      {
        int16_t hintDistance = 0;
        uint8_t hintMaterial = 0;

        if (uniformHint(coord, hintDistance, hintMaterial) && hintMaterial != reservedMaterial)
        {
          const auto filled = TerrainChunk::uniform(
            static_cast<int16_t>(std::clamp(static_cast<int>(hintDistance), -static_cast<int>(airDistance),
                                            static_cast<int>(airDistance))),
            hintMaterial);

          if (filled)
          {
            if (sameContent(current, filled))
            {
              return std::nullopt;
            }

            return ChunkEdits::value_type(coord, filled->isAir() ? nullptr : filled);
          }
        }
      }

      std::vector<int16_t> distances;
      std::vector<uint8_t> materials;

      if (!current)
      {
        distances.assign(samplesPerChunk, airDistance);
        materials.assign(samplesPerChunk, 0);
      }
      else if (current->isUniform())
      {
        distances.assign(samplesPerChunk, current->distances().front());
        materials.assign(samplesPerChunk, current->materials().front());
      }
      else
      {
        distances.assign(current->distances().begin(), current->distances().end());
        materials.assign(current->materials().begin(), current->materials().end());
      }

      bool changed = false;

      for (int z = low.z; z < high.z; ++z)
      {
        for (int y = low.y; y < high.y; ++y)
        {
          const auto row = static_cast<size_t>(sampleIndex(low.x - origin.x, y - origin.y, z - origin.z));

          changed = editRow({ low.x, y, z }, high.x - low.x, distances.data() + row, materials.data() + row) || changed;
        }
      }

      if (!changed)
      {
        return std::nullopt;
      }

      auto rebuilt = TerrainChunk::dense(std::move(distances), std::move(materials));

      if (!rebuilt || sameContent(current, rebuilt))
      {
        return std::nullopt;
      }

      return ChunkEdits::value_type(coord, rebuilt->isAir() ? nullptr : std::move(rebuilt));
    };

    std::vector<std::optional<ChunkEdits::value_type>> results(coords.size());
    const auto count = static_cast<int>(coords.size());
    const auto editChunk = [&](const int index)
    {
      results[static_cast<size_t>(index)] = processChunk(coords[static_cast<size_t>(index)]);
    };

    // Chunks are independent and written to their own slot, so the order and content of the result do not depend on
    // how many threads run.
    if (parallel && count >= minParallelChunks)
    {
      parallelFor(count, editChunk);
    }
    else
    {
      for (int index = 0; index < count; ++index)
      {
        editChunk(index);
      }
    }

    ChunkEdits edits;

    for (auto& result : results)
    {
      if (result)
      {
        edits.push_back(std::move(*result));
      }
    }

    return edits;
  }

  ChunkEdits editSamples(const TerrainVolume& volume, const SampleBox& box, const SampleEdit& edit,
                         const UniformChunkHint& uniformHint, const ChunkSkip& skipChunk, const bool parallel)
  {
    const auto editRow = [&edit](const glm::ivec3 first, const int count, int16_t* distances, uint8_t* materials)
    {
      bool changed = false;

      for (int offset = 0; offset < count; ++offset)
      {
        const uint8_t inputMaterial = materials[offset];
        const int16_t inputDistance = distances[offset];
        int16_t distance = inputDistance;
        uint8_t material = inputMaterial;

        edit({ first.x + offset, first.y, first.z }, distance, material);

        const auto outputDistance = static_cast<int16_t>(
          std::clamp(static_cast<int>(distance), -static_cast<int>(airDistance), static_cast<int>(airDistance)));
        const uint8_t outputMaterial = material == reservedMaterial ? inputMaterial : material;

        changed = changed || outputDistance != inputDistance || outputMaterial != inputMaterial;
        distances[offset] = outputDistance;
        materials[offset] = outputMaterial;
      }

      return changed;
    };

    return editRows(volume, box, editRow, uniformHint, skipChunk, parallel);
  }

  ChunkEdits redistance(const TerrainVolume& volume, const SampleBox& box)
  {
    if (box.empty() || !withinEditLimits(box))
    {
      return {};
    }

    const glm::ivec3 gridMin = box.min - glm::ivec3(1);
    const glm::ivec3 size = box.max - box.min + glm::ivec3(2);
    const int64_t total = static_cast<int64_t>(size.x) * size.y * size.z;

    if (total > maxRedistanceSamples)
    {
      return {};
    }

    const auto at = [&](const int x, const int y, const int z)
    {
      return static_cast<size_t>(x + size.x * (y + size.y * z));
    };

    std::vector<int16_t> encoded(static_cast<size_t>(total));
    std::vector<float> field(static_cast<size_t>(total));

    volume.readDistances({ gridMin, gridMin + size }, encoded);

    forEachSlice(size.z, total, [&](const int z)
    {
      const size_t begin = at(0, 0, z);
      const size_t end = begin + static_cast<size_t>(size.x) * static_cast<size_t>(size.y);

      for (size_t index = begin; index < end; ++index)
      {
        field[index] = decodeDistance(encoded[index]);
      }
    });

    std::vector<uint8_t> fixed(static_cast<size_t>(total), 0);

    // A sample is frozen when its 3x3x3 neighborhood holds both signs. Each sample starts as one bit for its sign (1
    // outside, 2 inside), and the window's bits are ORed one axis at a time, so a neighborhood holding both reads 3.
    std::vector<uint8_t> signs(static_cast<size_t>(total));

    forEachSlice(size.z, total, [&](const int z)
    {
      const size_t begin = at(0, 0, z);
      const size_t end = begin + static_cast<size_t>(size.x) * static_cast<size_t>(size.y);

      for (size_t index = begin; index < end; ++index)
      {
        signs[index] = encoded[index] < 0 ? 2 : 1;
      }
    });

    for (int axis = 0; axis < 3; ++axis)
    {
      const size_t step = axis == 0 ? 1 : (axis == 1 ? static_cast<size_t>(size.x)
                                                     : static_cast<size_t>(size.x) * static_cast<size_t>(size.y));
      const int extent = size[axis];
      const std::vector<uint8_t> previous = signs;

      forEachSlice(size.z, total, [&](const int z)
      {
        for (int y = 0; y < size.y; ++y)
        {
          for (int x = 0; x < size.x; ++x)
          {
            const size_t index = at(x, y, z);
            const int along = axis == 0 ? x : (axis == 1 ? y : z);
            uint8_t seen = previous[index];

            if (along > 0)
            {
              seen |= previous[index - step];
            }

            if (along < extent - 1)
            {
              seen |= previous[index + step];
            }

            signs[index] = seen;
          }
        }
      });
    }

    forEachSlice(size.z, total, [&](const int z)
    {
      for (int y = 0; y < size.y; ++y)
      {
        for (int x = 0; x < size.x; ++x)
        {
          const size_t index = at(x, y, z);
          const bool interior = x > 0 && y > 0 && z > 0 && x < size.x - 1 && y < size.y - 1 && z < size.z - 1;
          const bool freeze = !interior || encoded[index] == 0 || signs[index] == 3;

          fixed[index] = freeze ? 1 : 0;

          if (!freeze)
          {
            field[index] = encoded[index] < 0 ? -bandVoxels : bandVoxels;
          }
        }
      }
    });

    // Only a frozen sample inside the band can pull its neighbors below the band, and a value grows by at least
    // 1/sqrt(3) per step, so a sample farther than this from every such source keeps its starting value and the
    // sweeps need not visit it.
    constexpr int reach = 7;
    std::vector<uint8_t> sourceRow(static_cast<size_t>(size.y) * static_cast<size_t>(size.z), 0);
    std::vector<int> sliceMinX(static_cast<size_t>(size.z), size.x);
    std::vector<int> sliceMaxX(static_cast<size_t>(size.z), -1);

    forEachSlice(size.z, total, [&](const int z)
    {
      for (int y = 0; y < size.y; ++y)
      {
        for (int x = 0; x < size.x; ++x)
        {
          const size_t index = at(x, y, z);

          if (fixed[index] != 0 && std::abs(field[index]) < bandVoxels)
          {
            sourceRow[static_cast<size_t>(z) * static_cast<size_t>(size.y) + static_cast<size_t>(y)] = 1;
            sliceMinX[static_cast<size_t>(z)] = std::min(sliceMinX[static_cast<size_t>(z)], x);
            sliceMaxX[static_cast<size_t>(z)] = std::max(sliceMaxX[static_cast<size_t>(z)], x);
          }
        }
      }
    });

    const int sourceMinX = *std::ranges::min_element(sliceMinX);
    const int sourceMaxX = *std::ranges::max_element(sliceMaxX);

    std::vector<uint8_t> activeRow(sourceRow.size(), 0);

    for (int z = 0; z < size.z; ++z)
    {
      for (int y = 0; y < size.y; ++y)
      {
        bool closeToSource = false;

        for (int dz = std::max(z - reach, 0); dz <= std::min(z + reach, size.z - 1) && !closeToSource; ++dz)
        {
          for (int dy = std::max(y - reach, 0); dy <= std::min(y + reach, size.y - 1) && !closeToSource; ++dy)
          {
            closeToSource = sourceRow[static_cast<size_t>(dz) * static_cast<size_t>(size.y) + static_cast<size_t>(dy)] != 0;
          }
        }

        activeRow[static_cast<size_t>(z) * static_cast<size_t>(size.y) + static_cast<size_t>(y)] = closeToSource ? 1 : 0;
      }
    }

    const int sweepLow = std::max(1, sourceMinX - reach);
    const int sweepHigh = std::min(size.x - 2, sourceMaxX + reach);

    // Each pass computes every sample from the previous pass's values only, so slices can run in parallel and the
    // result does not depend on thread count or order. Magnitudes only shrink and a value moves one sample per pass,
    // so it settles within a handful of passes, at the same fixed point a sweeping order converges to.
    constexpr int maxPasses = 64;
    std::vector<float> nextField = field;
    std::vector<uint8_t> sliceChanged(static_cast<size_t>(size.z));

    // A row whose own values and whose neighbor rows' values did not change in the last pass reads the same inputs
    // again, so it cannot change; such rows are skipped, which keeps both buffers equal there.
    const auto rowIndex = [&size](const int y, const int z)
    {
      return static_cast<size_t>(z) * static_cast<size_t>(size.y) + static_cast<size_t>(y);
    };
    std::vector<uint8_t> rowChanged(activeRow.size(), 0);
    std::vector<uint8_t> nextRowChanged(activeRow.size(), 0);

    for (int z = 1; z < size.z - 1; ++z)
    {
      for (int y = 1; y < size.y - 1; ++y)
      {
        rowChanged[rowIndex(y, z)] = activeRow[rowIndex(y, z)];
      }
    }

    for (int pass = 0; pass < maxPasses && sourceMaxX >= 0; ++pass)
    {
      std::ranges::fill(sliceChanged, 0);

      forEachSlice(size.z, total, [&](const int z)
      {
        if (z < 1 || z > size.z - 2)
        {
          return;
        }

        bool any = false;

        for (int y = 1; y < size.y - 1; ++y)
        {
          nextRowChanged[rowIndex(y, z)] = 0;

          if (activeRow[rowIndex(y, z)] == 0)
          {
            continue;
          }

          bool neighborChanged = false;

          for (int dz = -1; dz <= 1 && !neighborChanged; ++dz)
          {
            for (int dy = -1; dy <= 1 && !neighborChanged; ++dy)
            {
              neighborChanged = rowChanged[rowIndex(y + dy, z + dz)] != 0;
            }
          }

          if (!neighborChanged)
          {
            continue;
          }

          bool rowAny = false;

          for (int x = sweepLow; x <= sweepHigh; ++x)
          {
            const size_t index = at(x, y, z);

            if (fixed[index] != 0)
            {
              continue;
            }

            const float current = field[index];
            const float a = std::min(std::abs(field[at(x - 1, y, z)]), std::abs(field[at(x + 1, y, z)]));
            const float b = std::min(std::abs(field[at(x, y - 1, z)]), std::abs(field[at(x, y + 1, z)]));
            const float c = std::min(std::abs(field[at(x, y, z - 1)]), std::abs(field[at(x, y, z + 1)]));

            // The upwind solution is at least the smallest neighbor plus 1/sqrt(3), so a sample already within
            // that of its smallest neighbor cannot improve.
            if (std::abs(current) <= std::min(a, std::min(b, c)) + 0.5f)
            {
              nextField[index] = current;
              continue;
            }

            const float magnitude = std::min(std::abs(current), solveEikonal(a, b, c));
            const float value = std::signbit(current) ? -magnitude : magnitude;

            nextField[index] = value;
            rowAny = rowAny || value != current;
          }

          nextRowChanged[rowIndex(y, z)] = rowAny ? 1 : 0;
          any = any || rowAny;
        }

        sliceChanged[static_cast<size_t>(z)] = any ? 1 : 0;
      });

      field.swap(nextField);
      rowChanged.swap(nextRowChanged);

      if (std::ranges::none_of(sliceChanged, [](const uint8_t changed) { return changed != 0; }))
      {
        break;
      }
    }

    return editRows(volume, box, [&](const glm::ivec3 first, const int count, int16_t* distances, uint8_t*)
    {
      const glm::ivec3 local = first - gridMin;
      const size_t base = at(local.x, local.y, local.z);
      bool touched = false;

      for (int offset = 0; offset < count; ++offset)
      {
        const size_t index = base + static_cast<size_t>(offset);

        if (fixed[index] == 0 && field[index] != decodeDistance(distances[offset]))
        {
          distances[offset] = encodeDistance(field[index]);
          touched = true;
        }
      }

      return touched;
    }, {}, {}, true);
  }
}
