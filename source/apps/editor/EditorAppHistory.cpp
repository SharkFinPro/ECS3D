#include "EditorApp.h"
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <SaveUI.h>
#include <NetClient.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace {
  // What to call the conflicting uuid a targetMissing/targetChanged refusal names, when undo()/redo()
  // report one.
  std::string conflictLabel(const std::optional<uuids::uuid>& conflict)
  {
    return conflict ? uuids::to_string(*conflict) : std::string("the target");
  }
}

void EditorApp::logMessage(const std::string& level, const std::string& message)
{
  if (level == "Error")
  {
    Log::error(LogCategory::editor, message);
  }
  else
  {
    Log::info(LogCategory::editor, message);
  }

  // Capped, and oldest first: a message stream the editor cannot parse produces one of these per tick,
  // and the panel re-renders every line it holds each frame.
  constexpr size_t maxMessages = 200;
  if (m_errorMessages.size() >= maxMessages)
  {
    m_errorMessages.erase(m_errorMessages.begin());
  }

  m_errorMessages.push_back("[" + level + "] " + message);
}

void EditorApp::undo()
{
  if (!m_serverEditable)
  {
    logMessage("Info", "Can't undo: the connected server is read-only.");
    return;
  }

  // A non-reversible command (see AGENTS.md's Editor Undo/Redo section) stays on top of the stack,
  // refused, rather than being handed to EditHistory::undo(), which would treat "a kind with no reverse"
  // as a validation conflict and drop it along with everything older beneath it.
  if (!m_editHistory.canUndo())
  {
    logMessage("Info", "Nothing to undo.");
    return;
  }

  if (!m_editHistory.nextUndoIsReversible())
  {
    logMessage("Info", "Can't undo: an object removal can't be undone yet.");
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    logMessage("Info", "Can't undo: no scene is loaded.");
    return;
  }

  edits::HistoryOutcome outcome;
  try
  {
    outcome = m_editHistory.undo(*scene->getObjectManager(), m_assetRegistry.get());
  }
  catch (const std::exception& e)
  {
    // A stored before/after blob failed to parse - data corruption, not a routine refusal (see
    // EditHistory.h's exception contract). The entry it came from is unrecoverable either way, so drop
    // the whole history rather than risk retrying into the same throw.
    logMessage("Error", std::string("Undo failed unexpectedly (") + e.what() + "); clearing the edit history.");
    m_editHistory.clear();
    return;
  }

  reportHistoryOutcome(outcome, true);
}

void EditorApp::redo()
{
  if (!m_serverEditable)
  {
    logMessage("Info", "Can't redo: the connected server is read-only.");
    return;
  }

  if (!m_editHistory.canRedo())
  {
    logMessage("Info", "Nothing to redo.");
    return;
  }

  if (!m_editHistory.nextRedoIsReversible())
  {
    logMessage("Info", "Can't redo: an object removal can't be redone yet.");
    return;
  }

  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    logMessage("Info", "Can't redo: no scene is loaded.");
    return;
  }

  edits::HistoryOutcome outcome;
  try
  {
    outcome = m_editHistory.redo(*scene->getObjectManager(), m_assetRegistry.get());
  }
  catch (const std::exception& e)
  {
    logMessage("Error", std::string("Redo failed unexpectedly (") + e.what() + "); clearing the edit history.");
    m_editHistory.clear();
    return;
  }

  reportHistoryOutcome(outcome, false);
}

void EditorApp::reportHistoryOutcome(const edits::HistoryOutcome& outcome, const bool isUndo)
{
  const std::string action = isUndo ? "undo" : "redo";

  switch (outcome.result)
  {
    case edits::HistoryResult::applied:
      // Exactly one of these is set (see HistoryOutcome), fixed per command kind by payloadForm(): a
      // component value edit or asset op is the networkMessage form (editComponent/addAsset/
      // replaceAsset/renameAsset/removeAsset), everything else (addObject, reparentObject,
      // reorderObject, renameObject, addComponent) is the sceneEdit json form, chunked into a
      // sceneEdit message the same way EditorApp::onSceneEdit does for a normal (non-undo) edit.
      if (outcome.messagePayload)
      {
        m_netClient->send(*outcome.messagePayload);
      }
      else if (outcome.jsonPayload)
      {
        const auto payload = outcome.jsonPayload->dump();
        net::Message message(net::MessageType::sceneEdit);
        for (const std::vector<uint8_t> chunks(payload.begin(), payload.end()); const auto& chunk : chunks)
        {
          message.write(chunk);
        }
        m_netClient->send(message);
      }
      m_saveUI->markEdited();
      break;

    case edits::HistoryResult::historyEmpty:
      logMessage("Info", "Nothing to " + action + ".");
      break;

    case edits::HistoryResult::notUndoable:
      logMessage("Info", "Can't " + action + ": that command has no reverse yet.");
      break;

    case edits::HistoryResult::targetMissing:
      logMessage("Info", "Can't " + action + ": " + conflictLabel(outcome.conflict) + " no longer exists.");
      break;

    case edits::HistoryResult::targetChanged:
      logMessage("Info", "Can't " + action + ": " + conflictLabel(outcome.conflict) + " has changed since that edit.");
      break;
  }
}
