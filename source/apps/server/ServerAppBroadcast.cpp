#include "ServerApp.h"
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <Replication.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <objects/Object.h>
#include <ScriptSystem.h>
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

void ServerApp::broadcastSnapshot() const
{
  // Refresh each Script's field blob from its live C# instance so the snapshot carries current values.
  // This reaches into the script bridge, so guard it: a field-sync hiccup must NOT stop the snapshot
  // from going out (that would leave the client with no scene at all).
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    try
    {
      m_scriptSystem->attachAll(*scene->getObjectManager());
      m_scriptSystem->syncFieldsToData(*scene->getObjectManager());
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::server, std::string("Script attach/field sync failed, sending snapshot with last-known field values: ") + e.what());
    }
  }

  // Binary snapshot: ProjectPacker writes the same project state ProjectSerializer::serialize would,
  // but tightly packed instead of JSON. ProjectSerializer stays the JSON path for file save/load.
  net::Message message(net::MessageType::snapshot);
  m_projectPacker->pack(message);

  const auto currentScene = m_sceneManager->getCurrentScene();
  Log::info(LogCategory::server, "Broadcasting snapshot: " + std::to_string(m_sceneManager->getScenes().size())
    + " scene(s), " + std::to_string(message.size()) + " bytes, currentScene='"
    + (currentScene ? uuids::to_string(currentScene->getUUID()) : std::string{}) + "'.");

  m_netServer->broadcast(message);

  broadcastSceneStatus();
}

void ServerApp::broadcastSceneStatus() const
{
  net::Message message(net::MessageType::sceneStatus);
  message.write(m_sceneManager->getSceneStatus());

  m_netServer->broadcast(message);
}

void ServerApp::broadcastEditStatus() const
{
  net::Message message(net::MessageType::editStatus);
  message.write(m_options.editMode);

  m_netServer->broadcast(message);
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
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    const auto objectManager = scene->getObjectManager();
    objectManager->flushPendingAdditions();
    objectManager->deleteObjectsMarkedForDeletion();
  }
}

void ServerApp::forwardLogToEditors() const
{
  // Caps what one call can hand to the network, so a log storm costs bounded work per loop iteration
  // instead of one send sized by however much piled up; anything past either cap is picked up on the
  // next iteration (or evicted by RemoteLogSink's own capacity first, and counted in `dropped` below).
  // maxBatchBytes stays well under TransportBackend::MaxMessageBytes (64 MiB) - RemoteLogSink::drain's
  // estimate is rough, and a batch that actually hit that ceiling would be refused and lost outright
  // rather than sent short.
  constexpr std::size_t maxEntriesPerBatch = 200;
  constexpr std::size_t maxBatchBytes = 256 * 1024;

  const auto drained = m_remoteLogSink->drain(maxEntriesPerBatch, maxBatchBytes);
  if (drained.entries.empty() && drained.dropped == 0)
  {
    return;
  }

  // packServerLog itself never logs, and sendToEditors only does on the pathological oversized-message
  // case (handled by the next drain, not recursively here) - so this cannot feed back into an ever-growing
  // stream of "forwarded a log" entries about itself.
  m_netServer->sendToEditors(net::packServerLog(drained.entries, drained.dropped));
}
