#include "EditorApp.h"
#include <ProjectPacker.h>
#include <Replication.h>
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
#include <string>

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

    case net::MessageType::editStatus:
      handleEditStatus(message);
      break;

    case net::MessageType::sceneStatus:
      handleSceneStatus(message);
      break;

    case net::MessageType::serverLog:
      handleServerLog(message);
      break;

    default: break;
  }
}

void EditorApp::handleSnapshot(const net::Message& message) const
{
  // Full state on join: rebuild the replicated scene from the packed project blob.
  m_projectPacker->unpack(message);

  const auto scene = m_sceneManager->getCurrentScene();
  Log::info(LogCategory::editor, "Applied snapshot (" + std::to_string(message.size()) + " bytes). Current scene: "
    + (scene ? scene->getName() : "<none>") + " ("
    + std::to_string(scene ? scene->getObjectManager()->getAllObjects().size() : 0) + " objects).");
}

void EditorApp::handleStateDelta(const net::Message& message) const
{
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    replication::unpackStateDelta(*scene->getObjectManager(), message);
  }
}

void EditorApp::handleEditComponent(const net::Message& message) const
{
  // Another editor (or this one, echoed by the server) changed a component. Like the client, a missed
  // edit is logged rather than ignored, so a real desync stands out from the ordinary rebroadcast race.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const auto result = replication::applyComponentEdit(*scene->getObjectManager(), message);
    replication::logMissedComponentEdit(result, message, LogCategory::editor);
  }
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

void EditorApp::handleEditStatus(const net::Message& message)
{
  net::MessageReader reader(message);

  // The server told us whether it's editable; a non-edit server makes the editor a read-only viewer.
  m_serverEditable = reader.read<bool>();

  if (!m_serverEditable)
  {
    logMessage("Info", "Connected to a non-edit server - the editor is read-only.");
  }
}

void EditorApp::handleSceneStatus(const net::Message& message)
{
  net::MessageReader reader(message);
  const auto status = reader.read<SceneStatus>();

  // Starting a scene snapshots the authored tree and stopping it rebuilds from that snapshot, so an edit
  // recorded on either side of the transition no longer describes the objects that are there. A
  // pause/resume leaves the scene exactly as it is, so it keeps the history. Read against the last
  // REPORTED status rather than m_sceneStatus, whose optimistic default would make the server's first
  // report (an edit server starts stopped) look like a stop.
  if (m_reportedSceneStatus.has_value() && m_reportedSceneStatus.value() != status
      && (m_reportedSceneStatus.value() == SceneStatus::stopped || status == SceneStatus::stopped))
  {
    m_editHistory.clear();
  }

  m_reportedSceneStatus = status;
  m_sceneStatus = status;
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
