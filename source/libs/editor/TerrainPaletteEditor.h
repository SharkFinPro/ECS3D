#ifndef TERRAINPALETTEEDITOR_H
#define TERRAINPALETTEEDITOR_H

#include <terrain/TerrainPalette.h>
#include <terrain/TerrainStore.h>
#include <VulkanEngine/components/assets/volume/VolumeMaterial.h>
#include <glm/vec3.hpp>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

// The decisions behind the Terrain inspector's Materials block, with no ImGui: a UI intent plus the current palette
// becomes a new palette or the reason it was refused. The caller draws, sends and records.
class TerrainPaletteEditor {
public:
  struct Context {
    bool editable = false;
    bool sceneStopped = true;
    bool terrainKnown = false;
  };

  struct Result {
    std::vector<terrain::TerrainMaterial> palette;
    std::optional<std::string> refusal;

    [[nodiscard]] bool ok() const
    {
      return !refusal.has_value();
    }
  };

  // Why the palette cannot be edited now, for a tooltip, or nullopt when it can. Palette edits during Play would
  // survive Stop on the server while the play history is discarded, so they wait for a stopped scene.
  [[nodiscard]] static std::optional<std::string> editRefusal(const Context& context)
  {
    if (!context.editable)
    {
      return "The connected server is read-only.";
    }

    if (!context.sceneStopped)
    {
      return "Stop the scene to edit materials.";
    }

    if (!context.terrainKnown)
    {
      return "This terrain has not arrived from the server yet.";
    }

    return std::nullopt;
  }

  [[nodiscard]] static std::string defaultName(const size_t index)
  {
    return "Material " + std::to_string(index);
  }

  // New entries cycle through the renderer's default colors, so neighbors differ.
  [[nodiscard]] static glm::vec3 defaultColor(const size_t index)
  {
    return vke::s_defaultVolumeColors[index % vke::s_defaultVolumeColors.size()];
  }

  [[nodiscard]] static Result add(const std::vector<terrain::TerrainMaterial>& palette)
  {
    return run(palette, [](auto& copy) {
      return terrain::addMaterial(copy, { defaultName(copy.size()), {}, {}, defaultColor(copy.size()) });
    });
  }

  // A material is a flat color while it has no albedo texture; a row switched to Texture before one is picked is
  // tracked by the UI, since the palette cannot hold it.
  [[nodiscard]] static bool isColorMode(const terrain::TerrainMaterial& material)
  {
    return material.albedoTexture.is_nil();
  }

  [[nodiscard]] static Result setColor(const std::vector<terrain::TerrainMaterial>& palette, const size_t index,
                                       const glm::vec3& color)
  {
    return run(palette, [&](auto& copy) {
      return terrain::setMaterialColor(copy, index, color);
    });
  }

  // Switching to Color drops the albedo texture and keeps the specular one. The color is never cleared, so
  // switching back to Texture shows the same color again later.
  [[nodiscard]] static Result useColor(const std::vector<terrain::TerrainMaterial>& palette, const size_t index)
  {
    return run(palette, [&](auto& copy) {
      return terrain::setMaterialTextures(copy, index, uuids::uuid{},
                                          index < copy.size() ? copy[index].specularTexture : uuids::uuid{});
    });
  }

  // inUse is read only when the intent is a remove, since computing it walks every chunk the terrain holds.
  template <typename InUseFn>
  [[nodiscard]] static Result remove(const std::vector<terrain::TerrainMaterial>& palette, const size_t index,
                                     InUseFn&& inUse)
  {
    return run(palette, [&](auto& copy) {
      return terrain::removeMaterial(copy, index, inUse());
    });
  }

  [[nodiscard]] static Result move(const std::vector<terrain::TerrainMaterial>& palette, const size_t from,
                                   const size_t to)
  {
    return run(palette, [&](auto& copy) {
      return terrain::moveMaterial(copy, from, to);
    });
  }

  [[nodiscard]] static Result rename(const std::vector<terrain::TerrainMaterial>& palette, const size_t index,
                                     const std::string& name)
  {
    return run(palette, [&](auto& copy) {
      return terrain::renameMaterial(copy, index, name);
    });
  }

  [[nodiscard]] static Result setAlbedo(const std::vector<terrain::TerrainMaterial>& palette, const size_t index,
                                        const uuids::uuid& texture)
  {
    return run(palette, [&](auto& copy) {
      return terrain::setMaterialTextures(copy, index, texture,
                                          index < copy.size() ? copy[index].specularTexture : uuids::uuid{});
    });
  }

