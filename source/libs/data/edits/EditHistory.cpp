#include "EditHistory.h"
#include "Replication.h"
#include "objects/ObjectManager.h"
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace edits {

std::uint64_t EditHistory::newToken()
{
  // Shared by every history, so a play-session history can never collide with the authored one.
  static std::atomic<std::uint64_t> counter{ 0 };
  return ++counter;
}

void EditHistory::pushUndo(std::vector<EditCommand> entry)
{
  m_redoStack.clear();
  m_redoIds.clear();

  m_undoStack.push_back(std::move(entry));
  m_undoIds.push_back(newToken());

  if (m_undoStack.size() > maxDepth)
  {
    // The oldest state still reachable by undo is no longer the one the history started from.
    m_undoStack.pop_front();
    m_undoIds.pop_front();
    m_baseToken = newToken();
  }
}

void EditHistory::dropUndoStack()
{
  if (m_undoStack.empty())
  {
    return;
  }

  m_undoStack.clear();
  m_undoIds.clear();
  m_baseToken = newToken();
}

void EditHistory::moveUndoToRedo()
{
  m_redoStack.push_back(std::move(m_undoStack.back()));
  m_redoIds.push_back(m_undoIds.back());
  m_undoStack.pop_back();
  m_undoIds.pop_back();
}

void EditHistory::moveRedoToUndo()
{
  m_undoStack.push_back(std::move(m_redoStack.back()));
  m_undoIds.push_back(m_redoIds.back());
  m_redoStack.pop_back();
  m_redoIds.pop_back();
}

std::uint64_t EditHistory::position() const
{
  return m_undoIds.empty() ? m_baseToken : m_undoIds.back();
}

void EditHistory::record(EditCommand command)
{
  pushUndo({ std::move(command) });
}

void EditHistory::recordBatch(std::vector<EditCommand> commands)
{
  if (commands.empty())
  {
    return;
  }

  if (commands.size() == 1)
  {
    record(std::move(commands.front()));
    return;
  }

  const bool allObjectSceneEdits = std::ranges::all_of(commands, [](const EditCommand& command) {
    return command.payloadForm() == PayloadForm::sceneEdit && !command.targetsSceneScripts();
  });

  if (!allObjectSceneEdits)
  {
    throw std::invalid_argument("EditHistory::recordBatch: every command must be an object sceneEdit");
  }

  pushUndo(std::move(commands));
}

bool EditHistory::canUndo() const
{
  return !m_undoStack.empty();
}

bool EditHistory::canRedo() const
{
  return !m_redoStack.empty();
}

std::optional<CommandKind> EditHistory::nextUndoKind() const
{
  if (m_undoStack.empty())
  {
    return std::nullopt;
  }

  return m_undoStack.back().back().kind();
}

std::optional<CommandKind> EditHistory::nextRedoKind() const
{
  if (m_redoStack.empty())
  {
    return std::nullopt;
  }

  return m_redoStack.back().back().kind();
}

namespace {
  bool allReversible(const std::vector<EditCommand>& entry)
  {
    return std::ranges::all_of(entry, [](const EditCommand& command) { return command.isReversible(); });
  }
}

bool EditHistory::nextUndoIsReversible() const
{
  return !m_undoStack.empty() && allReversible(m_undoStack.back());
}

bool EditHistory::nextRedoIsReversible() const
{
  return !m_redoStack.empty() && allReversible(m_redoStack.back());
}

namespace {
  HistoryResult toHistoryResult(const ValidationFailure failure)
  {
    switch (failure)
    {
      case ValidationFailure::notUndoable: return HistoryResult::notUndoable;
      case ValidationFailure::targetMissing: return HistoryResult::targetMissing;
      case ValidationFailure::targetChanged: return HistoryResult::targetChanged;
      case ValidationFailure::targetActive: return HistoryResult::targetActive;
      case ValidationFailure::none: default: return HistoryResult::applied;
    }
  }
}

HistoryOutcome EditHistory::undo(const ObjectManager& objectManager, const AssetRegistry* assetRegistry,
                                 const SceneAsset* scene)
{
  if (m_undoStack.empty())
  {
    return { HistoryResult::historyEmpty };
  }

  const std::vector<EditCommand> entry = m_undoStack.back();

  if (entry.size() == 1)
  {
    const EditCommand& command = entry.front();

    if (!command.isReversible())
    {
      const auto conflict = command.primaryUUID();
      // This entry is the newest one still on the undo stack; everything remaining beneath it is older,
      // and skipping past a refusal to reach it would apply reverts out of order.
      dropUndoStack();
      return { HistoryResult::notUndoable, conflict };
    }

    const auto validation = command.validateForUndo(objectManager, assetRegistry, scene);
    if (!validation.ok())
    {
      dropUndoStack();
      return { toHistoryResult(validation.failure), validation.conflict };
    }

    HistoryOutcome outcome{ HistoryResult::applied };
    if (command.payloadForm() == PayloadForm::sceneEdit)
    {
      outcome.jsonPayload = command.buildUndoJSON(objectManager);
    }
    else
    {
      outcome.messagePayload = command.buildUndoMessage(objectManager);
    }

    moveUndoToRedo();

    return outcome;
  }

  // A grouped entry: simulate the reverse on a scratch copy so each command's validation and undo payload
  // see what the commands undone before it (later in record order, so undone first) would have left
  // behind - see EditHistory.h's undo() comment.
  const auto scratch = makeScratchCopy(objectManager);
  std::vector<nlohmann::json> ops;
  ops.reserve(entry.size());

  for (auto it = entry.rbegin(); it != entry.rend(); ++it)
  {
    const EditCommand& command = *it;

    // Recording refuses a group holding one, and the scratch copy has no script list to validate against.
    if (!command.isReversible() || command.targetsSceneScripts())
    {
      const auto conflict = command.primaryUUID();
      dropUndoStack();
      return { HistoryResult::notUndoable, conflict };
    }

    const auto validation = command.validateForUndo(*scratch, assetRegistry);
    if (!validation.ok())
    {
      dropUndoStack();
      return { toHistoryResult(validation.failure), validation.conflict };
    }

    const auto undoJSON = command.buildUndoJSON(*scratch);
    if (replication::applySceneEdit(*scratch, undoJSON, assetRegistry) != replication::SceneEditResult::applied)
    {
      dropUndoStack();
      return { HistoryResult::targetChanged, command.primaryUUID() };
    }

    ops.push_back(undoJSON);
  }

  HistoryOutcome outcome{ HistoryResult::applied };
  outcome.jsonPayload = replication::buildBatch(ops);

  moveUndoToRedo();

  return outcome;
}

HistoryOutcome EditHistory::redo(const ObjectManager& objectManager, const AssetRegistry* assetRegistry,
                                 const SceneAsset* scene)
{
  if (m_redoStack.empty())
  {
    return { HistoryResult::historyEmpty };
  }

  const std::vector<EditCommand> entry = m_redoStack.back();

  if (entry.size() == 1)
  {
    const EditCommand& command = entry.front();

    // Every entry that reached the redo stack already passed isReversible() when it was undone, so redo
    // only needs to check whether the "before" state it expects still holds.
    const auto validation = command.validateForRedo(objectManager, assetRegistry, scene);
    if (!validation.ok())
    {
      m_redoStack.clear();
      m_redoIds.clear();
      return { toHistoryResult(validation.failure), validation.conflict };
    }

    HistoryOutcome outcome{ HistoryResult::applied };
    if (command.payloadForm() == PayloadForm::sceneEdit)
    {
      outcome.jsonPayload = command.buildRedoJSON(objectManager);
    }
    else
    {
      outcome.messagePayload = command.buildRedoMessage(objectManager);
    }

    moveRedoToUndo();

    return outcome;
  }

  const auto scratch = makeScratchCopy(objectManager);
  std::vector<nlohmann::json> ops;
  ops.reserve(entry.size());

  for (const auto& command : entry)
  {
    if (command.targetsSceneScripts())
    {
      m_redoStack.clear();
      m_redoIds.clear();
      return { HistoryResult::notUndoable, command.primaryUUID() };
    }

    const auto validation = command.validateForRedo(*scratch, assetRegistry);
    if (!validation.ok())
    {
      m_redoStack.clear();
      m_redoIds.clear();
      return { toHistoryResult(validation.failure), validation.conflict };
    }

    const auto redoJSON = command.buildRedoJSON(*scratch);
    if (replication::applySceneEdit(*scratch, redoJSON, assetRegistry) != replication::SceneEditResult::applied)
    {
      m_redoStack.clear();
      m_redoIds.clear();
      return { HistoryResult::targetChanged, command.primaryUUID() };
    }

    ops.push_back(redoJSON);
  }

  HistoryOutcome outcome{ HistoryResult::applied };
  outcome.jsonPayload = replication::buildBatch(ops);

  moveRedoToUndo();

  return outcome;
}

void EditHistory::reportUndoRejected()
{
  if (!m_redoStack.empty())
  {
    m_redoStack.pop_back();
    m_redoIds.pop_back();
  }
}

void EditHistory::reportRedoRejected()
{
  if (!m_undoStack.empty())
  {
    m_undoStack.pop_back();
    m_undoIds.pop_back();
  }
}

void EditHistory::clear()
{
  dropUndoStack();
  m_redoStack.clear();
  m_redoIds.clear();
}

namespace {
  std::string describeGroup(const std::vector<EditCommand>& entry, const ObjectManager& objectManager,
                            const AssetRegistry* assetRegistry)
  {
    if (entry.size() == 1)
    {
      return entry.front().describeForMenu(objectManager, assetRegistry);
    }

    const auto allOfKind = [&entry](const CommandKind kind) {
      return std::ranges::all_of(entry, [kind](const EditCommand& command) { return command.kind() == kind; });
    };

    const auto count = std::to_string(entry.size());

    if (allOfKind(CommandKind::removeObject))
    {
      return "Delete " + count + " Objects";
    }

    if (allOfKind(CommandKind::duplicateObject))
    {
      return "Duplicate " + count + " Objects";
    }

    return count + " Edits";
  }
}

std::optional<std::string> EditHistory::nextUndoLabel(const ObjectManager& objectManager,
                                                       const AssetRegistry* assetRegistry) const
{
  if (m_undoStack.empty())
  {
    return std::nullopt;
  }

  return describeGroup(m_undoStack.back(), objectManager, assetRegistry);
}

std::optional<std::string> EditHistory::nextRedoLabel(const ObjectManager& objectManager,
                                                       const AssetRegistry* assetRegistry) const
{
  if (m_redoStack.empty())
  {
    return std::nullopt;
  }

  return describeGroup(m_redoStack.back(), objectManager, assetRegistry);
}

std::size_t EditHistory::undoDepth() const
{
  return m_undoStack.size();
}

std::size_t EditHistory::redoDepth() const
{
  return m_redoStack.size();
}

}
