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
#include <ScriptSystem.h>
#include <SceneObjectDiff.h>
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
  // A script's component edit (e.g. ModelRendererBindings swapping a model/texture) isn't covered by the
  // per-tick state delta, which only carries Transform - so it replicates like an editor edit instead:
  // rebuild the wire message from the mutated component and broadcast it the same way applyComponentEdit's
  // caller does above.
  for (const auto& [objectUUID, component] : BindingContext::takeComponentEdits())
  {
    m_netServer->broadcast(replication::buildComponentEdit(objectUUID, component));
  }

  // The spawn/destroy bindings buffered what the scripts did on BindingContext (scripting can't reach the
  // net layer). Broadcast spawns before destroys, then remove the marked objects from the authoritative
  // scene. A spawned object is still live here, so its packed blob carries current transform/components.
  const auto spawned = BindingContext::takeSpawned();
  for (const auto& object : spawned)
  {
    m_netServer->broadcast(replication::buildObjectSpawned(*object));
  }

  const auto destroyed = BindingContext::takeDestroyed();
  for (const auto& uuid : destroyed)
  {
    m_netServer->broadcast(replication::buildObjectDestroyed(uuid));
  }

  // Both flushes run here, once per frame, regardless of which (if either) fired this frame: a spawn-only
  // frame still has to admit its pending objects into the scene, and flushing additions before deletions
  // means an object spawned then destroyed in the same frame's script passes is a real member of the list
  // by the time the delete pass looks for it.
  std::vector<uuids::uuid> spawnedUUIDs;
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const auto objectManager = scene->getObjectManager();
    objectManager->flushPendingAdditions();
    objectManager->deleteObjectsMarkedForDeletion();

    // An object spawned and destroyed in the same pass is gone by now and is not reported as spawned.
    for (const auto& uuid : sceneObjectDiff::flattenSubtrees(spawned))
    {
      if (objectManager->getObjectByUUID(uuid))
      {
        spawnedUUIDs.push_back(uuid);
      }
    }
  }

  // Handlers run here can spawn or destroy too; those land in the buffers for the next call rather than
  // recursing.
  notifyObjectEvents(spawnedUUIDs, destroyed);

  // A script's addComponent/removeComponent (ComponentOpsBindings) already applied to each object listed
  // here - unlike spawn/destroy there is nothing further to apply locally - but the change isn't covered
  // by the per-tick state delta or a single-component buildComponentEdit either, so each changed object's
  // current packed state goes out as its own objectComponentsChanged message: cheaper than the editor's
  // own sceneEdit addComponent/removeComponent ops, which re-snapshot the whole project, since only the
  // objects that actually changed need to reach a client. Read after the deletion pass above, so a uuid
  // whose object left the scene this same tick (already covered by an objectDestroyed above) is silently
  // skipped rather than resent for an object that no longer exists.
  const auto changedUUIDs = BindingContext::takeStructuralComponentChanges();
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const auto objectManager = scene->getObjectManager();
    for (const auto& objectUUID : changedUUIDs)
    {
      if (const auto object = objectManager->getObjectByUUID(objectUUID))
      {
        m_netServer->broadcast(replication::buildObjectComponentsChanged(*object));
      }
    }
  }
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