  [[nodiscard]] static Result setSpecular(const std::vector<terrain::TerrainMaterial>& palette, const size_t index,
                                          const uuids::uuid& texture)
  {
    return run(palette, [&](auto& copy) {
      return terrain::setMaterialTextures(copy, index,
                                          index < copy.size() ? copy[index].albedoTexture : uuids::uuid{}, texture);
    });
  }

  // The Materials list shows this many lines before it scrolls.
  static constexpr size_t maxVisibleRows = 8;

  // The list's height for count lines of rowPitch each, capped at maxVisibleRows, plus the frame's own padding.
  [[nodiscard]] static float listHeight(const size_t count, const float rowPitch, const float padding)
  {
    const size_t rows = std::clamp<size_t>(count, 1, maxVisibleRows);
    return static_cast<float>(rows) * rowPitch + padding;
  }

private:
  template <typename EditFn>
  [[nodiscard]] static Result run(const std::vector<terrain::TerrainMaterial>& palette, EditFn&& edit)
  {
    Result result{ palette, std::nullopt };
    result.refusal = edit(result.palette);

    if (result.refusal)
    {
      result.palette = palette;
    }

    return result;
  }
};

[[nodiscard]] inline bool pendingEqual(const std::string& a, const std::string& b)
{
  return a == b;
}

