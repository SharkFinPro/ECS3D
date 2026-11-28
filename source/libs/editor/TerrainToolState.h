#ifndef TERRAINTOOLSTATE_H
#define TERRAINTOOLSTATE_H

#include <terrain/TerrainBrush.h>
#include <terrain/TerrainStore.h>
#include <VulkanEngine/components/assets/volume/VolumeMaterial.h>
#include <glm/vec3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <uuid.h>

enum class TerrainTool : uint8_t { add, subtract, smooth, flatten, paint };

inline constexpr size_t terrainToolCount = 5;

[[nodiscard]] inline terrain::BrushOp::Kind brushKind(const TerrainTool tool)
{
  switch (tool)
  {
    case TerrainTool::subtract:
      return terrain::BrushOp::Kind::subtract;
    case TerrainTool::smooth:
      return terrain::BrushOp::Kind::smooth;
    case TerrainTool::flatten:
      return terrain::BrushOp::Kind::flatten;
    case TerrainTool::paint:
      return terrain::BrushOp::Kind::paint;
    case TerrainTool::add:
    default:
      return terrain::BrushOp::Kind::add;
  }
}

// The palette index a brush may use: index itself while the palette holds it, else the last entry. An empty palette
// draws the renderer's default colors by index, so 0 stays meaningful.
[[nodiscard]] inline uint8_t clampMaterial(const uint8_t index, const size_t paletteSize)
{
  if (index < paletteSize)
  {
    return index;
  }

  return paletteSize == 0 ? 0 : static_cast<uint8_t>(paletteSize - 1);
}

// The renderer draws this many default colors by index when a terrain has no palette.
inline constexpr size_t defaultMaterialCount = 8;
static_assert(defaultMaterialCount == vke::s_defaultVolumeColors.size());

// The swatch beside a Material entry: the renderer's default color for a terrain without a palette, which it picks by
// index modulo the palette size, and a neutral gray for a palette entry, whose look is its textures.
[[nodiscard]] inline glm::vec3 materialSwatchColor(const std::vector<terrain::TerrainMaterial>& palette,
                                                   const uint8_t index)
{
  return palette.empty() ? vke::s_defaultVolumeColors[index % vke::s_defaultVolumeColors.size()]
                         : glm::vec3(0.5f);
}

// For a row of buttons of the given widths, whether each one starts a new row because it does not fit beside the
// button before it in the available width. The first button never does.
[[nodiscard]] inline std::vector<bool> buttonRowBreaks(const std::vector<float>& widths, const float spacing,
                                                       const float available)
{
  std::vector<bool> breaks(widths.size(), false);
  float used = 0.0f;

  for (size_t index = 0; index < widths.size(); ++index)
  {
    if (index > 0 && used + spacing + widths[index] > available)
    {
      breaks[index] = true;
      used = 0.0f;
    }
    else if (index > 0)
    {
      used += spacing;
    }

    used += widths[index];
  }

  return breaks;
}

// How many materials the Material list offers: the palette's entries, or the renderer's defaults for an empty palette.
[[nodiscard]] inline size_t selectableMaterials(const std::vector<terrain::TerrainMaterial>& palette)
{
  return palette.empty() ? defaultMaterialCount : palette.size();
}

// The Material list's text for an index: "3: Rock", "3: Material 3" for an unnamed entry, "Default 3" without a
// palette.
[[nodiscard]] inline std::string materialLabel(const std::vector<terrain::TerrainMaterial>& palette,
                                              const uint8_t index)
{
  if (palette.empty())
  {
    return "Default " + std::to_string(index);
  }

  const std::string& name = palette[clampMaterial(index, palette.size())].name;
  return std::to_string(index) + ": " + (name.empty() ? "Material " + std::to_string(index) : name);
}

// Whether terrain mode may be on: an object with a Terrain component is selected, the server accepts edits and the
// scene is stopped (editing a running scene is not supported).
struct TerrainModeContext
{
  std::optional<uuids::uuid> selectedTerrainObject;
  bool editable = false;
  bool sceneStopped = true;
};

class TerrainToolState
{
public:
  [[nodiscard]] bool active() const
  {
    return m_terrainObject.has_value();
  }

  // Set exactly while active.
  [[nodiscard]] const std::optional<uuids::uuid>& terrainObject() const
  {
    return m_terrainObject;
  }

  [[nodiscard]] TerrainTool tool() const
  {
    return m_tool;
  }

  void setTool(const TerrainTool tool)
  {
    m_tool = tool;
  }

  // The palette index Add's new ground and Paint write; a session choice that outlives the mode.
  [[nodiscard]] uint8_t activeMaterial() const
  {
    return m_activeMaterial;
  }

  void setActiveMaterial(const uint8_t material)
  {
    m_activeMaterial = material;
  }

  // Turns the mode on for the context's object; refused (returns false, stays off) when the context is not eligible.
  bool enter(const TerrainModeContext& context)
  {
    if (!eligible(context))
    {
      return false;
    }

    m_terrainObject = context.selectedTerrainObject;
    return true;
  }

  void exit()
  {
    m_terrainObject.reset();
  }

  // enter when off, exit when on. Returns active().
  bool toggle(const TerrainModeContext& context)
  {
    if (active())
    {
      exit();
    }
    else
    {
      enter(context);
    }

    return active();
  }

  // Called every frame. Returns true when this call left the mode.
  bool refresh(const TerrainModeContext& context)
  {
    if (!active())
    {
      return false;
    }

    if (eligible(context) && context.selectedTerrainObject == m_terrainObject)
    {
      return false;
    }

    exit();
    return true;
  }

private:
  [[nodiscard]] static bool eligible(const TerrainModeContext& context)
  {
    return context.editable && context.sceneStopped && context.selectedTerrainObject.has_value();
  }

  std::optional<uuids::uuid> m_terrainObject;
  TerrainTool m_tool = TerrainTool::add;
  uint8_t m_activeMaterial = 0;
};

#endif //TERRAINTOOLSTATE_H
