#ifndef TERRAINTOOLSTATE_H
#define TERRAINTOOLSTATE_H

#include <terrain/TerrainBrush.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <uuid.h>

enum class TerrainTool : uint8_t { add, subtract, smooth, flatten };

inline constexpr size_t terrainToolCount = 4;

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
    case TerrainTool::add:
    default:
      return terrain::BrushOp::Kind::add;
  }
}

// Whether terrain mode may be on: an object with a Terrain component is selected and the server accepts edits.
struct TerrainModeContext
{
  std::optional<uuids::uuid> selectedTerrainObject;
  bool editable = false;
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
    return context.editable && context.selectedTerrainObject.has_value();
  }

  std::optional<uuids::uuid> m_terrainObject;
  TerrainTool m_tool = TerrainTool::add;
};

#endif //TERRAINTOOLSTATE_H
