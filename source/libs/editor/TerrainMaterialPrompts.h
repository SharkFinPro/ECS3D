#ifndef TERRAINMATERIALPROMPTS_H
#define TERRAINMATERIALPROMPTS_H

#include "TerrainPaletteEditor.h"
#include <terrain/TerrainStore.h>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <uuid.h>

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

// The confirmation a removal of a painted material waits for: which entry goes and what its painted ground becomes.
// It belongs to the palette it was opened over, so an undo, a redo or a snapshot that changes the palette meanwhile
// closes it rather than remove whatever now sits at the index.
class MaterialRemovePrompt {
public:
  void open(const uuids::uuid& terrain, const std::vector<terrain::TerrainMaterial>& palette, const size_t index)
  {
    m_terrain = terrain;
    m_palette = palette;
    m_index = index;
    m_replacement = TerrainPaletteEditor::defaultReplacement(index);
    m_open = index < palette.size() && m_replacement < palette.size();
  }

  void close()
  {
    m_open = false;
  }

  [[nodiscard]] bool isOpen() const
  {
    return m_open;
  }

  [[nodiscard]] const uuids::uuid& terrainUuid() const
  {
    return m_terrain;
  }

  [[nodiscard]] size_t index() const
  {
    return m_index;
  }

  [[nodiscard]] size_t replacement() const
  {
    return m_replacement;
  }

  // Any other entry of the palette; the removed one itself is ignored.
  void choose(const size_t replacement)
  {
    if (replacement != m_index && replacement < m_palette.size())
    {
      m_replacement = replacement;
    }
  }

  // Whether the prompt still describes this terrain's live palette; closes it when it does not.
  [[nodiscard]] bool matches(const uuids::uuid& terrain, const std::vector<terrain::TerrainMaterial>& live)
  {
    if (m_open && (terrain != m_terrain || live != m_palette))
    {
      m_open = false;
    }

    return m_open;
  }

private:
  uuids::uuid m_terrain;
  std::vector<terrain::TerrainMaterial> m_palette;
  size_t m_index = 0;
  size_t m_replacement = 0;
  bool m_open = false;
};

#endif //TERRAINMATERIALPROMPTS_H
