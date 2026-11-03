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
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <exception>
#include <string>

namespace {
  std::string describeSnapshot(const std::size_t sceneCount, const std::size_t bytes, const SceneAsset* currentScene)
  {
    return std::to_string(sceneCount) + " scene(s), " + std::to_string(bytes) + " bytes, currentScene='"
      + (currentScene ? uuids::to_string(currentScene->getUUID()) : std::string{}) + "'.";
  }

  template <typename T>
  T saturatedTo(const uint64_t value)
  {
    return static_cast<T>(std::min<uint64_t>(value, std::numeric_limits<T>::max()));
  }
}

net::Message ServerApp::buildSnapshot(uint32_t& buildMicros) const
{
  ECS3D_PROFILE_SCOPE("Snapshot");

  const auto buildStart = std::chrono::steady_clock::now();

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

  buildMicros = saturatedTo<uint32_t>(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - buildStart).count()));
  return message;
}

void ServerApp::broadcastSnapshot(const net::SnapshotReason reason) const
{
  uint32_t buildMicros = 0;
  const auto message = buildSnapshot(buildMicros);

  Log::info(LogCategory::server, "Broadcasting snapshot: "
    + describeSnapshot(m_sceneManager->getScenes().size(), message.size(), m_sceneManager->getCurrentScene().get()));

  m_netServer->broadcast(message);

  m_snapshotLog.record({reason, saturatedTo<uint32_t>(message.size()), buildMicros,
                        saturatedTo<uint16_t>(static_cast<uint64_t>(std::max(m_netServer->connectionCount(), 0))),
                        std::chrono::steady_clock::now()});

  broadcastSceneStatus();
}

void ServerApp::sendJoinReply(const int32_t connId) const
{
  m_netServer->sendTo(connId, replication::buildEditStatus(m_options.editMode));

  uint32_t buildMicros = 0;
  const auto message = buildSnapshot(buildMicros);

  Log::info(LogCategory::server, "Sending snapshot to connection " + std::to_string(connId) + ": "
    + describeSnapshot(m_sceneManager->getScenes().size(), message.size(), m_sceneManager->getCurrentScene().get()));

  m_netServer->sendTo(connId, message);

  m_snapshotLog.record({net::SnapshotReason::join, saturatedTo<uint32_t>(message.size()), buildMicros, 1,
                        std::chrono::steady_clock::now()});

  m_netServer->sendTo(connId, replication::buildSceneStatus(m_sceneManager->getSceneStatus()));
}

void ServerApp::sendServerStatsIfDue(const std::chrono::steady_clock::time_point now)
{
  if (now - m_lastStatsFeedTime < serverStatsPeriod)
  {
    return;
  }

  m_lastStatsFeedTime = now;

  StatsFeedInput input;
  input.net = {m_netServer->traffic().totalOut(), m_netServer->traffic().totalIn(), m_netServer->connectionCount(),
               &m_snapshotLog};
  input.now = now;

  if (m_statsSubscriptions.empty())
  {
    skipServerStats(m_lane, m_statsFeedCursor, input);
    return;
  }

  ECS3D_PROFILE_SCOPE("Stats");

  input.editors = m_netServer->editorCount();
#ifndef NDEBUG
  input.flags |= net::serverStatsFlags::debugBuild;
#endif
  if (m_options.editMode)
  {
    input.flags |= net::serverStatsFlags::editMode;
  }

  if (sceneRunning())
  {
    input.flags |= net::serverStatsFlags::sceneRunning;
  }

  // Both copies are packed before takeRecipients records which table each subscriber was sent, so a throw
  // leaves the subscriptions as they were.
  try
  {
    const auto stats = collectServerStats(m_lane, m_counters, m_statsFeedCursor, input);
    const auto withTable = net::packServerStats(stats, true);
    const auto withoutTable = net::packServerStats(stats, false);
    const auto recipients = m_statsSubscriptions.takeRecipients(stats.nodeTableEpoch);

    if (!recipients.withNodeTable.empty())
    {
      m_netServer->sendToConnections(recipients.withNodeTable, withTable);
    }

    if (!recipients.withoutNodeTable.empty())
    {
      m_netServer->sendToConnections(recipients.withoutNodeTable, withoutTable);
    }

    m_statsFeedFailing = false;
  }
  catch (const std::exception& e)
  {
    if (!m_statsFeedFailing)
    {
      Log::error(LogCategory::server, std::string("Server statistics were not sent: ") + e.what());
    }

    m_statsFeedFailing = true;
  }
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
