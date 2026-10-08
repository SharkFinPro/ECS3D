#include "EditorApp.h"
#include <ProjectPacker.h>
#include <Replication.h>
#include <PlayerSlots.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <RingBufferSink.h>
#include <ServerLog.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

void EditorApp::applyMessage(const net::Message& message)
{
  switch (message.getType())
  {
    case net::MessageType::snapshot:
      handleSnapshot(message);
      break;

    case net::MessageType::stateDelta:
      handleStateDelta(message);
      break;

    case net::MessageType::editComponent:
      handleEditComponent(message);
      break;

    case net::MessageType::objectSpawned:
      handleObjectSpawned(message);
      break;

    case net::MessageType::objectDestroyed:
      handleObjectDestroyed(message);
      break;

    case net::MessageType::objectComponentsChanged:
      handleObjectComponentsChanged(message);
      break;

    case net::MessageType::editStatus:
      handleEditStatus(message);
      break;

    case net::MessageType::sceneStatus:
      handleSceneStatus(message);
      break;

    case net::MessageType::serverLog:
      handleServerLog(message);
      break;

    case net::MessageType::playerSlot:
      handlePlayerSlot(message);
      break;

    default: break;
  }
}

void EditorApp::handleSnapshot(const net::Message& message)
{
  // Full state on join: rebuild the replicated scene from the packed project blob.
  m_projectPacker->unpack(message);

  const auto scene = m_sceneManager->getCurrentScene();
  Log::info(LogCategory::editor, "Applied snapshot (" + std::to_string(message.size()) + " bytes). Current scene: "
    + (scene ? scene->getName() : "<none>") + " ("
    + std::to_string(scene ? scene->getObjectManager()->getAllObjects().size() : 0) + " objects).");

  // The server re-snapshots (rather than echoing a targeted result) after every sceneEdit and asset
  // mutation, so this is the rebroadcast a structural or asset undo/redo request is waiting on - see the
  // in-flight gate on requestUndo()/requestRedo().
  clearUndoRedoPending();

  if (m_historyScope.observe(scene ? std::optional(scene->getUUID()) : std::nullopt))
  {
    if (m_playHistory.current().canUndo() || m_playHistory.current().canRedo() || m_playHistory.hasStash())
    {
      Log::info(LogCategory::editor, "The active scene changed underneath the editor; undo history cleared.");
    }

    clearEditHistory();
  }
}

void EditorApp::handleStateDelta(const net::Message& message) const
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::unpackStateDelta(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleEditComponent(const net::Message& message)
{
  // Another editor (or this one, echoed by the server) changed a component. Like the client, a missed
  // edit is logged rather than ignored, so a real desync stands out from the ordinary rebroadcast race.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const auto result = replication::applyComponentEdit(*scene->getObjectManager(), message);
    replication::logMissedComponentEdit(result, message, LogCategory::editor);
  }

  // The rebroadcast a component-edit undo/redo request is waiting on - see the in-flight gate on
  // requestUndo()/requestRedo(). Any inbound editComponent releases it, not just the echo of this
  // editor's own request - including another editor's unrelated edit in a multi-editor session, which can
  // release the gate earlier than the request it was actually waiting on. Accepted: a stale second press
  // let through early is still caught by EditHistory's own validation against the live scene, the same
  // safety net a single-editor session relies on for every other refusal.
  clearUndoRedoPending();
}

void EditorApp::handleObjectSpawned(const net::Message& message) const
{
  // A script spawned an object at runtime; splice the packed object into the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectSpawned(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleObjectDestroyed(const net::Message& message) const
{
  // A script destroyed an object at runtime; drop it from the replicated scene.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectDestroyed(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleObjectComponentsChanged(const net::Message& message) const
{
  // A script added/removed a component on an object that already exists here; reconcile it in place.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::applyObjectComponentsChanged(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleEditStatus(const net::Message& message)
{
  const auto editable = replication::parseEditStatus(message);
  if (!editable)
  {
    return;
  }

  // The server told us whether it's editable; a non-edit server makes the editor a read-only viewer.
  m_serverEditable = *editable;

  if (!m_serverEditable)
  {
    logMessage("Info", "Connected to a non-edit server - the editor is read-only.");
  }
}

void EditorApp::handleSceneStatus(const net::Message& message)
{
  const auto status = replication::parseSceneStatus(message);
  if (!status)
  {
    return;
  }

  if (m_playHistory.observeStatus(*status))
  {
    clearUndoRedoPending();
  }

  m_sceneStatus = *status;
}

void EditorApp::handleServerLog(const net::Message& message) const
{
  const auto batch = net::unpackServerLog(message);

  // "[server]" distinguishes these from the editor's own logging in the same category (e.g. both sides
  // log under LogCategory::net) without collapsing every entry into one category and losing the level/
  // category filters the Console panel already offers.
  for (const auto& entry : batch.entries)
  {
    m_consoleSink->write(LogEntry{ entry.time, entry.level, entry.category, "[server] " + entry.message });
  }

  if (batch.dropped > 0)
  {
    m_consoleSink->write(LogEntry{ std::chrono::system_clock::now(), LogLevel::warn, LogCategory::server,
      "[server] " + std::to_string(batch.dropped) + " log entr"
      + (batch.dropped == 1 ? std::string("y") : std::string("ies"))
      + " were dropped before this batch (the server's outbound log queue overflowed)." });
  }
}

void EditorApp::handlePlayerSlot(const net::Message& message)
{
  // The server broadcasts every connection's slot; keep only the one tagged with our own nonce.
  const auto payload = replication::parsePlayerSlot(message);
  if (!payload || payload->nonce != m_joinNonce)
  {
    return;
  }

  const auto requested = m_requestedPlayerSlot;
  m_requestedPlayerSlot.reset();
  m_playerSlot = payload->slot;

  // No request pending: this is the join reply, which only records the slot.
  if (!requested)
  {
    return;
  }

  if (payload->slot != *requested)
  {
    Log::warn(LogCategory::editor, "The server did not grant player slot " + std::to_string(*requested)
      + "; still controlling player " + std::to_string(payload->slot) + ".");
    return;
  }

  // The new slot has no input yet, so send the current state on the next frame instead of waiting for a change.
  m_inputSent = false;

  const auto scene = m_sceneManager->getCurrentScene();
  const auto camera = scene ? findPlayerCamera(*scene->getObjectManager(), payload->slot) : std::nullopt;
  if (camera)
  {
    m_viewCameraObject = camera;
  }
  else
  {
    Log::info(LogCategory::editor, "Controlling player " + std::to_string(payload->slot)
      + ", which has no Camera; the view is unchanged.");
  }
}
