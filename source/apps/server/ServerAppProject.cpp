#include "ServerApp.h"
#include <ProjectPacker.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <terrain/TerrainReplication.h>
#include <terrain/TerrainStore.h>
#include <objects/ObjectManager.h>
#include <objects/components/Component.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <Log.h>
#include <uuid.h>
#include <exception>
#include <string>

void ServerApp::handleLoadProject(const net::Message& message, const int32_t senderId)
{
  // An editor opened a different project: stop the current scripts, swap the project in, restart,
  // and snapshot so every view rebuilds. The blob is sent (not a path) so it works off-machine too.
  Log::info(LogCategory::server, "Received loadProject (" + std::to_string(message.bytes().size()) + " bytes).");

  // Only a failed load needs this: the scene that survives it was running, so its scripts are restarted.
  const bool wasRunning = m_sceneManager->getSceneStatus() == SceneStatus::running;

  // Stop the current scripts before the scene is swapped out from under them.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    stopScriptsLogged(*scene);
  }

  // Before the unpack, so a failed one cannot leave the previous project's expectations for uuids both share.
  m_terrainStore->clearExpectations();
  m_terrainUploadWarned.clear();
  m_terrainUploader = senderId;

  try
  {
    // Same packed shape as a snapshot: unpack clears + rebuilds the AssetRegistry/SceneManager and loads
    // the current scene. ProjectSerializer stays the JSON path for file save/load.
    m_projectPacker->unpack(message);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Failed to load project from editor: ") + e.what());

    // unpack parses into locals and only swaps on failure-free completion, so a throw leaves the current
    // scene untouched - the same one whose scripts were just stopped. Restart them only if it was running.
    if (wasRunning)
    {
      if (const auto scene = m_sceneManager->getCurrentScene())
      {
        startScriptsLogged(*scene);
      }
    }

    return;
  }

  // The project is in; a failure here leaves terrain behind it, not the scene, so the load still finishes.
  resetTerrainForLoadedProject();

  finishProjectLoad();
}

void ServerApp::resetTerrainForLoadedProject()
{
  try
  {
    m_terrainStore->resetVolumes();

    // Every terrain, so a count of 0 also clears an expectation an earlier load left behind.
    for (const auto& uuid : m_terrainStore->terrains())
    {
      if (const auto* description = m_terrainStore->description(uuid))
      {
        m_terrainStore->expectChunks(uuid, description->chunkCount);
      }
    }

    // The volumes are empty now, so every stream ends here; where it started from no longer matters.
    const terrain::TerrainStreamer::FocusFor origin = [](const uuids::uuid&, const terrain::TerrainDescription&)
    {
      return glm::vec3(0.0f);
    };

    for (const auto& uuid : m_terrainStore->terrains())
    {
      m_terrainStreamer.restart(uuid, *m_terrainStore, origin);
    }
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, std::string("Failed to reset terrain after loading the project: ") + e.what());
  }
}

void ServerApp::dropTerrainUploadFrom(const int32_t connId)
{
  if (m_terrainUploader != connId)
  {
    return;
  }

  m_terrainUploader.reset();

  if (const auto reason = terrain::pendingUploadReason(*m_terrainStore))
  {
    m_terrainStore->clearExpectations();
    Log::warn(LogCategory::server, "The editor uploading the project's terrain disconnected (" + *reason +
      "); the terrain stays incomplete until a project is opened again.");
  }
}

void ServerApp::handleTerrainUpload(const net::Message& message)
{
  const auto payload = terrain::unpackTerrainChunks(message);
  if (!payload)
  {
    Log::warn(LogCategory::server, "Refused a malformed terrain upload (" + std::to_string(message.bytes().size())
      + " bytes).");
    return;
  }

  const std::string name = uuids::to_string(payload->terrain);
  const auto result = terrain::applyTerrainUpload(*m_terrainStore, *payload);

  if (result.unknownTerrain || result.notExpected)
  {
    if (m_terrainUploadWarned.insert(payload->terrain).second)
    {
      Log::warn(LogCategory::server, "Ignored a terrain upload for " + std::string(result.unknownTerrain
        ? "unknown terrain " + name : "terrain " + name + ": no upload is expected"));
    }

    return;
  }

  if (result.ignoredHashOnly > 0)
  {
    Log::warn(LogCategory::server, "Terrain " + name + ": ignored " + std::to_string(result.ignoredHashOnly)
      + " hash-only upload entries");
  }

  m_terrainStreamer.requeue(payload->terrain, result.applied);

  if (result.completed)
  {
    const auto volume = m_terrainStore->find(payload->terrain);
    Log::info(LogCategory::server, "Terrain " + name + " uploaded: " + std::to_string(volume->chunks().size())
      + " chunk(s)");
  }
}

void ServerApp::finishProjectLoad() const
{
  // New project/scene: any contact history belongs to the project we just swapped out.
  m_collisionSystem->reset();

  // unpack() leaves the scene stopped, and a loaded project stays that way until the editor presses play.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    Log::info(LogCategory::server, "Loaded project from editor: scene '" + scene->getName() + "' ("
      + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects, stopped).");
  }

  broadcastSnapshot(net::SnapshotReason::projectLoad);
}

void ServerApp::startScriptsLogged(SceneAsset& scene) const
{
  try
  {
    startSceneScripts(scene);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
}

void ServerApp::stopScriptsLogged(SceneAsset& scene) const
{
  try
  {
    m_scriptSystem->stop(scene);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }

  discardScriptBuffers();
}

void ServerApp::loadScene(const std::string& sceneUUID) const
{
  const auto parsed = uuids::uuid::from_string(sceneUUID);
  if (!parsed.has_value())
  {
    return;
  }

  const auto scene = m_sceneManager->getScene(parsed.value());
  if (!scene)
  {
    return;
  }

  // Stop the outgoing scene's scripts before switching the active scene.
  if (const auto current = m_sceneManager->getCurrentScene())
  {
    stopScriptsLogged(*current);
  }

  m_sceneManager->loadScene(scene);

  // New scene: contact history from the previous scene is meaningless here.
  m_collisionSystem->reset();

  Log::info(LogCategory::server, "Switched to scene '" + scene->getName() + "' ("
    + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects, stopped).");

  broadcastSnapshot(net::SnapshotReason::sceneControl);
}
