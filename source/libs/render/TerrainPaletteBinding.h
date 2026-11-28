#ifndef TERRAINPALETTEBINDING_H
#define TERRAINPALETTEBINDING_H

#include <terrain/TerrainStore.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <uuid.h>

// Mirrors vke::s_maxVolumeMaterials; GpuAssetCache checks they agree.
inline constexpr std::size_t maxTerrainPaletteMaterials = 16;

// One repeat per 10 world units, which is 2 m at the engine's 5 units per meter.
inline constexpr float terrainTextureRepeatsPerUnit = 0.1f;

template <typename Texture>
struct TerrainMaterialHandles {
  std::shared_ptr<Texture> albedo;
  std::shared_ptr<Texture> specular;
  float textureScale = terrainTextureRepeatsPerUnit;

  [[nodiscard]] bool operator==(const TerrainMaterialHandles&) const = default;
};

template <typename Texture>
using TerrainPaletteHandles = std::vector<TerrainMaterialHandles<Texture>>;

// A nil uuid is no texture (null, drawn white). A uuid the resolver cannot resolve draws the placeholder, which is
// asked for only then, and is passed to onMissing. Entries past the engine's limit are dropped.
template <typename Texture, typename Resolver, typename Placeholder, typename OnMissing>
[[nodiscard]] TerrainPaletteHandles<Texture> resolveTerrainPalette(std::span<const terrain::TerrainMaterial> palette,
                                                                   const Resolver& resolve,
                                                                   const Placeholder& placeholder,
                                                                   const OnMissing& onMissing)
{
  const auto slot = [&](const uuids::uuid& uuid) -> std::shared_ptr<Texture> {
    if (uuid.is_nil())
    {
      return nullptr;
    }

    if (auto texture = resolve(uuid))
    {
      return texture;
    }

    onMissing(uuid);
    return placeholder();
  };

  TerrainPaletteHandles<Texture> resolved;
  const std::size_t count = palette.size() < maxTerrainPaletteMaterials ? palette.size() : maxTerrainPaletteMaterials;
  resolved.reserve(count);

  for (std::size_t i = 0; i < count; ++i)
  {
    resolved.push_back({ slot(palette[i].albedoTexture), slot(palette[i].specularTexture),
                         terrainTextureRepeatsPerUnit });
  }

  return resolved;
}

template <typename Texture>
[[nodiscard]] bool samePalette(const TerrainPaletteHandles<Texture>& a, const TerrainPaletteHandles<Texture>& b)
{
  return a == b;
}

// What a terrain's palette was last resolved from. Resolving loads and looks up every texture, so an input that is
// unchanged and resolved without a placeholder is not resolved again; one that used a placeholder is, because that
// is how a texture that arrives later is noticed.
class TerrainPaletteInputMemory {
public:
  [[nodiscard]] bool inputChanged(const std::span<const terrain::TerrainMaterial> input) const
  {
    return !m_known || !std::ranges::equal(input, m_input);
  }

  [[nodiscard]] bool needsResolve(const std::span<const terrain::TerrainMaterial> input) const
  {
    return inputChanged(input) || m_usedPlaceholder;
  }

  void record(const std::span<const terrain::TerrainMaterial> input, const bool usedPlaceholder)
  {
    m_input.assign(input.begin(), input.end());
    m_usedPlaceholder = usedPlaceholder;
    m_known = true;
  }

private:
  std::vector<terrain::TerrainMaterial> m_input;
  bool m_usedPlaceholder = false;
  bool m_known = false;
};

// What one surface was last given. A surface starts on the engine's default palette, which an empty palette
// matches, so only a changed palette (or a new surface that needs a non-empty one) costs a texture-array build. A
// palette is recorded when it is tried, so one the engine rejects is not retried until the palette changes.
template <typename Texture>
class TerrainPaletteApplication {
public:
  [[nodiscard]] bool needsApply(const TerrainPaletteHandles<Texture>& resolved) const
  {
    return !samePalette(m_applied, resolved);
  }

  void markApplied(TerrainPaletteHandles<Texture> resolved)
  {
    m_applied = std::move(resolved);
  }

  void reset()
  {
    m_applied.clear();
  }

private:
  TerrainPaletteHandles<Texture> m_applied;
};

#endif //TERRAINPALETTEBINDING_H
