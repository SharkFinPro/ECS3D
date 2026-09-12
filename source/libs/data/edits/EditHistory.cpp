#include "EditHistory.h"

namespace edits {

void EditHistory::record(EditCommand command)
{
  m_redoStack.clear();
  m_undoStack.push_back(std::move(command));

  if (m_undoStack.size() > maxDepth)
  {
    m_undoStack.pop_front();
  }
}

bool EditHistory::canUndo() const
{
  return !m_undoStack.empty();
}

bool EditHistory::canRedo() const
{
  return !m_redoStack.empty();
}

namespace {
  HistoryResult toHistoryResult(const ValidationFailure failure)
  {
    switch (failure)
    {
      case ValidationFailure::notUndoable: return HistoryResult::notUndoable;
      case ValidationFailure::targetMissing: return HistoryResult::targetMissing;
      case ValidationFailure::targetChanged: return HistoryResult::targetChanged;
      case ValidationFailure::none: default: return HistoryResult::applied;
    }
  }
}

HistoryOutcome EditHistory::undo(const ObjectManager& objectManager, const AssetRegistry* assetRegistry)
{
  if (m_undoStack.empty())
  {
    return { HistoryResult::historyEmpty };
  }

  const EditCommand command = m_undoStack.back();

  if (!command.isReversible())
  {
    const auto conflict = command.primaryUUID();
    // This entry is the newest one still on the undo stack; everything remaining beneath it is older,
    // and skipping past a refusal to reach it would apply reverts out of order.
    m_undoStack.clear();
    return { HistoryResult::notUndoable, conflict };
  }

  const auto validation = command.validateForUndo(objectManager, assetRegistry);
  if (!validation.ok())
  {
    m_undoStack.clear();
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

  m_undoStack.pop_back();
  m_redoStack.push_back(command);

  return outcome;
}

HistoryOutcome EditHistory::redo(const ObjectManager& objectManager, const AssetRegistry* assetRegistry)
{
  if (m_redoStack.empty())
  {
    return { HistoryResult::historyEmpty };
  }

  const EditCommand command = m_redoStack.back();

  // Every entry that reached the redo stack already passed isReversible() when it was undone, so redo
  // only needs to check whether the "before" state it expects still holds.
  const auto validation = command.validateForRedo(objectManager, assetRegistry);
  if (!validation.ok())
  {
    m_redoStack.clear();
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

  m_redoStack.pop_back();
  m_undoStack.push_back(command);

  return outcome;
}

void EditHistory::reportUndoRejected()
{
  if (!m_redoStack.empty())
  {
    m_redoStack.pop_back();
  }
}

void EditHistory::reportRedoRejected()
{
  if (!m_undoStack.empty())
  {
    m_undoStack.pop_back();
  }
}

void EditHistory::clear()
{
  m_undoStack.clear();
  m_redoStack.clear();
}

}
