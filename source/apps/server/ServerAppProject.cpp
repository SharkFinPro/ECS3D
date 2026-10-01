#include "ServerApp.h"
#include <ProjectSerializer.h>
#include <ProjectPacker.h>
#include <assets/AssetRegistry.h>
#include <scenes/SceneManager.h>
#include <scenes/SceneAsset.h>
#include <objects/ObjectManager.h>
#include <objects/Object.h>
#include <objects/components/Component.h>
#include <CollisionSystem.h>
#include <ScriptSystem.h>
#include <Log.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <exception>
#include <string>

void ServerApp::handleLoadProject(const net::Message& message) const
{
  // An editor opened a different project: stop the current scripts, swap the project in, restart,
  // and snapshot so every view rebuilds. The blob is sent (not a path) so it works off-machine too.
  Log::info(LogCategory::server, "Received loadProject (" + std::to_string(message.bytes().size()) + " bytes).");

  // Only a failed load needs this: the scene that survives it was running, so its scripts are restarted.
  const bool wasRunning = m_sceneManager->getSceneStatus() == SceneStatus::running;

  // Stop the current scripts before the scene is swapped out from under them.
  if (const auto scene = m_sceneManager->getCurrentScene())
  {
    stopScriptsLogged(*scene->getObjectManager());
  }

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
        startScriptsLogged(*scene->getObjectManager());
      }
    }

    return;
  }

  finishProjectLoad();
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

  broadcastSnapshot();
}

void ServerApp::startScriptsLogged(ObjectManager& objectManager) const
{
  try
  {
    m_scriptSystem->start(objectManager);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
}

void ServerApp::stopScriptsLogged(ObjectManager& objectManager) const
{
  try
  {
    m_scriptSystem->stop(objectManager);
  }
  catch (const std::exception& e)
  {
    Log::error(LogCategory::server, e.what());
  }
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
    try
    {
      m_scriptSystem->stop(*current->getObjectManager());
    }
    catch (const std::exception& e)
    {
      Log::error(LogCategory::server, e.what());
    }
  }

  m_sceneManager->loadScene(scene);

  // New scene: contact history from the previous scene is meaningless here.
  m_collisionSystem->reset();

  Log::info(LogCategory::server, "Switched to scene '" + scene->getName() + "' ("
    + std::to_string(scene->getObjectManager()->getAllObjects().size()) + " objects, stopped).");

  broadcastSnapshot();
}
