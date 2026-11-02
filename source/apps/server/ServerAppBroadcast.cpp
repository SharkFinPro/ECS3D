#include "ServerApp.h"
#include "ServerPolicy.h"
#include <assets/AssetRegistry.h>
#include <assets/ScriptClassAssets.h>
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <Replication.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/Object.h>
#include <ProfileScope.h>
#include <ScriptSystem.h>
#include "StructuralChanges.h"
#include <bindings/BindingContext.h>
#include <NetServer.h>
#include <Log.h>
#include <RemoteLogSink.h>
#include <ServerLog.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <cstddef>
#include <exception>
#include <string>

namespace {
  std::string describeSnapshot(const std::size_t sceneCount, const std::size_t bytes, const SceneAsset* currentScene)
  {
    return std::to_string(sceneCount) + " scene(s), " + std::to_string(bytes) + " bytes, currentScene='"
      + (currentScene ? uuids::to_string(currentScene->getUUID()) : std::string{}) + "'.";
  }
}

net::Message ServerApp::buildSnapshot() const
{
  ECS3D_PROFILE_SCOPE("Snapshot");

  // Refresh each Script's field blob from its live C# instance so the snapshot carries current values.
  // This reaches into the script bridge, so guard it: a field-sync hiccup must NOT stop the snapshot
  // from going out (that would leave the client with no scene at all).
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->attachAll(*scene);
      m_scriptSystem->syncFieldsToData(*scene);
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::server, std::string("Script attach/field sync failed, sending snapshot with last-known field values: ") + e.what());
    }
  }

  // Run after the project is loaded or the scripts are recompiled, so the Script slot lists exactly the classes
  // the server can attach, whatever the project file lists.
  try
  {
    syncScriptClassAssets(*m_assetRegistry, m_scriptSystem->scriptClassNames());
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Script class assets were not registered: ") + e.what());
  }

  // Binary snapshot: ProjectPacker writes the same project state ProjectSerializer::serialize would,
  // but tightly packed instead of JSON. ProjectSerializer stays the JSON path for file save/load.
  net::Message message(net::MessageType::snapshot);
  m_projectPacker->pack(message);
  return message;
}

void ServerApp::broadcastSnapshot() const
{
  const auto message = buildSnapshot();

  Log::info(LogCategory::server, "Broadcasting snapshot: "
    + describeSnapshot(m_sceneManager->getScenes().size(), message.size(), m_sceneManager->getCurrentScene().get()));

  m_netServer->broadcast(message);

  broadcastSceneStatus();
}

void ServerApp::sendJoinReply(const int32_t connId) const
{
  m_netServer->sendTo(connId, replication::buildEditStatus(m_options.editMode));

  const auto message = buildSnapshot();

  Log::info(LogCategory::server, "Sending snapshot to connection " + std::to_string(connId) + ": "
    + describeSnapshot(m_sceneManager->getScenes().size(), message.size(), m_sceneManager->getCurrentScene().get()));

  m_netServer->sendTo(connId, message);

  m_netServer->sendTo(connId, replication::buildSceneStatus(m_sceneManager->getSceneStatus()));
}

void ServerApp::broadcastSceneStatus() const
{
  m_netServer->broadcast(replication::buildSceneStatus(m_sceneManager->getSceneStatus()));
}

void ServerApp::broadcastStateDelta() const
{
  const auto scene = m_sceneManager->getCurrentScene();
  if (!scene)
  {
    return;
  }

  // Binary state delta: packStateDelta writes each object's uuid + local transform straight into the
  // message, rather than a heavier per-tick JSON dump.
  net::Message message(net::MessageType::stateDelta);
  replication::packStateDelta(message, *scene->getObjectManager());
  m_netServer->broadcast(message);
}

void ServerApp::broadcastStructuralChanges() const
{
  const auto scene = m_sceneManager->getCurrentScene();

  replicateStructuralChanges(
    scene ? scene->getObjectManager().get() : nullptr,
    BindingContext::takeComponentEdits,
    BindingContext::takeSpawned,
    BindingContext::takeDestroyed,
    BindingContext::takeStructuralComponentChanges,
    [this](const net::Message& message) { m_netServer->broadcast(message); },
    [this](const std::vector<uuids::uuid>& spawned, const std::vector<uuids::uuid>& destroyed)
    {
      notifyObjectEvents(spawned, destroyed);
    });
}

void ServerApp::forwardLogToEditors() const
{
  if (!m_remoteLogSink)
  {
    return;
  }

  // packServerLog itself never logs, and sendToEditors only does on the pathological oversized-message
  // case (handled by the next drain, not recursively here) - so this cannot feed back into an ever-growing
  // stream of "forwarded a log" entries about itself.
  forwardDrainedLog(*m_remoteLogSink, [this](const net::Message& message)
  {
    m_netServer->sendToEditors(message);
  });
}
