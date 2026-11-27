#include "EditorApp.h"
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <edits/EditHistory.h>
#include <edits/TerrainSetChunks.h>
#include <terrain/TerrainBinding.h>
#include <terrain/TerrainStore.h>
#include <SaveUI.h>
#include <NetClient.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
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

bool EditorApp::refuseForTerrainStroke(const bool isUndo)
{
  const edits::EditHistory& history = m_playHistory.current();

  TerrainUndoContext context;
  context.strokeActive = m_terrainBrush.active();
  context.awaitingFinalChunks = m_terrainBrush.awaitingFinalChunks();
  context.topKind = isUndo ? history.nextUndoKind() : history.nextRedoKind();
  context.topUuid = isUndo ? history.nextUndoPrimaryUUID() : history.nextRedoPrimaryUUID();
  context.brushTerrain = m_terrainBrush.terrain();

  const TerrainUndoRefusal refusal = decideTerrainUndoRefusal(context);

  if (refusal == TerrainUndoRefusal::none)
  {
    return false;
  }

  logMessage("Info", terrainUndoRefusalMessage(refusal, isUndo));
  return true;
}

void EditorApp::undo()
{
  if (!m_serverEditable)
  {
    logMessage("Info", "Can't undo: the connected server is read-only.");
    return;
  }

  if (refuseForTerrainStroke(true))
  {
    return;
  }

  // A non-reversible command (see AGENTS.md's Editor Undo/Redo section) stays on top of the stack,
  // refused, rather than being handed to EditHistory::undo(), which would treat "a kind with no reverse"
  // as a validation conflict and drop it along with everything older beneath it.
  if (!m_playHistory.current().canUndo())
  {
    logMessage("Info", "Nothing to undo.");
    return;
  }

  if (!m_playHistory.current().nextUndoIsReversible())
  {
    logMessage("Info", "Can't undo: that command has no reverse yet.");
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
    outcome = m_playHistory.current().undo(*scene->getObjectManager(), m_assetRegistry.get(), scene.get(),
                                              m_terrainStore.get());
  }
  catch (const std::exception& e)
  {
    // A stored before/after blob failed to parse - data corruption, not a routine refusal (see
    // EditHistory.h's exception contract). The entry it came from is unrecoverable either way, so drop
    // the whole history rather than risk retrying into the same throw.
    logMessage("Error", std::string("Undo failed unexpectedly (") + e.what() + "); clearing the edit history.");
    m_playHistory.current().clear();
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

  if (refuseForTerrainStroke(false))
  {
    return;
  }

  if (!m_playHistory.current().canRedo())
  {
    logMessage("Info", "Nothing to redo.");
    return;
  }

  if (!m_playHistory.current().nextRedoIsReversible())
  {
    logMessage("Info", "Can't redo: that command has no reverse yet.");
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
    outcome = m_playHistory.current().redo(*scene->getObjectManager(), m_assetRegistry.get(), scene.get(),
                                           m_terrainStore.get());
  }
  catch (const std::exception& e)
  {
    logMessage("Error", std::string("Redo failed unexpectedly (") + e.what() + "); clearing the edit history.");
    m_playHistory.current().clear();
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
      // replaceAsset/renameAsset/removeAsset), every structural kind (addObject, removeObject,
      // reparentObject, reorderObject, renameObject, addComponent, removeComponent, duplicateObject,
      // instantiatePrefab) is the sceneEdit json form - a single command's own op, or one replication::
      // buildBatch of a grouped entry's ops (see EditHistory::undo/redo) - chunked into a sceneEdit
      // message the same way EditorApp::onSceneEdit does for a normal (non-undo) edit, and a terrain
      // stroke is the terrainChunks form: setChunks terrainEdit messages. They are packed first, so a pack
      // failure leaves the view and the server untouched; then applied to the editor's own volume as the
      // prediction the server's hash-only echo confirms; then sent. A send that fails partway leaves the
      // view ahead of the server, which the echo's hash mismatch repairs with a full chunk request.
      if (outcome.messagePayload)
      {
        // A terrain comes back through the same steps as a create: the editor's own registry and store first, so the
        // snapshot that follows keeps the description and chunks, then the addAsset, then the chunk upload the server
        // holds Play for. A failure rolls the editor back and clears the history, since the entry cannot be replayed.
        if (outcome.terrainRestore)
        {
          const auto result = restoreTerrain(*outcome.terrainRestore,
                                             [this, &outcome] { m_netClient->send(*outcome.messagePayload); });
          if (result != edits::TerrainRestoreResult::applied)
          {
            if (result == edits::TerrainRestoreResult::failedAfterSend)
            {
              try
              {
                m_netClient->send(replication::packRemoveAsset(
                  replication::buildRemoveAsset(outcome.terrainRestore->description.uuid)));
              }
              catch (const std::exception& e)
              {
                Log::error(LogCategory::editor, std::string("The server's copy of the terrain could not be removed: ") +
                                                  e.what());
              }
            }

            logMessage("Error", "The terrain " + action + " could not be applied (" +
                                  (result == edits::TerrainRestoreResult::refused ? "the editor refused the terrain"
                                                                                   : "it failed and was rolled back") +
                                  "); clearing the edit history.");
            m_playHistory.current().clear();
            break;
          }
        }
        else
        {
          if (outcome.terrainRemoved)
          {
            m_terrainUpload.forget(*outcome.terrainRemoved);
            m_createdTerrains.erase(*outcome.terrainRemoved);
          }

          m_netClient->send(*outcome.messagePayload);
        }
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
      else if (outcome.terrainPayload)
      {
        const edits::TerrainSetChunks& payload = *outcome.terrainPayload;

        std::vector<net::Message> messages;
        try
        {
          messages = edits::packSetChunksEdits(payload);
        }
        catch (const std::exception& e)
        {
          logMessage("Error", "The terrain " + action + " could not be packed (" + e.what() +
                              "); clearing the edit history.");
          m_playHistory.current().clear();
          break;
        }

        if (const auto volume = m_terrainStore->find(payload.terrain))
        {
          edits::predictSetChunks(*volume, payload);
          terrain::invalidateTerrainBounds(*m_sceneManager, payload.terrain);
        }

        m_terrainUndo.stage(payload.terrain);

        try
        {
          for (const net::Message& message : messages)
          {
            m_netClient->send(message);
          }
        }
        catch (const std::exception& e)
        {
          logMessage("Error", "The terrain " + action + " could not be sent in full (" + e.what() +
                              "); the server's terrain may differ from this view until its chunks arrive.");
        }
      }
      noteRecordedEdit();
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

    case edits::HistoryResult::targetActive:
      logMessage("Info", "Can't " + action + ": that scene is active; load another scene first.");
      break;
  }
}
