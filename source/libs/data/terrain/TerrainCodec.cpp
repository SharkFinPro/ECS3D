#include "TerrainCodec.h"
#include "TerrainTypes.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace terrain
{
  namespace
  {
    constexpr uint8_t uniformTag = 0;
    constexpr uint8_t denseTag = 1;
    constexpr size_t uniformBytes = 4;
    constexpr size_t maxLiteralRun = 128;
    constexpr size_t minRepeatRun = 3;
    constexpr size_t maxRepeatRun = 130;
    constexpr int repeatBias = 125;

    template <typename Value>
    void putValue(std::vector<uint8_t>& out, const Value value)
    {
      out.push_back(static_cast<uint8_t>(value & 0xFF));

      if constexpr (sizeof(Value) == 2)
      {
        out.push_back(static_cast<uint8_t>(value >> 8));
      }
    }

    template <typename Value>
    void encodeRuns(std::vector<uint8_t>& out, const std::vector<Value>& values)
    {
      const size_t count = values.size();
      size_t index = 0;

      const auto runLengthAt = [&](const size_t start)
      {
        size_t length = 1;

        while (start + length < count && length < maxRepeatRun && values[start + length] == values[start])
        {
          ++length;
        }

        return length;
      };

      while (index < count)
      {
        const size_t run = runLengthAt(index);

        if (run >= minRepeatRun)
        {
          out.push_back(static_cast<uint8_t>(run + repeatBias));
          putValue(out, values[index]);
          index += run;
          continue;
        }

        size_t literal = 0;

        while (index + literal < count && literal < maxLiteralRun &&
               !(index + literal != index && runLengthAt(index + literal) >= minRepeatRun))
        {
          ++literal;
        }

        out.push_back(static_cast<uint8_t>(literal - 1));

        for (size_t i = 0; i < literal; ++i)
        {
          putValue(out, values[index + i]);
        }

        index += literal;
      }
    }

    // Reads exactly samplesPerChunk values into a fixed buffer, so no size comes from the input.
    template <typename Value>
    bool decodeRuns(const std::span<const uint8_t> bytes, size_t& position, std::vector<Value>& values)
    {
      constexpr size_t width = sizeof(Value);
      const size_t total = static_cast<size_t>(samplesPerChunk);
      size_t filled = 0;

      const auto readValue = [&](const size_t at)
      {
        Value value = bytes[at];

        if constexpr (width == 2)
        {
          value = static_cast<Value>(value | (static_cast<Value>(bytes[at + 1]) << 8));
        }

        return value;
      };

      while (filled < total)
      {
        if (position >= bytes.size())
        {
          return false;
        }

        const uint8_t control = bytes[position++];

        if (control < 128)
        {
          const size_t length = static_cast<size_t>(control) + 1;

          if (filled + length > total || position + length * width > bytes.size())
          {
            return false;
          }

          for (size_t i = 0; i < length; ++i)
          {
            values[filled++] = readValue(position);
            position += width;
          }
        }
        else
        {
          const size_t length = static_cast<size_t>(control) - repeatBias;

          if (filled + length > total || position + width > bytes.size())
          {
            return false;
          }

          const Value value = readValue(position);
          position += width;
          std::fill_n(values.begin() + static_cast<std::ptrdiff_t>(filled), length, value);
          filled += length;
        }
      }

      return true;
    }

    bool validSample(const int16_t distance, const uint8_t material)
    {
      return distance >= -airDistance && distance <= airDistance && material != reservedMaterial;
    }
  }

  std::vector<uint8_t> encodeChunk(const TerrainChunk& chunk)
  {
    std::vector<uint8_t> out;

    if (chunk.isUniform())
    {
      out.reserve(uniformBytes);
      out.push_back(uniformTag);
      putValue(out, static_cast<uint16_t>(chunk.distances().front()));
      out.push_back(chunk.materials().front());
      return out;
    }

    out.push_back(denseTag);

    std::vector<uint16_t> deltas;
    deltas.reserve(static_cast<size_t>(samplesPerChunk));
    uint16_t previous = 0;

    for (const int16_t distance : chunk.distances())
    {
      const auto raw = static_cast<uint16_t>(distance);
      deltas.push_back(static_cast<uint16_t>(raw - previous));
      previous = raw;
    }

    encodeRuns(out, deltas);

    const std::span<const uint8_t> materials = chunk.materials();
    encodeRuns(out, std::vector<uint8_t>(materials.begin(), materials.end()));

    return out;
  }

  std::shared_ptr<const TerrainChunk> decodeChunk(const std::span<const uint8_t> bytes)
  {
    if (bytes.empty())
    {
      return nullptr;
    }

    if (bytes[0] == uniformTag)
    {
      if (bytes.size() != uniformBytes)
      {
        return nullptr;
      }

      const auto distance = static_cast<int16_t>(static_cast<uint16_t>(bytes[1] | (bytes[2] << 8)));
      const uint8_t material = bytes[3];

      if (!validSample(distance, material))
      {
        return nullptr;
      }

      return TerrainChunk::uniform(distance, material);
    }

    if (bytes[0] != denseTag)
    {
      return nullptr;
    }

    size_t position = 1;
    std::vector<uint16_t> deltas(static_cast<size_t>(samplesPerChunk));
    std::vector<uint8_t> materials(static_cast<size_t>(samplesPerChunk));

    if (!decodeRuns(bytes, position, deltas) || !decodeRuns(bytes, position, materials) || position != bytes.size())
    {
      return nullptr;
    }

    std::vector<int16_t> distances(static_cast<size_t>(samplesPerChunk));
    uint16_t running = 0;

    for (size_t i = 0; i < distances.size(); ++i)
    {
      running = static_cast<uint16_t>(running + deltas[i]);
      distances[i] = static_cast<int16_t>(running);

      if (!validSample(distances[i], materials[i]))
      {
        return nullptr;
      }
    }

    return TerrainChunk::dense(std::move(distances), std::move(materials));
  }
}
