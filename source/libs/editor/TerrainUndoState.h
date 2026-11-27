#ifndef TERRAINUNDOSTATE_H
#define TERRAINUNDOSTATE_H

#include <edits/EditCommand.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <uuid.h>

// Which terrain a pending terrain undo or redo is waiting on: the terrainChunks echo about that terrain is what ends
// the wait, since a terrain edit is answered with chunks rather than a snapshot. The terrain is staged while the
// request builds its payload and bound to the in-flight gate when the gate begins, so a request that is not terrain
// (or one that sends nothing) never inherits an earlier one's terrain.
class TerrainUndoState
{
public:
  // A new request is starting; whatever an earlier one staged is not part of it.
  void startRequest()
  {
    m_staged.reset();
  }

  void stage(const uuids::uuid& terrain)
  {
    m_staged = terrain;
  }

  // The request went in flight: it now waits on the staged terrain, or on none when it staged nothing.
  void begin()
  {
    m_pending = std::exchange(m_staged, std::nullopt);
  }

  void reset()
  {
    m_staged.reset();
    m_pending.reset();
  }

  [[nodiscard]] const std::optional<uuids::uuid>& pending() const
  {
    return m_pending;
  }

  // A terrainChunks message about terrain landed. True when it is the answer the in-flight request waits on, so the
  // caller clears the gate. A gate that is no longer pending (it timed out or was cleared) leaves nothing to answer.
  [[nodiscard]] bool noteTerrainChunks(const uuids::uuid& terrain, const bool gatePending)
  {
    if (!gatePending)
    {
      m_pending.reset();
      return false;
    }

    if (m_pending != terrain)
    {
      return false;
    }

    m_pending.reset();
    return true;
  }

private:
  std::optional<uuids::uuid> m_staged;
  std::optional<uuids::uuid> m_pending;
};

enum class TerrainUndoRefusal : uint8_t { none, strokeActive, awaitingFinalTerrain };

// What the editor knows when an undo or redo is requested.
struct TerrainUndoContext
{
  bool strokeActive = false;
  bool awaitingFinalChunks = false;
  // The kind and terrain of the command on top of the stack the request would take from.
  std::optional<edits::CommandKind> topKind;
  std::optional<uuids::uuid> topUuid;
  // The terrain of the brush session, whose released stroke's final chunks may be outstanding.
  uuids::uuid brushTerrain;
};

// An active stroke refuses every undo and redo. A released stroke's outstanding final chunks refuse only the request
// that would reverse a terrain stroke of that same terrain, because its recorded chunks are the ones still changing.
[[nodiscard]] inline TerrainUndoRefusal decideTerrainUndoRefusal(const TerrainUndoContext& context)
{
  if (context.strokeActive)
  {
    return TerrainUndoRefusal::strokeActive;
  }

  if (context.awaitingFinalChunks && context.topKind == edits::CommandKind::terrainStroke &&
      context.topUuid == context.brushTerrain)
  {
    return TerrainUndoRefusal::awaitingFinalTerrain;
  }

  return TerrainUndoRefusal::none;
}

[[nodiscard]] inline std::string terrainUndoRefusalMessage(const TerrainUndoRefusal refusal, const bool isUndo)
{
  const std::string prefix = isUndo ? "Can't undo: " : "Can't redo: ";

  switch (refusal)
  {
    case TerrainUndoRefusal::strokeActive: return prefix + "finish the terrain stroke first.";
    case TerrainUndoRefusal::awaitingFinalTerrain: return prefix + "waiting for the server's final terrain.";
    case TerrainUndoRefusal::none: break;
  }

  return {};
}

#endif //TERRAINUNDOSTATE_H