// A picker that is clicked and released without moving reports the same color up to rounding.
[[nodiscard]] inline bool pendingEqual(const glm::vec3& a, const glm::vec3& b)
{
  constexpr float tolerance = 1e-6f;

  return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

[[nodiscard]] inline std::string materialName(const terrain::TerrainMaterial& material)
{
  return material.name;
}

[[nodiscard]] inline glm::vec3 materialColor(const terrain::TerrainMaterial& material)
{
  return material.color;
}

// One field of the Materials block that is edited over several frames (a typed name, a dragged color) and becomes one
// palette edit when it settles. Only the selected material's details are drawn, so one pending value serves the block.
//
// The edit belongs to the material it started on: if that material is not the one at the row's index when the edit
// settles (a reorder, an undo or a snapshot changed it underneath), the edit is dropped. A pending edit is also
// settled as soon as a frame draws without its row (another terrain, another material selected, the block collapsed),
// so it never outlives the details it was typed into.
template <typename Value, Value (*Get)(const terrain::TerrainMaterial&)>
class PendingField {
public:
  struct Commit {
    uuids::uuid terrain;
    size_t index = 0;
    Value value{};

    [[nodiscard]] bool operator==(const Commit&) const = default;
  };

  // What the widget reported: whether the value changed, and whether the edit has settled (the field was left, the
  // drag was released). A frame that reports a change never settles the edit it started.
  struct Report {
    bool changed = false;
    bool settled = false;
  };

  // One frame of one row. draw receives the value to show and edits it in place. A row that is not the one being
  // edited shows the material's own value, so the field follows the palette while idle. canSend is false when an
  // edit would be dropped: the pending value is then abandoned.
  template <typename DrawFn>
  [[nodiscard]] std::optional<Commit> frame(const uuids::uuid& terrain, const size_t index,
                                            const terrain::TerrainMaterial& material, const bool canSend,
                                            DrawFn&& draw)
  {
    const bool mine = isRow(terrain, index);
    Value value = mine ? m_value : Get(material);
    const Report report = draw(value);

    if (report.changed)
    {
      if (!mine)
      {
        m_terrain = terrain;
        m_index = index;
        m_origin = material;
      }

      m_dirty = true;
      m_value = value;
    }
    else if (mine)
    {
      m_value = value;
    }

    if (!isRow(terrain, index))
    {
      return std::nullopt;
    }

    m_rowDrawn = true;

    if (report.settled && !report.changed)
    {
      return finish(material, canSend);
    }

    return std::nullopt;
  }

  // Once per UI frame, before the block draws. When the last frame did not draw the pending row, the edit settles
  // now. materialOf returns the material currently at (terrain, index), or nullopt when there is none.
  template <typename MaterialFn>
  [[nodiscard]] std::optional<Commit> endFrame(const bool canSend, MaterialFn&& materialOf)
  {
    const bool drawn = std::exchange(m_rowDrawn, false);

    if (drawn || !m_dirty)
    {
      return std::nullopt;
    }

    const std::optional<terrain::TerrainMaterial> material = materialOf(m_terrain, m_index);
    if (!material)
    {
      m_dirty = false;
      return std::nullopt;
    }

    return finish(*material, canSend);
  }

  [[nodiscard]] bool dirty() const
  {
    return m_dirty;
  }

private:
  uuids::uuid m_terrain;
  size_t m_index = 0;
  terrain::TerrainMaterial m_origin;
  Value m_value{};
  bool m_dirty = false;
  bool m_rowDrawn = false;

  [[nodiscard]] bool isRow(const uuids::uuid& terrain, const size_t index) const
  {
    return m_dirty && m_terrain == terrain && m_index == index;
  }

  [[nodiscard]] std::optional<Commit> finish(const terrain::TerrainMaterial& live, const bool canSend)
  {
    m_dirty = false;

    if (!canSend || live != m_origin || pendingEqual(m_value, Get(live)))
    {
      return std::nullopt;
    }

    return Commit{ m_terrain, m_index, m_value };
  }
};

using MaterialNameField = PendingField<std::string, materialName>;
using MaterialColorField = PendingField<glm::vec3, materialColor>;

// The rows the Materials block shows in Texture mode before a texture is picked: the palette cannot hold that state
// (no albedo reads as Color). The rows are indices into the palette they were recorded against, so they follow the
// edits the block makes itself (a move, a remove) and are dropped when the palette changes any other way (an undo,
// a redo, a snapshot) or another terrain is shown.
class TextureModeRows {
public:
  struct Change {
    enum class Kind { none, move, remove };

    Kind kind = Kind::none;
    size_t from = 0;
    size_t to = 0;
  };

  void sync(const uuids::uuid& terrain, const std::vector<terrain::TerrainMaterial>& live)
  {
    if (terrain != m_terrain || live != m_known)
    {
      m_rows.clear();
      m_terrain = terrain;
      m_known = live;
    }
  }

  [[nodiscard]] bool contains(const size_t index) const
  {
    return std::ranges::find(m_rows, index) != m_rows.end();
  }

  void insert(const size_t index)
  {
    if (!contains(index))
    {
      m_rows.push_back(index);
    }
  }

  void erase(const size_t index)
  {
    std::erase(m_rows, index);
  }

  // After an edit of the block's own applied: moves the rows with their materials, then drops those that got an
  // albedo texture (Texture mode is derived from it now) or no longer exist. Other rows stay.
  void follow(const Change& change, const std::vector<terrain::TerrainMaterial>& after)
  {
    for (size_t& row : m_rows)
    {
      row = rowAfter(change, row);
    }

    std::erase_if(m_rows, [&after](const size_t row) {
      return row >= after.size() || !after[row].albedoTexture.is_nil();
    });

    m_known = after;
  }

  // Where the entry at row is after change; a removed entry maps to removedRow.
  static constexpr size_t removedRow = static_cast<size_t>(-1);

  [[nodiscard]] static size_t rowAfter(const Change& change, const size_t row)
  {
    if (change.kind == Change::Kind::move)
    {
      if (row == change.from)
      {
        return change.to;
      }

      if (change.from < change.to && row > change.from && row <= change.to)
      {
        return row - 1;
      }

      if (change.to < change.from && row >= change.to && row < change.from)
      {
        return row + 1;
      }
    }
    else if (change.kind == Change::Kind::remove)
    {
      if (row == change.from)
      {
        return removedRow;
      }

      return row > change.from ? row - 1 : row;
    }

    return row;
  }

private:
  uuids::uuid m_terrain;
  std::vector<terrain::TerrainMaterial> m_known;
  std::vector<size_t> m_rows;
};

// The material the Materials block keeps selected after one of its own edits applied: a moved one stays selected,
// and a removed one hands the selection to its neighbor (the entry that took its place, else the new last one).
[[nodiscard]] inline size_t materialSelectionAfter(const TextureModeRows::Change& change, const size_t selected,
                                                   const size_t paletteSize)
{
  const size_t row = TextureModeRows::rowAfter(change, selected);
  const size_t kept = row == TextureModeRows::removedRow ? change.from : row;

  return paletteSize == 0 ? 0 : std::min(kept, paletteSize - 1);
}

// A refusal the Materials block shows beside the selected material's Remove button. It belongs to the selection it
// was raised for and is cleared once another material or another terrain is selected.
class MaterialNote {
public:
  void raise(const uuids::uuid& terrain, const size_t index, std::string text)
  {
    m_terrain = terrain;
    m_index = index;
    m_text = std::move(text);
  }

  void clear()
  {
    m_text.clear();
  }

  // The note for the selection drawn now, empty when there is none; a note raised for another selection is cleared.
  [[nodiscard]] const std::string& textFor(const uuids::uuid& terrain, const size_t index)
  {
    if (terrain != m_terrain || index != m_index)
    {
      m_text.clear();
    }

    return m_text;
  }

private:
  uuids::uuid m_terrain;
  size_t m_index = 0;
  std::string m_text;
};

#endif //TERRAINPALETTEEDITOR_H
